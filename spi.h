#pragma once

#ifndef KERNEL_MODULE
#include <inttypes.h>
#include <sys/syscall.h>
#endif
#include <linux/futex.h>

#include "display.h"
#include "tick.h"
#include "dma.h"

#define BCM2835_GPIO_BASE                    0x200000   // Address to GPIO register file
#define BCM2835_SPI0_BASE                    0x204000   // Address to SPI0 register file
#define BCM2835_TIMER_BASE                   0x3000     // Address to System Timer register file

#define BCM2835_SPI0_CS_RXF                  0x00100000 // Receive FIFO is full
#define BCM2835_SPI0_CS_RXR                  0x00080000 // FIFO needs reading
#define BCM2835_SPI0_CS_TXD                  0x00040000 // TXD TX FIFO can accept Data
#define BCM2835_SPI0_CS_RXD                  0x00020000 // RXD RX FIFO contains Data
#define BCM2835_SPI0_CS_DONE                 0x00010000 // Done transfer Done
#define BCM2835_SPI0_CS_ADCS                 0x00000800 // Automatically Deassert Chip Select
#define BCM2835_SPI0_CS_INTR                 0x00000400 // Fire interrupts on RXR?
#define BCM2835_SPI0_CS_INTD                 0x00000200 // Fire interrupts on DONE?
#define BCM2835_SPI0_CS_DMAEN                0x00000100 // Enable DMA transfers?
#define BCM2835_SPI0_CS_TA                   0x00000080 // Transfer Active
#define BCM2835_SPI0_CS_CLEAR                0x00000030 // Clear FIFO Clear RX and TX
#define BCM2835_SPI0_CS_CLEAR_RX             0x00000020 // Clear FIFO Clear RX
#define BCM2835_SPI0_CS_CLEAR_TX             0x00000010 // Clear FIFO Clear TX
#define BCM2835_SPI0_CS_CPOL                 0x00000008 // Clock Polarity
#define BCM2835_SPI0_CS_CPHA                 0x00000004 // Clock Phase
#define BCM2835_SPI0_CS_CS                   0x00000003 // Chip Select

#define BCM2835_SPI0_CS_RXF_SHIFT                  20
#define BCM2835_SPI0_CS_RXR_SHIFT                  19
#define BCM2835_SPI0_CS_TXD_SHIFT                  18
#define BCM2835_SPI0_CS_RXD_SHIFT                  17
#define BCM2835_SPI0_CS_DONE_SHIFT                 16
#define BCM2835_SPI0_CS_ADCS_SHIFT                 11
#define BCM2835_SPI0_CS_INTR_SHIFT                 10
#define BCM2835_SPI0_CS_INTD_SHIFT                 9
#define BCM2835_SPI0_CS_DMAEN_SHIFT                8
#define BCM2835_SPI0_CS_TA_SHIFT                   7
#define BCM2835_SPI0_CS_CLEAR_RX_SHIFT             5
#define BCM2835_SPI0_CS_CLEAR_TX_SHIFT             4
#define BCM2835_SPI0_CS_CPOL_SHIFT                 3
#define BCM2835_SPI0_CS_CPHA_SHIFT                 2
#define BCM2835_SPI0_CS_CS_SHIFT                   0

#define GPIO_SPI0_MOSI  10        // Pin P1-19, MOSI when SPI0 in use
#define GPIO_SPI0_MISO   9        // Pin P1-21, MISO when SPI0 in use
#define GPIO_SPI0_CLK   11        // Pin P1-23, CLK when SPI0 in use
#define GPIO_SPI0_CE0    8        // Pin P1-24, CE0 when SPI0 in use
#define GPIO_SPI0_CE1    7        // Pin P1-26, CE1 when SPI0 in use

extern volatile void *bcm2835;

typedef struct GPIORegisterFile
{
  uint32_t gpfsel[6], reserved0; // GPIO Function Select registers, 3 bits per pin, 10 pins in an uint32_t
  uint32_t gpset[2], reserved1; // GPIO Pin Output Set registers, write a 1 to bit at index I to set the pin at index I high
  uint32_t gpclr[2], reserved2; // GPIO Pin Output Clear registers, write a 1 to bit at index I to set the pin at index I low
  uint32_t gplev[2];
} GPIORegisterFile;
extern volatile GPIORegisterFile *gpio;

