#ifndef KERNEL_MODULE
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#endif

#include "config.h"
#include "spi.h"
#include "util.h"

volatile GPIORegisterFile *gpio = 0;
volatile SPIRegisterFile *spi = 0;

// Synchonously performs a single SPI command byte + N data bytes transfer on the calling thread. Call in between a BEGIN_SPI_COMMUNICATION() and END_SPI_COMMUNICATION() pair.
void RunSPITask(SPITask *task)
{
  // An SPI transfer to the display always starts with one control (command) byte, followed by N data bytes.
  uint32_t cs;
  while (!((cs = spi->cs) & BCM2835_SPI0_CS_DONE))
    if ((cs & (BCM2835_SPI0_CS_RXR | BCM2835_SPI0_CS_RXF)))
      spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;

  if ((cs & BCM2835_SPI0_CS_RXD)) spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;

  CLEAR_GPIO(GPIO_TFT_DATA_CONTROL);
  spi->fifo = task->cmd;

  uint8_t *tStart = task->data;
  uint8_t *tEnd = task->data + task->size;
  uint8_t *tPrefillEnd = task->data + MIN(15, task->size);
  while(!(spi->cs & (BCM2835_SPI0_CS_RXD|BCM2835_SPI0_CS_DONE))) /*nop*/;

  SET_GPIO(GPIO_TFT_DATA_CONTROL);

  while(tStart < tPrefillEnd) spi->fifo = *tStart++;
  while(tStart < tEnd)
  {
    cs = spi->cs;
    if ((cs & BCM2835_SPI0_CS_TXD)) spi->fifo = *tStart++;
    if ((cs & (BCM2835_SPI0_CS_RXR|BCM2835_SPI0_CS_RXF))) spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;
  }
}

SharedMemory *spiTaskMemory = 0;
volatile uint64_t spiThreadIdleUsecs = 0;
volatile uint64_t spiThreadSleepStartTime = 0;
volatile int spiThreadSleeping = 0;
double spiUsecsPerByte;

SPITask *GetTask() // Returns the first task in the queue, called in worker thread
{
  uint32_t head = spiTaskMemory->queueHead;
  uint32_t tail = spiTaskMemory->queueTail;
  if (head == tail) return 0;
  SPITask *task = (SPITask*)(spiTaskMemory->buffer + head);
  if (task->cmd == 0) // Wrapped around?
  {
    spiTaskMemory->queueHead = 0;
    __sync_synchronize();
    if (tail == 0) return 0;
    task = (SPITask*)spiTaskMemory->buffer;
  }
  return task;
}

void DoneTask(SPITask *task) // Frees the first SPI task from the queue, called in worker thread
{
  __atomic_fetch_sub(&spiTaskMemory->spiBytesQueued, task->size+1, __ATOMIC_RELAXED);
  spiTaskMemory->queueHead = (uint32_t)((uint8_t*)task - spiTaskMemory->buffer) + sizeof(SPITask) + task->size;
  __sync_synchronize();
}

#ifndef KERNEL_MODULE
// A worker thread that keeps the SPI bus filled at all times
void *spi_thread(void *unused)
{
  for(;;)
  {
    if (spiTaskMemory->queueTail != spiTaskMemory->queueHead)
    {
      BEGIN_SPI_COMMUNICATION();
      {
        while(spiTaskMemory->queueTail != spiTaskMemory->queueHead)
        {
          SPITask *task = GetTask();
          if (task)
          {
            RunSPITask(task);
            DoneTask(task);
          }
        }
      }
      END_SPI_COMMUNICATION();
    }
    else
    {
#ifdef STATISTICS
      uint64_t t0 = tick();
      spiThreadSleepStartTime = t0;
      __atomic_store_n(&spiThreadSleeping, 1, __ATOMIC_RELAXED);
#endif
      syscall(SYS_futex, &spiTaskMemory->queueTail, FUTEX_WAIT, spiTaskMemory->queueHead, 0, 0, 0); // Start sleeping until we get new tasks
#ifdef STATISTICS
      __atomic_store_n(&spiThreadSleeping, 0, __ATOMIC_RELAXED);
      uint64_t t1 = tick();
      __sync_fetch_and_add(&spiThreadIdleUsecs, t1-t0);
#endif
    }
  }
}
#endif

