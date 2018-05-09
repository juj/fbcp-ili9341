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
#include <errno.h>
#endif

#include "config.h"
#include "spi.h"
#include "util.h"
#include "dma.h"

int mem_fd = -1;
volatile void *bcm2835 = 0;
volatile GPIORegisterFile *gpio = 0;
volatile SPIRegisterFile *spi = 0;

void DumpSPICS(uint32_t reg)
{
  PRINT_FLAG(BCM2835_SPI0_CS_CS);
  PRINT_FLAG(BCM2835_SPI0_CS_CPHA);
  PRINT_FLAG(BCM2835_SPI0_CS_CPOL);
  PRINT_FLAG(BCM2835_SPI0_CS_CLEAR_TX);
  PRINT_FLAG(BCM2835_SPI0_CS_CLEAR_RX);
  PRINT_FLAG(BCM2835_SPI0_CS_TA);
  PRINT_FLAG(BCM2835_SPI0_CS_DMAEN);
  PRINT_FLAG(BCM2835_SPI0_CS_INTD);
  PRINT_FLAG(BCM2835_SPI0_CS_INTR);
  PRINT_FLAG(BCM2835_SPI0_CS_DONE);
  PRINT_FLAG(BCM2835_SPI0_CS_RXD);
  PRINT_FLAG(BCM2835_SPI0_CS_TXD);
  PRINT_FLAG(BCM2835_SPI0_CS_RXR);
  PRINT_FLAG(BCM2835_SPI0_CS_RXF);
}

// Errata to BCM2835 behavior: documentation states that the SPI0 DLEN register is only used for DMA. However, even when DMA is not being utilized, setting it from
// a value != 0 or 1 gets rid of an excess idle clock cycle that is present when transmitting each byte. (by default in Polled SPI Mode each 8 bits transfer in 9 clocks)
// With DLEN=2 each byte is clocked to the bus in 8 cycles, observed to improve max throughput from 56.8mbps to 63.3mbps (+11.4%, quite close to the theoretical +12.5%)
// https://www.raspberrypi.org/forums/viewtopic.php?f=44&t=181154
#define UNLOCK_FAST_8_CLOCKS_SPI() (spi->dlen = 2)

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

// For small transfers, using DMA is not worth it, but pushing through with polled SPI gives better bandwidth.
// For larger transfers though that are more than this amount of bytes, using DMA is faster.
// This cutoff number was experimentally tested to find where Polled SPI and DMA are as fast.
#define DMA_IS_FASTER_THAN_POLLED_SPI 240
  // Do a DMA transfer if this task is suitable in size for DMA to handle
#ifdef USE_DMA_TRANSFERS
  if (tEnd - tStart > DMA_IS_FASTER_THAN_POLLED_SPI)
  {
    SPIDMATransfer(task);

    // After having done a DMA transfer, the SPI0 DLEN register has reset to zero, so restore it to fast mode.
    UNLOCK_FAST_8_CLOCKS_SPI();
  }
  else