#define SET_GPIO_MODE(pin, mode) gpio->gpfsel[(pin)/10] = (gpio->gpfsel[(pin)/10] & ~(0x7 << ((pin) % 10) * 3)) | ((mode) << ((pin) % 10) * 3)
#define GET_GPIO_MODE(pin) ((gpio->gpfsel[(pin)/10] & (0x7 << ((pin) % 10) * 3)) >> (((pin) % 10) * 3))
#define GET_GPIO(pin) (gpio->gplev[0] & (1 << (pin))) // Pin must be (0-31)
#define SET_GPIO(pin) gpio->gpset[0] = 1 << (pin) // Pin must be (0-31)
#define CLEAR_GPIO(pin) gpio->gpclr[0] = 1 << (pin) // Pin must be (0-31)

typedef struct SPIRegisterFile
{
  uint32_t cs;   // SPI Master Control and Status register
  uint32_t fifo; // SPI Master TX and RX FIFOs
  uint32_t clk;  // SPI Master Clock Divider
  uint32_t dlen; // SPI Master Number of DMA Bytes to Write
} SPIRegisterFile;
extern volatile SPIRegisterFile *spi;

// Defines the size of the SPI task memory buffer in bytes. This memory buffer can contain two frames worth of tasks at maximum,
// so for best performance, should be at least ~DISPLAY_WIDTH*DISPLAY_HEIGHT*BYTES_PER_PIXEL*2 bytes in size, plus some small
// amount for structuring each SPITask command. Technically this can be something very small, like 4096b, and not need to contain
// even a single full frame of data, but such small buffers can cause performance issues from threads starving.
#define SHARED_MEMORY_SIZE (DISPLAY_DRAWABLE_WIDTH*DISPLAY_DRAWABLE_HEIGHT*DISPLAY_BYTESPERPIXEL*3)
#define SPI_QUEUE_SIZE (SHARED_MEMORY_SIZE - sizeof(SharedMemory))

// Defines the maximum size of a single SPI task, in bytes. This excludes the command byte. If MAX_SPI_TASK_SIZE
// is not defined, there is no length limit that applies. (In ALL_TASKS_SHOULD_DMA version of DMA transfer,
// there is DMA chaining, so SPI tasks can be arbitrarily long)
#ifndef ALL_TASKS_SHOULD_DMA
#define MAX_SPI_TASK_SIZE 65528
#endif

typedef struct __attribute__((packed)) SPITask
{
  uint32_t size;
  uint8_t cmd;
  uint32_t dmaSpiHeader;
  uint8_t data[];
} SPITask;

#define BEGIN_SPI_COMMUNICATION() do { spi->cs = BCM2835_SPI0_CS_TA | DISPLAY_SPI_DRIVE_SETTINGS; } while(0)
#define END_SPI_COMMUNICATION()  do { \
    uint32_t cs; \
    while (!(((cs = spi->cs) ^ BCM2835_SPI0_CS_TA) & (BCM2835_SPI0_CS_DONE | BCM2835_SPI0_CS_TA))) /* While TA=1 and DONE=0*/ \
    { \
      if ((cs & (BCM2835_SPI0_CS_RXR | BCM2835_SPI0_CS_RXF))) \
        spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA | DISPLAY_SPI_DRIVE_SETTINGS; \
    } \
    spi->cs = BCM2835_SPI0_CS_CLEAR_RX | DISPLAY_SPI_DRIVE_SETTINGS; /* Clear TA and any pending bytes */ \
  } while(0)

// A convenience for defining and dispatching SPI task bytes inline
#define SPI_TRANSFER(command, ...) do { \
    char data_buffer[] = { __VA_ARGS__ }; \
    SPITask *t = AllocTask(sizeof(data_buffer)); \
    t->cmd = (command); \
    memcpy(t->data, data_buffer, sizeof(data_buffer)); \
    CommitTask(t); \
    RunSPITask(t); \
    DoneTask(t); \
  } while(0)

#define QUEUE_SPI_TRANSFER(command, ...) do { \
    char data_buffer[] = { __VA_ARGS__ }; \
    SPITask *t = AllocTask(sizeof(data_buffer)); \
    t->cmd = (command); \
    memcpy(t->data, data_buffer, sizeof(data_buffer)); \
    CommitTask(t); \
  } while(0)

#ifdef DISPLAY_SPI_BUS_IS_16BITS_WIDE // For displays that have their command register set be 16 bits word size width (ILI9486)

