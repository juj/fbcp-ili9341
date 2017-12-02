#pragma once

#include <inttypes.h>
#include <linux/futex.h>
#include <sys/syscall.h>

#include "display.h"
#include "tick.h"

#define BCM2835_GPIO_BASE               0x200000
#define BCM2835_SPI0_BASE               0x204000

#define BCM2835_SPI0_CS_CLEAR                0x00000030 /*!< Clear FIFO Clear RX and TX */
#define BCM2835_SPI0_CS_CLEAR_TX             0x00000010 /*!< Clear FIFO Clear TX */
#define BCM2835_SPI0_CS_CLEAR_RX             0x00000020 /*!< Clear FIFO Clear RX */
#define BCM2835_SPI0_CS_TA                   0x00000080 /*!< Transfer Active */
#define BCM2835_SPI0_CS_RXF                  0x00100000 /*!< Receive FIFO is full */
#define BCM2835_SPI0_CS_RXR                  0x00080000 /*!< FIFO needs reading */
#define BCM2835_SPI0_CS_TXD                  0x00040000 /*!< TXD TX FIFO can accept Data */
#define BCM2835_SPI0_CS_RXD                  0x00020000 /*!< RXD RX FIFO contains Data */
#define BCM2835_SPI0_CS_DONE                 0x00010000 /*!< Done transfer Done */

#define BCM2835_SPI0_CS_CPOL                 0x00000008 /*!< Clock Polarity */
#define BCM2835_SPI0_CS_CPHA                 0x00000004 /*!< Clock Phase */
#define BCM2835_SPI0_CS_CS                   0x00000003 /*!< Chip Select */

#define GPIO_SPI0_MOSI  10        /*!< Version 1, Pin P1-19, MOSI when SPI0 in use */
#define GPIO_SPI0_MISO   9        /*!< Version 1, Pin P1-21, MISO when SPI0 in use */
#define GPIO_SPI0_CLK   11        /*!< Version 1, Pin P1-23, CLK when SPI0 in use */
#define GPIO_SPI0_CE0    8        /*!< Version 1, Pin P1-24, CE0 when SPI0 in use */
#define GPIO_SPI0_CE1    7        /*!< Version 1, Pin P1-26, CE1 when SPI0 in use */

struct GPIORegisterFile
{
  uint32_t gpfsel[6], reserved0; // GPIO Function Select registers, 3 bits per pin, 10 pins in an uint32_t
  uint32_t gpset[2], reserved1; // GPIO Pin Output Set registers, write a 1 to bit at index I to set the pin at index I high
  uint32_t gpclr[2]; // GPIO Pin Output Clear registers, write a 1 to bit at index I to set the pin at index I low
};
extern volatile GPIORegisterFile *gpio;

#define SET_GPIO_MODE(pin, mode) gpio->gpfsel[(pin)/10] = (gpio->gpfsel[(pin)/10] & ~(0x7 << ((pin) % 10) * 3)) | ((mode) << ((pin) % 10) * 3)
#define SET_GPIO(pin) gpio->gpset[0] = 1 << (pin) // Pin must be (0-31)
#define CLEAR_GPIO(pin) gpio->gpclr[0] = 1 << (pin) // Pin must be (0-31)

struct SPIRegisterFile
{
  uint32_t cs;   // SPI Master Control and Status register
  uint32_t fifo; // SPI Master TX and RX FIFOs
  uint32_t clk;  // SPI Master Clock Divider
};
extern volatile SPIRegisterFile *spi;

// Defines the maximum size of a single SPI task, in bytes. This excludes the command byte. The relationship
// MAX_SPI_TASK_SIZE <= SHARED_MEMORY_SIZE/4 should hold, so that there is no danger of deadlocking the ring buffer, which has
// been implemented with the assumption that an individual task in the buffer is considerably smaller than the size of the ring
// buffer itself. Also, MAX_SPI_TASK_SIZE >= SCANLINE_SIZE should hold, scanline merging assumes that it can always fit one full
// scanline bytes of data in one task.
#define MAX_SPI_TASK_SIZE (SCANLINE_SIZE*16)

// Defines the size of the SPI task memory buffer in bytes. This memory buffer can contain two frames worth of tasks at maximum,
// so for best performance, should be at least ~DISPLAY_WIDTH*DISPLAY_HEIGHT*BYTES_PER_PIXEL*2 bytes in size, plus some small
// amount for structuring each SPITask command. Technically this can be something very small, like 4096b, and not need to contain
// even a single full frame of data, but such small buffers can cause performance issues from threads starving.
#define SHARED_MEMORY_SIZE (DISPLAY_WIDTH*DISPLAY_HEIGHT*DISPLAY_BYTESPERPIXEL*5/2)
#define SPI_QUEUE_SIZE (SHARED_MEMORY_SIZE - sizeof(SharedMemory))

