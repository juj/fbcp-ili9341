#pragma once

#include <stdint.h>
#include <linux/futex.h> // FUTEX_WAKE
#include <sys/syscall.h>
#include "dma.h"
#include "spi.h"
#include "display.h"
#include "tick.h"
#include "display.h"

// Sends no-Op command wich enables sensing of touch screen on some displays
extern void sendNoOpCommand();

// Checks last time touch screen sensed
extern bool activeTouchscreen();

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
#define SHARED_MEMORY_SIZE (DISPLAY_DRAWABLE_WIDTH*DISPLAY_DRAWABLE_HEIGHT*SPI_BYTESPERPIXEL*3)
#define SPI_QUEUE_SIZE (SHARED_MEMORY_SIZE - sizeof(SharedMemory))

#if defined(SPI_3WIRE_DATA_COMMAND_FRAMING_BITS) && SPI_3WIRE_DATA_COMMAND_FRAMING_BITS == 1
// Need a byte of padding for 8-bit -> 9-bit expansion for performance
#define SPI_9BIT_TASK_PADDING_BYTES 1
#else
#define SPI_9BIT_TASK_PADDING_BYTES 0
#endif

// Defines the maximum size of a single SPI task, in bytes. This excludes the command byte. If MAX_SPI_TASK_SIZE
// is not defined, there is no length limit that applies. (In ALL_TASKS_SHOULD_DMA version of DMA transfer,
// there is DMA chaining, so SPI tasks can be arbitrarily long)
#ifndef ALL_TASKS_SHOULD_DMA
#define MAX_SPI_TASK_SIZE 65528
#endif

typedef struct __attribute__((packed)) SPITask
{
  uint32_t size; // Size, including both 8-bit and 9-bit tasks
#ifdef SPI_3WIRE_PROTOCOL
  uint32_t sizeExpandedTaskWithPadding; // Size of the expanded 9-bit/32-bit task. The expanded task starts at address spiTask->data + spiTask->size - spiTask->sizeExpandedTaskWithPadding;
#endif
#ifdef SPI_32BIT_COMMANDS
  uint32_t cmd;
#else
  uint8_t cmd;
#endif
  uint32_t dmaSpiHeader;
#ifdef OFFLOAD_PIXEL_COPY_TO_DMA_CPP
  uint8_t *fb;
  uint8_t *prevFb;
  uint16_t width;
#endif
  uint8_t data[]; // Contains both 8-bit and 9-bit tasks back to back, 8-bit first, then 9-bit.

#ifdef SPI_3WIRE_PROTOCOL
  inline uint8_t *PayloadStart() { return data + (size - sizeExpandedTaskWithPadding); }
  inline uint8_t *PayloadEnd() { return data + (size - SPI_9BIT_TASK_PADDING_BYTES); }
  inline uint32_t PayloadSize() const { return sizeExpandedTaskWithPadding - SPI_9BIT_TASK_PADDING_BYTES; }
  inline uint32_t *DmaSpiHeaderAddress() { return (uint32_t*)(PayloadStart()-4); }
#else
  inline uint8_t *PayloadStart() { return data; }
  inline uint8_t *PayloadEnd() { return data + size; }
  inline uint32_t PayloadSize() const { return size; }
  inline uint32_t *DmaSpiHeaderAddress() { return &dmaSpiHeader; }
#endif

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

#define WAIT_SPI_FINISHED()  do { \
    uint32_t cs; \
    while (!((cs = spi->cs) & BCM2835_SPI0_CS_DONE)) /* While DONE=0*/ \
    { \
      if ((cs & (BCM2835_SPI0_CS_RXR | BCM2835_SPI0_CS_RXF))) \
        spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA | DISPLAY_SPI_DRIVE_SETTINGS; \
    } \
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

#ifdef STATISTICS
extern volatile uint64_t spiThreadIdleUsecs;
extern volatile uint64_t spiThreadSleepStartTime;
extern volatile int spiThreadSleeping;
#endif

#ifdef SPI_3WIRE_PROTOCOL

// Converts the given SPI task in-place from an 8-bit task to a 9-bit task.
void Interleave8BitSPITaskTo9Bit(SPITask *task);

// Converts the given SPI task in-place from a 16-bit task to a 32-bit task.
void Interleave16BitSPITaskTo32Bit(SPITask *task);

// If the given display is a 3-wire SPI display (9 bits/task instead of 8 bits/task), this function computes the byte size of the 8-bit task when it is converted to a 9-bit task.
uint32_t NumBytesNeededFor9BitSPITask(uint32_t byteSizeFor8BitTask);

// If the given display is a 3-wire SPI display with 32 bits bus width, this function computes the byte size of the task when it is converted to a 32-bit task.
uint32_t NumBytesNeededFor32BitSPITask(uint32_t byteSizeFor8BitTask);

#endif

static inline SPITask *AllocTask(uint32_t bytes) // Returns a pointer to a new SPI task block, called on main thread
{
#ifdef SPI_3WIRE_PROTOCOL
  // For 3-wire/9-bit tasks, store the converted task right at the end of the 8-bit task.
#ifdef SPI_32BIT_COMMANDS
  uint32_t sizeExpandedTaskWithPadding = NumBytesNeededFor32BitSPITask(bytes) + SPI_9BIT_TASK_PADDING_BYTES;
#else
  uint32_t sizeExpandedTaskWithPadding = NumBytesNeededFor9BitSPITask(bytes) + SPI_9BIT_TASK_PADDING_BYTES;
#endif
  bytes += sizeExpandedTaskWithPadding;
#else
//  const uint32_t totalBytesFor9BitTask = 0;
#endif

  uint32_t bytesToAllocate = sizeof(SPITask) + bytes;// + totalBytesFor9BitTask;
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
      head = spiTaskMemory->queueHead;
    }
    SPITask *endOfBuffer = (SPITask*)(spiTaskMemory->buffer + tail);
    endOfBuffer->cmd = 0; // Use cmd=0x00 to denote "end of buffer, wrap to beginning"
    __sync_synchronize();
    spiTaskMemory->queueTail = 0;
    __sync_synchronize();
    if (spiTaskMemory->queueHead == tail) syscall(SYS_futex, &spiTaskMemory->queueTail, FUTEX_WAKE, 1, 0, 0, 0); // Wake the SPI thread if it was sleeping to get new tasks
    tail = 0;
    newTail = bytesToAllocate;
  }

  // If the SPI task queue is full, wait for the SPI thread to process some tasks. This throttles the main thread to not run too fast.
  uint32_t head = spiTaskMemory->queueHead;
  while(head > tail && head <= newTail)
  {
    usleep(100); // Since the SPI queue is full, we can afford to sleep a bit on the main thread without introducing lag.
    head = spiTaskMemory->queueHead;
  }

  SPITask *task = (SPITask*)(spiTaskMemory->buffer + tail);
  task->size = bytes;