#define QUEUE_MOVE_CURSOR_TASK(cursor, pos) do { \
    SPITask *task = AllocTask(4); \
    task->cmd = (cursor); \
    task->data[0] = 0; \
    task->data[1] = (pos) >> 8; \
    task->data[2] = 0; \
    task->data[3] = (pos) & 0xFF; \
    bytesTransferred += 6; \
    CommitTask(task); \
  } while(0)

#define QUEUE_SET_WRITE_WINDOW_TASK(cursor, x, endX) do { \
    SPITask *task = AllocTask(8); \
    task->cmd = (cursor); \
    task->data[0] = 0; \
    task->data[1] = (x) >> 8; \
    task->data[2] = 0; \
    task->data[3] = (x) & 0xFF; \
    task->data[4] = 0; \
    task->data[5] = (endX) >> 8; \
    task->data[6] = 0; \
    task->data[7] = (endX) & 0xFF; \
    bytesTransferred += 10; \
    CommitTask(task); \
  } while(0)

#elif defined(DISPLAY_SET_CURSOR_IS_8_BIT) // For displays that have their set cursor commands be a uint8 instead of uint16 (SSD1351)

#define QUEUE_SET_WRITE_WINDOW_TASK(cursor, x, endX) do { \
    SPITask *task = AllocTask(2); \
    task->cmd = (cursor); \
    task->data[0] = (x); \
    task->data[1] = (endX); \
    bytesTransferred += 3; \
    CommitTask(task); \
  } while(0)

#else // Regular 8-bit interface with 16bits wide set cursor commands (most displays)

#define QUEUE_MOVE_CURSOR_TASK(cursor, pos) do { \
    SPITask *task = AllocTask(2); \
    task->cmd = (cursor); \
    task->data[0] = (pos) >> 8; \
    task->data[1] = (pos) & 0xFF; \
    bytesTransferred += 3; \
    CommitTask(task); \
  } while(0)

#define QUEUE_SET_WRITE_WINDOW_TASK(cursor, x, endX) do { \
    SPITask *task = AllocTask(4); \
    task->cmd = (cursor); \
    task->data[0] = (x) >> 8; \
    task->data[1] = (x) & 0xFF; \
    task->data[2] = (endX) >> 8; \
    task->data[3] = (endX) & 0xFF; \
    bytesTransferred += 5; \
    CommitTask(task); \
  } while(0)
#endif

typedef struct SharedMemory
{
/*
#ifdef USE_DMA_TRANSFERS
  volatile DMAControlBlock cb[2];
  volatile uint32_t dummyDMADestinationWriteAddress;
  volatile uint32_t dmaTxChannel, dmaRxChannel;
#endif
*/
  volatile uint32_t queueHead;
  volatile uint32_t queueTail;
  volatile uint32_t spiBytesQueued; // Number of actual payload bytes in the queue
  volatile uint32_t interruptsRaised;
  //volatile uintptr_t sharedMemoryBaseInPhysMemory;
//  volatile uint8_t buffer[];
} SharedMemory;

extern GpuMemory spiMem;

#ifdef KERNEL_MODULE
extern dma_addr_t spiTaskMemoryPhysical;
#define VIRT_TO_BUS(ptr) ((uintptr_t)(ptr) | 0xC0000000U)
#endif
extern SharedMemory *spiTaskMemory;
extern double spiUsecsPerByte;

extern SharedMemory *dmaSourceMemory; // TODO: Optimize away the need to have this at all, instead DMA directly from SPI ring buffer if possible

#ifdef STATISTICS
extern volatile uint64_t spiThreadIdleUsecs;
extern volatile uint64_t spiThreadSleepStartTime;
extern volatile int spiThreadSleeping;
#endif

extern int mem_fd;