typedef struct __attribute__((packed)) SPITask
{
  uint32_t size;
  uint8_t cmd;
  uint8_t data[];
} SPITask;

#define BEGIN_SPI_COMMUNICATION() do { spi->cs |= BCM2835_SPI0_CS_CLEAR | BCM2835_SPI0_CS_TA; __sync_synchronize(); } while(0)
#define END_SPI_COMMUNICATION()  do { \
    while ((spi->cs & BCM2835_SPI0_CS_RXD)) (void)spi->fifo; \
    while (!(spi->cs & BCM2835_SPI0_CS_DONE)) ; \
    spi->cs &= ~BCM2835_SPI0_CS_TA; \
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

#define QUEUE_MOVE_CURSOR_TASK(cursor, pos) do { \
    SPITask *task = AllocTask(2); \
    task->cmd = (cursor); \
    task->data[0] = (pos) >> 8; \
    task->data[1] = (pos) & 0xFF; \
    bytesTransferred += 3; \
    CommitTask(task); \
  } while(0)

#define QUEUE_SET_X_WINDOW_TASK(x, endX) do { \
    SPITask *task = AllocTask(4); \
    task->cmd = DISPLAY_SET_CURSOR_X; \
    task->data[0] = (x) >> 8; \
    task->data[1] = (x) & 0xFF; \
    task->data[2] = (endX) >> 8; \
    task->data[3] = (endX) & 0xFF; \
    bytesTransferred += 5; \
    CommitTask(task); \
  } while(0)

typedef struct SharedMemory
{
  volatile uint32_t queueHead;
  volatile uint32_t queueTail;
  volatile uint8_t buffer[];
} SharedMemory;

extern SharedMemory *spiTaskMemory;
extern volatile uint32_t spiBytesQueued;
extern double spiUsecsPerByte;

#ifdef STATISTICS
extern volatile uint64_t spiThreadIdleUsecs;
extern volatile uint64_t spiThreadSleepStartTime;
extern volatile int spiThreadSleeping;
#endif

static inline SPITask *AllocTask(uint32_t bytes) // Returns a pointer to a new SPI task block, called on main thread
{
  uint32_t bytesToAllocate = sizeof(SPITask) + bytes;
  uint32_t tail = spiTaskMemory->queueTail;
  uint32_t newTail = tail + bytesToAllocate;
  if (spiTaskMemory->queueTail + bytesToAllocate + sizeof(SPITask)/*Add extra SPITask size so that there will always be room for eob marker*/ >= SPI_QUEUE_SIZE)
  {
    uint32_t head = spiTaskMemory->queueHead;
    while(head > tail || head == 0/*Head must move > 0 so that we don't stomp on it*/)
    {
      usleep(100); // Wait until there are no remaining bytes to process in the far right end of the buffer - we'll write an eob marker there as soon as the read pointer has cleared it.
      head = spiTaskMemory->queueHead;
    }
    SPITask *endOfBuffer = (SPITask*)(spiTaskMemory->buffer + tail);
    endOfBuffer->cmd = 0; // Use cmd=0x00 to denote "end of buffer, wrap to beginning"
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
    usleep(100);
    head = spiTaskMemory->queueHead;
  }

  SPITask *task = (SPITask*)(spiTaskMemory->buffer + tail);
  task->size = bytes;
  return task;
}

static inline void CommitTask(SPITask *task) // Advertises the given SPI task from main thread to worker, called on main thread
{
  uint32_t head = spiTaskMemory->queueHead;
  __sync_synchronize();
  uint32_t tail = spiTaskMemory->queueTail;
  spiTaskMemory->queueTail = (uint32_t)((uint8_t*)task - spiTaskMemory->buffer) + sizeof(SPITask) + task->size;
  __atomic_fetch_add(&spiBytesQueued, task->size+1, __ATOMIC_RELAXED);
  __sync_synchronize();
  if (spiTaskMemory->queueHead == tail) syscall(SYS_futex, &spiTaskMemory->queueTail, FUTEX_WAKE, 1, 0, 0, 0); // Wake the SPI thread if it was sleeping to get new tasks
}

void InitSPI();
void DeinitSPI();
void RunSPITask(SPITask *task);
void DoneTask(SPITask *task);