#ifdef SPI_3WIRE_PROTOCOL
  task->sizeExpandedTaskWithPadding = sizeExpandedTaskWithPadding;
#endif
#ifdef OFFLOAD_PIXEL_COPY_TO_DMA_CPP
  task->fb = &task->data[0];
  task->prevFb = 0;
#endif
  return task;
}

static inline void CommitTask(SPITask *task) // Advertises the given SPI task from main thread to worker, called on main thread
{
#ifdef SPI_3WIRE_PROTOCOL
#ifdef SPI_32BIT_COMMANDS
  Interleave16BitSPITaskTo32Bit(task);
#else
  Interleave8BitSPITaskTo9Bit(task);
#endif
#endif
  __sync_synchronize();
  uint32_t tail = spiTaskMemory->queueTail;
  spiTaskMemory->queueTail = (uint32_t)((uint8_t*)task - spiTaskMemory->buffer) + sizeof(SPITask) + task->size;
  __atomic_fetch_add(&spiTaskMemory->spiBytesQueued, task->PayloadSize()+1, __ATOMIC_RELAXED);
  __sync_synchronize();
  if (spiTaskMemory->queueHead == tail) syscall(SYS_futex, &spiTaskMemory->queueTail, FUTEX_WAKE, 1, 0, 0, 0); // Wake the SPI thread if it was sleeping to get new tasks
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
bool hasInterrupt();
void DeinitSPI(void);
void ExecuteSPITasks(void);
void RunSPITask(SPITask *task);
SPITask *GetTask(void);
void DoneTask(SPITask *task);
void DumpSPICS(uint32_t reg);
#ifdef RUN_WITH_REALTIME_THREAD_PRIORITY
void SetRealtimeThreadPriority();
#endif