int InitSPI()
{
#ifdef KERNEL_MODULE

#define BCM2835_PERI_BASE               0x3F000000
#define BCM2835_GPIO_BASE               0x200000
#define BCM2835_SPI0_BASE               0x204000
  printk("ioremapping %p\n", (void*)(BCM2835_PERI_BASE+BCM2835_GPIO_BASE));
  void *bcm2835 = ioremap(BCM2835_PERI_BASE+BCM2835_GPIO_BASE, 32768);
  printk("Got bcm address %p\n", bcm2835);
  if (!bcm2835) FATAL_ERROR("Failed to map BCM2835 address!");
  spi = (volatile SPIRegisterFile*)((uintptr_t)bcm2835 + BCM2835_SPI0_BASE - BCM2835_GPIO_BASE);
  gpio = (volatile GPIORegisterFile*)((uintptr_t)bcm2835);

#else // Userland version
  // Find the memory address to the BCM2835 peripherals
  FILE *fp = fopen("/proc/device-tree/soc/ranges", "rb");
  if (!fp) FATAL_ERROR("Failed to open /proc/device-tree/soc/ranges!");
  struct { uint32_t reserved, peripheralsAddress, peripheralsSize; } ranges;
  int n = fread(&ranges, sizeof(ranges), 1, fp);
  fclose(fp);
  if (n != 1) FATAL_ERROR("Failed to read /proc/device-tree/soc/ranges!");

  // Memory map GPIO and SPI peripherals for direct access
  int mem = open("/dev/mem", O_RDWR|O_SYNC);
  if (mem < 0) FATAL_ERROR("can't open /dev/mem (run as sudo)");
  void *bcm2835 = mmap(NULL, be32toh(ranges.peripheralsSize), (PROT_READ | PROT_WRITE), MAP_SHARED, mem, be32toh(ranges.peripheralsAddress));
  if (bcm2835 == MAP_FAILED) FATAL_ERROR("mapping /dev/mem failed");
  spi = (volatile SPIRegisterFile*)((uintptr_t)bcm2835 + BCM2835_SPI0_BASE);
  gpio = (volatile GPIORegisterFile*)((uintptr_t)bcm2835 + BCM2835_GPIO_BASE);
  close(mem);
#endif

  // Estimate how many microseconds transferring a single byte over the SPI bus takes?
  spiUsecsPerByte = 8.0/*bits/byte*/ * SPI_BUS_CLOCK_DIVISOR * 9.0/8.0/*BCM2835 SPI master idles for one bit per each byte*/ / 400/*Approx BCM2835 SPI clock (250MHz is lowest, turbo is at 400MHz)*/;

#ifndef KERNEL_MODULE_CLIENT
  // By default all GPIO pins are in input mode (0x00), initialize them for SPI and GPIO writes
  SET_GPIO_MODE(GPIO_TFT_DATA_CONTROL, 0x01); // Data/Control pin to output (0x01)
  SET_GPIO_MODE(GPIO_SPI0_CE1, 0x04); // Set the SPI0 pins to the Alt 0 function (0x04) to enable SPI0 access on them
  SET_GPIO_MODE(GPIO_SPI0_CE0, 0x04);
  SET_GPIO_MODE(GPIO_SPI0_MISO, 0x04);
  SET_GPIO_MODE(GPIO_SPI0_MOSI, 0x04);
  SET_GPIO_MODE(GPIO_SPI0_CLK, 0x04);

  spi->cs = BCM2835_SPI0_CS_CLEAR; // Initialize the Control and Status register to defaults: CS=0 (Chip Select), CPHA=0 (Clock Phase), CPOL=0 (Clock Polarity), CSPOL=0 (Chip Select Polarity), TA=0 (Transfer not active), and reset TX and RX queues.
  spi->clk = SPI_BUS_CLOCK_DIVISOR; // Clock Divider determines SPI bus speed, resulting speed=256MHz/clk
#endif

  // Initialize SPI thread task buffer memory
#ifdef KERNEL_MODULE_CLIENT
  int driverfd = open("/proc/lkmc_mmap", O_RDWR|O_SYNC);
  if (driverfd < 0) FATAL_ERROR("Could not open SPI ring buffer - kernel driver module not running?");
  spiTaskMemory = (SharedMemory*)mmap(NULL, SHARED_MEMORY_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED/* | MAP_NORESERVE | MAP_POPULATE | MAP_LOCKED*/, driverfd, 0);
  close(driverfd);
  if (spiTaskMemory == MAP_FAILED) FATAL_ERROR("Could not mmap SPI ring buffer!");
  printf("Got shared memory block %p, ring buffer head %p, ring buffer tail %p\n", (const char *)spiTaskMemory, spiTaskMemory->queueHead, spiTaskMemory->queueTail);
#else

#ifdef KERNEL_MODULE
  spiTaskMemory = (SharedMemory*)kmalloc(SHARED_MEMORY_SIZE, GFP_KERNEL);
#else
  spiTaskMemory = (SharedMemory*)malloc(SHARED_MEMORY_SIZE);
#endif

  spiTaskMemory->queueHead = spiTaskMemory->queueTail = spiTaskMemory->spiBytesQueued = 0;
#endif

#if !defined(KERNEL_MODULE) && !defined(KERNEL_MODULE_CLIENT)
  InitSPIDisplay();

  // Create a dedicated thread to feed the SPI bus. While this is fast, it consumes a lot of CPU. It would be best to replace
  // this thread with a kernel module that processes the created SPI task queue using interrupts. (while juggling the GPIO D/C line as well)
  pthread_t thread;
  int rc = pthread_create(&thread, NULL, spi_thread, NULL); // After creating the thread, it is assumed to have ownership of the SPI bus, so no SPI chat on the main thread after this.
  if (rc != 0) FATAL_ERROR("Failed to create SPI thread!");
#endif

  return 0;
}

void DeinitSPI()
{
#ifndef KERNEL_MODULE_CLIENT

#ifdef KERNEL_MODULE
  kfree(spiTaskMemory);
#else
  free(spiTaskMemory);
#endif
  spiTaskMemory = 0;
  SET_GPIO_MODE(GPIO_TFT_DATA_CONTROL, 0);
  SET_GPIO_MODE(GPIO_SPI0_CE1, 0);
  SET_GPIO_MODE(GPIO_SPI0_CE0, 0);
  SET_GPIO_MODE(GPIO_SPI0_MISO, 0);
  SET_GPIO_MODE(GPIO_SPI0_MOSI, 0);
  SET_GPIO_MODE(GPIO_SPI0_CLK, 0);
#endif
}
