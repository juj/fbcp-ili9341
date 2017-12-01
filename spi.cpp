#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>

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
  do {
    cs = spi->cs;
    if ((cs & BCM2835_SPI0_CS_RXD)) spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;
  } while (!(cs & (BCM2835_SPI0_CS_DONE)));

  spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;

  CLEAR_GPIO(GPIO_TFT_DATA_CONTROL);
  spi->fifo = task->cmd;

  uint8_t *tStart = task->data;
  uint8_t *tEnd = task->data + task->bytes;
  uint8_t *tPrefillEnd = task->data + MIN(15, task->bytes);

  while(!(spi->cs & (BCM2835_SPI0_CS_RXD|BCM2835_SPI0_CS_DONE)));
  SET_GPIO(GPIO_TFT_DATA_CONTROL);

  while(tStart < tPrefillEnd) spi->fifo = *tStart++;
  while(tStart < tEnd)
  {
    uint32_t v = spi->cs;
    if ((v & BCM2835_SPI0_CS_TXD)) spi->fifo = *tStart++;
    if ((v & BCM2835_SPI0_CS_RXD)) (void)spi->fifo;
  }
}

SPITask tasks[SPI_QUEUE_LENGTH];
volatile uint32_t queueHead = 0, queueTail = 0;
volatile uint32_t spiBytesQueued = 0;
volatile uint64_t spiThreadIdleUsecs = 0;
volatile uint64_t spiThreadSleepStartTime = 0;
volatile int spiThreadSleeping = 0;
double spiUsecsPerByte;

SPITask *GetTask() // Returns the first task in the queue, called in worker thread
{
  return (queueHead != queueTail) ? tasks+queueHead : 0;
}

void DoneTask() // Frees the first SPI task from the queue, called in worker thread
{
  __atomic_fetch_sub(&spiBytesQueued, tasks[queueHead].bytes, __ATOMIC_RELAXED);
  queueHead = (queueHead + 1) % SPI_QUEUE_LENGTH;
  __sync_synchronize();
}

// A worker thread that keeps the SPI bus filled at all times
void *spi_thread(void*)
{
  for(;;)
  {
    if (queueTail != queueHead)
    {
      BEGIN_SPI_COMMUNICATION();
      {
        while(queueTail != queueHead)
        {
          RunSPITask(GetTask());
          DoneTask();
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
      syscall(SYS_futex, &queueTail, FUTEX_WAIT, queueHead, 0, 0, 0); // Start sleeping until we get new tasks
#ifdef STATISTICS
      __atomic_store_n(&spiThreadSleeping, 0, __ATOMIC_RELAXED);
      uint64_t t1 = tick();
      __sync_fetch_and_add(&spiThreadIdleUsecs, t1-t0);
#endif
    }
  }
}

void InitSPI()
{
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

  // By default all GPIO pins are in input mode (0x00), initialize them for SPI and GPIO writes
  SET_GPIO_MODE(GPIO_TFT_DATA_CONTROL, 0x01); // Data/Control pin to output (0x01)
  SET_GPIO_MODE(GPIO_SPI0_CE1, 0x04); // Set the SPI0 pins to the Alt 0 function (0x04) to enable SPI0 access on them
  SET_GPIO_MODE(GPIO_SPI0_CE0, 0x04);
  SET_GPIO_MODE(GPIO_SPI0_MISO, 0x04);
  SET_GPIO_MODE(GPIO_SPI0_MOSI, 0x04);
  SET_GPIO_MODE(GPIO_SPI0_CLK, 0x04);

  spi->cs = BCM2835_SPI0_CS_CLEAR; // Initialize the Control and Status register to defaults: CS=0 (Chip Select), CPHA=0 (Clock Phase), CPOL=0 (Clock Polarity), CSPOL=0 (Chip Select Polarity), TA=0 (Transfer not active), and reset TX and RX queues.
  spi->clk = SPI_BUS_CLOCK_DIVISOR; // Clock Divider determines SPI bus speed, resulting speed=256MHz/clk

  // Estimate how many microseconds transferring a single byte over the SPI bus takes?
  spiUsecsPerByte = 8.0/*bits/byte*/ * SPI_BUS_CLOCK_DIVISOR * 9.0/8.0/*BCM2835 SPI master idles for one bit per each byte*/ / 400/*Approx BCM2835 SPI clock (250MHz is lowest, turbo is at 400MHz)*/;

  InitSPIDisplay();

  // Create a dedicated thread to feed the SPI bus. While this is fast, it consumes a lot of CPU. It would be best to replace
  // this thread with a kernel module that processes the created SPI task queue using interrupts. (while juggling the GPIO D/C line as well)
  pthread_t thread;
  int rc = pthread_create(&thread, NULL, spi_thread, NULL); // After creating the thread, it is assumed to have ownership of the SPI bus, so no SPI chat on the main thread after this.
  if (rc != 0) FATAL_ERROR("Failed to create SPI thread!");
}

void DeinitSPI()
{
  SET_GPIO_MODE(GPIO_TFT_DATA_CONTROL, 0);
  SET_GPIO_MODE(GPIO_SPI0_CE1, 0);
  SET_GPIO_MODE(GPIO_SPI0_CE0, 0);
  SET_GPIO_MODE(GPIO_SPI0_MISO, 0);
  SET_GPIO_MODE(GPIO_SPI0_MOSI, 0);
  SET_GPIO_MODE(GPIO_SPI0_CLK, 0);  
}
