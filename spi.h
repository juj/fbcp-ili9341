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

#define MAX_SPI_TASK_SIZE SCANLINE_SIZE

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

struct SPITask
{
  uint8_t cmd;
  uint8_t data[MAX_SPI_TASK_SIZE];
  uint16_t bytes;
};

#define BEGIN_SPI_COMMUNICATION() do { spi->cs |= BCM2835_SPI0_CS_CLEAR | BCM2835_SPI0_CS_TA; __sync_synchronize(); } while(0)
#define END_SPI_COMMUNICATION()  do { \
    while ((spi->cs & BCM2835_SPI0_CS_RXD)) (void)spi->fifo; \
    while (!(spi->cs & BCM2835_SPI0_CS_DONE)) ; \
    spi->cs &= ~BCM2835_SPI0_CS_TA; \
  } while(0)

// A convenience for defining and dispatching SPI task bytes inline
#define SPI_TRANSFER(...) do { \
    char data_buffer[] = { __VA_ARGS__ }; \
    SPITask t; \
    t.cmd = data_buffer[0]; \
    memcpy(t.data, data_buffer+1, sizeof(data_buffer)-1); \
    t.bytes = sizeof(data_buffer)-1; \
    RunSPITask(&t); \
  } while(0)

#define QUEUE_MOVE_CURSOR_TASK(cursor, pos) do { \
    SPITask *task = AllocTask(); \
    task->cmd = (cursor); \
    task->data[0] = (pos) >> 8; \
    task->data[1] = (pos) & 0xFF; \
    task->bytes = 2; \
    bytesTransferred += 3; \
    CommitTask(); \
  } while(0)

#define QUEUE_SET_X_WINDOW_TASK(x, endX) do { \
    SPITask *task = AllocTask(); \
    task->cmd = DISPLAY_SET_CURSOR_X; \
    task->data[0] = (x) >> 8; \
    task->data[1] = (x) & 0xFF; \
    task->data[2] = (endX) >> 8; \
    task->data[3] = (endX) & 0xFF; \
    task->bytes = 4; \
    bytesTransferred += 5; \
    CommitTask(); \
  } while(0)

// Main thread will dispatch SPI write tasks in a ring buffer to a worker thread
#define SPI_QUEUE_LENGTH (DISPLAY_HEIGHT*3*6*10) // Entering a scanline costs one SPI task, setting X coordinate a second, and data span a third; have enough room for a couple of these for each scanline.
extern SPITask tasks[SPI_QUEUE_LENGTH];
extern volatile uint32_t queueHead, queueTail;
extern volatile uint32_t spiBytesQueued;
extern double spiUsecsPerByte;

#ifdef STATISTICS
extern volatile uint64_t spiThreadIdleUsecs;
extern volatile uint64_t spiThreadSleepStartTime;
extern volatile int spiThreadSleeping;
#endif

static inline SPITask *AllocTask() // Returns a pointer to a new SPI task block, called on main thread
{
  // If the SPI task queue is full, wait for the SPI thread to process some tasks. This throttles the main thread to not run too fast.
  while((queueTail + 1) % SPI_QUEUE_LENGTH == queueHead) usleep(100);
  return tasks+queueTail;
}

static inline void CommitTask() // Advertises the given SPI task from main thread to worker, called on main thread
{
  __sync_synchronize();
  __atomic_fetch_add(&spiBytesQueued, tasks[queueTail].bytes, __ATOMIC_RELAXED);
  uint32_t tail = queueTail;
  queueTail = (tail + 1) % SPI_QUEUE_LENGTH;
  if (queueHead == tail) syscall(SYS_futex, &queueTail, FUTEX_WAKE, 1, 0, 0, 0); // Wake the SPI thread if it was sleeping to get new tasks
}

void InitSPI();
void DeinitSPI();
void RunSPITask(SPITask *task);