#endif
  {
    while(tStart < tPrefillEnd) spi->fifo = *tStart++;
    while(tStart < tEnd)
    {
      cs = spi->cs;
      if ((cs & BCM2835_SPI0_CS_TXD)) spi->fifo = *tStart++;
// TODO:      else asm volatile("yield");
      if ((cs & (BCM2835_SPI0_CS_RXR|BCM2835_SPI0_CS_RXF))) spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;
    }
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
  mem_fd = open("/dev/mem", O_RDWR|O_SYNC);
  if (mem_fd < 0) FATAL_ERROR("can't open /dev/mem (run as sudo)");
  bcm2835 = mmap(NULL, be32toh(ranges.peripheralsSize), (PROT_READ | PROT_WRITE), MAP_SHARED, mem_fd, be32toh(ranges.peripheralsAddress));
  if (bcm2835 == MAP_FAILED) FATAL_ERROR("mapping /dev/mem failed");
  spi = (volatile SPIRegisterFile*)((uintptr_t)bcm2835 + BCM2835_SPI0_BASE);
  gpio = (volatile GPIORegisterFile*)((uintptr_t)bcm2835 + BCM2835_GPIO_BASE);
  // TODO: On graceful shutdown, (ctrl-c signal?) close(mem_fd)
#endif

  // Estimate how many microseconds transferring a single byte over the SPI bus takes?
  spiUsecsPerByte = 8.0/*bits/byte*/ * SPI_BUS_CLOCK_DIVISOR / 400/*Approx BCM2835 SPI clock (250MHz is lowest, turbo is at 400MHz)*/;

#if !defined(KERNEL_MODULE_CLIENT) || defined(KERNEL_MODULE_CLIENT_DRIVES)
  // By default all GPIO pins are in input mode (0x00), initialize them for SPI and GPIO writes
  SET_GPIO_MODE(GPIO_TFT_DATA_CONTROL, 0x01); // Data/Control pin to output (0x01)
  SET_GPIO_MODE(GPIO_SPI0_MISO, 0x04);
  SET_GPIO_MODE(GPIO_SPI0_MOSI, 0x04);
  SET_GPIO_MODE(GPIO_SPI0_CLK, 0x04);

  // Set the SPI 0 pin explicitly to output, and enable chip select on the line by setting it to low.
  // fbcp-ili9341 assumes exclusive access to the SPI0 bus, and exclusive presence of only one device on the bus,
  // which is (permanently) activated here.
  SET_GPIO_MODE(GPIO_SPI0_CE0, 0x01);
  CLEAR_GPIO(GPIO_SPI0_CE0);

  spi->cs = BCM2835_SPI0_CS_CLEAR; // Initialize the Control and Status register to defaults: CS=0 (Chip Select), CPHA=0 (Clock Phase), CPOL=0 (Clock Polarity), CSPOL=0 (Chip Select Polarity), TA=0 (Transfer not active), and reset TX and RX queues.
  spi->clk = SPI_BUS_CLOCK_DIVISOR; // Clock Divider determines SPI bus speed, resulting speed=256MHz/clk
#endif

  // Initialize SPI thread task buffer memory
#ifdef KERNEL_MODULE_CLIENT
  int driverfd = open("/proc/bcm2835_spi_display_bus", O_RDWR|O_SYNC);
  if (driverfd < 0) FATAL_ERROR("Could not open SPI ring buffer - kernel driver module not running?");
  spiTaskMemory = (SharedMemory*)mmap(NULL, SHARED_MEMORY_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED/* | MAP_NORESERVE | MAP_POPULATE | MAP_LOCKED*/, driverfd, 0);
  close(driverfd);
  if (spiTaskMemory == MAP_FAILED) FATAL_ERROR("Could not mmap SPI ring buffer!");
  printf("Got shared memory block %p, ring buffer head %p, ring buffer tail %p, shared memory block phys address: %p\n", (const char *)spiTaskMemory, spiTaskMemory->queueHead, spiTaskMemory->queueTail, spiTaskMemory->sharedMemoryBaseInPhysMemory);

#ifdef USE_DMA_TRANSFERS
  printf("DMA TX channel: %d, DMA RX channel: %d\n", spiTaskMemory->dmaTxChannel, spiTaskMemory->dmaRxChannel);
#endif

#else

#ifdef KERNEL_MODULE
  spiTaskMemory = (SharedMemory*)kmalloc(SHARED_MEMORY_SIZE, GFP_KERNEL | GFP_DMA);
  // TODO: Ideally we would be able to directly perform the DMA from the SPI ring buffer in 'spiTaskMemory'. However
  // that pointer is shared to userland, and it is proving troublesome to make it both userland-writable as well as cache-bypassing DMA coherent.
  // Therefore these two memory areas are separate for now, and we memcpy() from SPI ring buffer to the following intermediate 'dmaSourceMemory'
  // memory area to perform the DMA transfer. Is there a way to avoid this intermediate buffer? That would improve performance a bit.
  dmaSourceMemory = (SharedMemory*)dma_alloc_writecombine(0, SHARED_MEMORY_SIZE, &spiTaskMemoryPhysical, GFP_KERNEL);
  LOG("Allocated DMA memory: mem: %p, phys: %p", spiTaskMemory, (void*)spiTaskMemoryPhysical);
  memset((void*)spiTaskMemory, 0, SHARED_MEMORY_SIZE);
#else
  spiTaskMemory = (SharedMemory*)malloc(SHARED_MEMORY_SIZE);
#endif

  spiTaskMemory->queueHead = spiTaskMemory->queueTail = spiTaskMemory->spiBytesQueued = 0;
#endif

#ifdef USE_DMA_TRANSFERS
  InitDMA();
#endif

  // Enable fast 8 clocks per byte transfer mode, instead of slower 9 clocks per byte.
  UNLOCK_FAST_8_CLOCKS_SPI();

#if !defined(KERNEL_MODULE) && (!defined(KERNEL_MODULE_CLIENT) || defined(KERNEL_MODULE_CLIENT_DRIVES))
  printf("Initializing display\n");
  InitSPIDisplay();

  // Create a dedicated thread to feed the SPI bus. While this is fast, it consumes a lot of CPU. It would be best to replace
  // this thread with a kernel module that processes the created SPI task queue using interrupts. (while juggling the GPIO D/C line as well)
  pthread_t thread;
  printf("Creating SPI task thread\n");
  int rc = pthread_create(&thread, NULL, spi_thread, NULL); // After creating the thread, it is assumed to have ownership of the SPI bus, so no SPI chat on the main thread after this.
  if (rc != 0) FATAL_ERROR("Failed to create SPI thread!");
#endif

  LOG("InitSPI done");
  return 0;
}

void DeinitSPI()
{
#ifndef KERNEL_MODULE_CLIENT

#ifdef KERNEL_MODULE
  kfree(spiTaskMemory);
  dma_free_writecombine(0, SHARED_MEMORY_SIZE, dmaSourceMemory, spiTaskMemoryPhysical);
  spiTaskMemoryPhysical = 0;
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