static inline SPITask *AllocTask(uint32_t bytes) // Returns a pointer to a new SPI task block, called on main thread
{
  uint32_t bytesToAllocate = sizeof(SPITask) + bytes;
  uint32_t tail = spiTaskMemory->queueTail;
  uint32_t newTail = tail + bytesToAllocate;
  // Is the new task too large to write contiguously into the ring buffer, that it's split into two parts? We never split,
  // but instead write a sentinel at the end of the ring buffer, and jump the tail back to the beginning of the buffer and
  // allocate the new task there. However in doing so, we must make sure that we don't write over the head marker.
  if (newTail + sizeof(SPITask)/*Add extra SPITask size so that there will always be room for eob marker*/ >= SPI_QUEUE_SIZE)
  {
    uint32_t head = spiTaskMemory->queueHead;
    // Write a sentinel, but wait for the head to advance first so that it is safe to write.
    while(head > tail || head == 0/*Head must move > 0 so that we don't stomp on it*/)
    {
#if defined(KERNEL_MODULE_CLIENT) && !defined(KERNEL_MODULE)
      // Hack: Pump the kernel module to start transferring in case it has stopped. TODO: Remove this line:
      if (!(spi->cs & BCM2835_SPI0_CS_TA)) spi->cs |= BCM2835_SPI0_CS_TA;
      // Wait until there are no remaining bytes to process in the far right end of the buffer - we'll write an eob marker there as soon as the read pointer has cleared it.
      // At this point the SPI queue may actually be quite empty, so don't sleep (except for now in kernel client app)
      usleep(100);
#endif
      head = spiTaskMemory->queueHead;
    }
    SPITask *endOfBuffer = (SPITask*)((uintptr_t)spiMem.virtualAddr + tail);
    endOfBuffer->cmd = 0; // Use cmd=0x00 to denote "end of buffer, wrap to beginning"
    __sync_synchronize();
    spiTaskMemory->queueTail = 0;
    __sync_synchronize();
#if !defined(KERNEL_MODULE_CLIENT) && !defined(KERNEL_MODULE)
    if (spiTaskMemory->queueHead == tail) syscall(SYS_futex, &spiTaskMemory->queueTail, FUTEX_WAKE, 1, 0, 0, 0); // Wake the SPI thread if it was sleeping to get new tasks
#endif
    tail = 0;
    newTail = bytesToAllocate;
  }

  // If the SPI task queue is full, wait for the SPI thread to process some tasks. This throttles the main thread to not run too fast.
  uint32_t head = spiTaskMemory->queueHead;
  while(head > tail && head <= newTail)
  {
#if defined(KERNEL_MODULE_CLIENT) && !defined(KERNEL_MODULE)
      // Hack: Pump the kernel module to start transferring in case it has stopped. TODO: Remove this line:
    if (!(spi->cs & BCM2835_SPI0_CS_TA)) spi->cs |= BCM2835_SPI0_CS_TA;
#endif
    usleep(100); // Since the SPI queue is full, we can afford to sleep a bit on the main thread without introducing lag.
    head = spiTaskMemory->queueHead;
  }

  SPITask *task = (SPITask*)((uintptr_t)spiMem.virtualAddr + tail);
  task->size = bytes;
  return task;
}

static inline void CommitTask(SPITask *task) // Advertises the given SPI task from main thread to worker, called on main thread
{
  __sync_synchronize();
#if !defined(KERNEL_MODULE_CLIENT) && !defined(KERNEL_MODULE)
  uint32_t tail = spiTaskMemory->queueTail;
#endif
  spiTaskMemory->queueTail = (uint32_t)((uint8_t*)task - (uintptr_t)spiMem.virtualAddr) + sizeof(SPITask) + task->size;
  __atomic_fetch_add(&spiTaskMemory->spiBytesQueued, task->size+1, __ATOMIC_RELAXED);
  __sync_synchronize();
#if !defined(KERNEL_MODULE_CLIENT) && !defined(KERNEL_MODULE)
  if (spiTaskMemory->queueHead == tail) syscall(SYS_futex, &spiTaskMemory->queueTail, FUTEX_WAKE, 1, 0, 0, 0); // Wake the SPI thread if it was sleeping to get new tasks
#endif
}

#ifdef USE_SPI_THREAD
#define IN_SINGLE_THREADED_MODE_RUN_TASK() ((void)0)
#else
#define IN_SINGLE_THREADED_MODE_RUN_TASK() { \
  SPITask *t = GetTask(); \
  RunSPITask(t); \
  DoneTask(t); \
}
#endif

int InitSPI(void);
void DeinitSPI(void);
void ExecuteSPITasks(void);
void RunSPITask(SPITask *task);
SPITask *GetTask(void);
void DoneTask(SPITask *task);
void DumpSPICS(uint32_t reg);
#ifdef RUN_WITH_REALTIME_THREAD_PRIORITY
void SetRealtimeThreadPriority();
#endif
