#include <bcm_host.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/futex.h>
#include <linux/spi/spidev.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <endian.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define MAX(x, y) ((x) >= (y) ? (x) : (y))

// Build options: Uncomment any of these, or set at the command line to configure:

// If defined, prints out performance logs to stdout every second
// #define STATISTICS

// If defined, no sleeps are specified and the code runs as fast as possible. This should not improve
// performance, as the code has been developed with the mindset that sleeping should only occur at
// times when there is no work to do, rather than sleeping to reduce power usage. The only expected
// effect of this is that CPU usage shoots to 200%, while display update FPS is the same. Present
// here to allow toggling to debug this assumption.
// #define NO_THROTTLING

// If defined, display updates are synced to the vsync signal provided by the VideoCore GPU. That seems
// to occur quite precisely at 60 Hz. Testing on PAL NES games that run at 50Hz, this will not work well,
// since they produce new frames at every 20msecs, and the VideoCore functions for snapshotting also will
// output new frames at this vsync-detached interval, so there's a 50 Hz vs 60 Hz mismatch that results
// in visible microstuttering. Still, providing this as an option, this might be good for content that
// is known to run at native 60Hz.
// #define USE_GPU_VSYNC

// If defined, progressive updating is always used (at the expense of slowing down refresh rate if it's
// too much for the display to handle)
// #define NO_INTERLACING

// Configures the desired display update rate.
#define TARGET_FRAME_RATE 60

#define FATAL_ERROR(msg) do { fprintf(stderr, "%s\n", msg); syslog(LOG_ERR, msg); exit(1); } while(0)

#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
#define DISPLAY_BYTESPERPIXEL 2
#define SCANLINE_SIZE (DISPLAY_WIDTH*DISPLAY_BYTESPERPIXEL)
#define FRAMEBUFFER_SIZE (DISPLAY_WIDTH*DISPLAY_HEIGHT*DISPLAY_BYTESPERPIXEL)

#define BCM2835_GPIO_BASE               0x200000
#define BCM2835_SPI0_BASE               0x204000

#define BCM2835_SPI0_CS_CLEAR                0x00000030 /*!< Clear FIFO Clear RX and TX */
#define BCM2835_SPI0_CS_TA                   0x00000080 /*!< Transfer Active */
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
#define GPIO_TFT_DATA_CONTROL 25  /*!< Version 1, Pin P1-22, PiTFT 2.8 resistive Data/Control pin */

struct GPIORegisterFile
{
  uint32_t gpfsel[6], reserved0; // GPIO Function Select registers, 3 bits per pin, 10 pins in an uint32_t
  uint32_t gpset[2], reserved1; // GPIO Pin Output Set registers, write a 1 to bit at index I to set the pin at index I high
  uint32_t gpclr[2]; // GPIO Pin Output Clear registers, write a 1 to bit at index I to set the pin at index I low
};
volatile GPIORegisterFile *gpio = 0;

#define SET_GPIO_MODE(pin, mode) gpio->gpfsel[(pin)/10] = (gpio->gpfsel[(pin)/10] & ~(0x7 << ((pin) % 10) * 3)) | ((mode) << ((pin) % 10) * 3)
#define SET_GPIO(pin) gpio->gpset[0] = 1 << (pin) // Pin must be (0-31)
#define CLEAR_GPIO(pin) gpio->gpclr[0] = 1 << (pin) // Pin must be (0-31)

struct SPIRegisterFile
{
  uint32_t cs;   // SPI Master Control and Status register
  uint32_t fifo; // SPI Master TX and RX FIFOs
  uint32_t clk;  // SPI Master Clock Divider
};
volatile SPIRegisterFile *spi = 0;

#define BEGIN_SPI_COMMUNICATION() do { spi->cs |= BCM2835_SPI0_CS_CLEAR | BCM2835_SPI0_CS_TA; __sync_synchronize(); } while(0)
#define END_SPI_COMMUNICATION()  spi->cs &= ~BCM2835_SPI0_CS_TA

#define MAX_SPI_TASK_SIZE SCANLINE_SIZE

struct SPITask
{
  uint8_t cmd;
  uint8_t data[MAX_SPI_TASK_SIZE];
  uint16_t bytes;
};

// Synchonously performs a single SPI command byte + N data bytes transfer on the calling thread. Call in between a BEGIN_SPI_COMMUNICATION() and END_SPI_COMMUNICATION() pair.
void RunSPITask(SPITask *task)
{
  // An SPI transfer to the display always starts with one control (command) byte, followed by N data bytes.
  CLEAR_GPIO(GPIO_TFT_DATA_CONTROL);
  __sync_synchronize();
  spi->fifo = task->cmd;
  while(!(spi->cs & BCM2835_SPI0_CS_RXD)) ;
  (void)spi->fifo;
  SET_GPIO(GPIO_TFT_DATA_CONTROL);
  __sync_synchronize();

  // Write the data bytes
  if (task->bytes == 2) // Special case for 2 byte transfers, such as X or Y coordinate sets, or single pixel fills
  {
    spi->fifo = task->data[0];
    spi->fifo = task->data[1];
    while (!(spi->cs & BCM2835_SPI0_CS_DONE)) ;
    (void)spi->fifo;
    (void)spi->fifo;
  }
  else if (task->bytes == 4) // Special case for 4 byte transfers, such as X [begin, end] window sets
  {
    spi->fifo = task->data[0];
    spi->fifo = task->data[1];
    spi->fifo = task->data[2];
    spi->fifo = task->data[3];
    while (!(spi->cs & BCM2835_SPI0_CS_DONE)) ;
    (void)spi->fifo;
    (void)spi->fifo;
    (void)spi->fifo;
    (void)spi->fifo;
  }
  else
  {
    int bytesToRead = task->bytes;
    for(uint8_t *tStart = task->data, *tEnd = task->data + task->bytes; tStart < tEnd;)
    {
      uint32_t v = spi->cs;
      if ((v & BCM2835_SPI0_CS_TXD)) spi->fifo = *tStart++;
      if ((v & BCM2835_SPI0_CS_RXD)) (void)spi->fifo, --bytesToRead;
    }
    // Flush the remaining read bytes to finish the transfer
    while(bytesToRead > 0)
      if (spi->cs & BCM2835_SPI0_CS_RXD)
        (void)spi->fifo, --bytesToRead;
  }
}

// A convenience for defining and dispatching SPI task bytes inline
#define SPI_TRANSFER(...) do { \
    char data_buffer[] = { __VA_ARGS__ }; \
    SPITask t; \
    t.cmd = data_buffer[0]; \
    memcpy(t.data, data_buffer+1, sizeof(data_buffer)-1); \
    t.bytes = sizeof(data_buffer)-1; \
    RunSPITask(&t); \
  } while(0)

#define CURSOR_X 0x2A
#define CURSOR_Y 0x2B

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
    task->cmd = CURSOR_X; \
    task->data[0] = (x) >> 8; \
    task->data[1] = (x) & 0xFF; \
    task->data[2] = (endX) >> 8; \
    task->data[3] = (endX) & 0xFF; \
    task->bytes = 4; \
    bytesTransferred += 5; \
    CommitTask(); \
  } while(0)

// Main thread will dispatch SPI write tasks in a ring buffer to a worker thread
#define SPI_QUEUE_LENGTH (DISPLAY_HEIGHT*3*6) // Entering a scanline costs one SPI task, setting X coordinate a second, and data span a third; have enough room for a couple of these for each scanline.
SPITask tasks[SPI_QUEUE_LENGTH];
volatile uint32_t queueHead = 0, queueTail = 0;
volatile int spiBytesQueued = 0;

#ifdef NO_THROTTLING
#define usleep(x) ((void)0)
#endif

#ifdef STATISTICS
volatile uint64_t spiThreadIdleUsecs = 0;
#endif

SPITask *AllocTask() // Returns a pointer to a new SPI task block, called on main thread
{
  // If the SPI task queue is full, wait for the SPI thread to process some tasks. This throttles the main thread to not run too fast.
  while((queueTail + 1) % SPI_QUEUE_LENGTH == queueHead) usleep(100);
  return tasks+queueTail;
}

void CommitTask() // Advertises the given SPI task from main thread to worker, called on main thread
{
  __sync_synchronize();
  __sync_fetch_and_add(&spiBytesQueued, tasks[queueTail].bytes);
  uint32_t tail = queueTail;
  queueTail = (tail + 1) % SPI_QUEUE_LENGTH;
  if (queueHead == tail) syscall(SYS_futex, &queueTail, FUTEX_WAKE, 1, 0, 0, 0); // Wake the SPI thread if it was sleeping to get new tasks
}

SPITask *GetTask() // Returns the first task in the queue, called in worker thread
{
  return (queueHead != queueTail) ? tasks+queueHead : 0;
}

void DoneTask() // Frees the first SPI task from the queue, called in worker thread
{
  __sync_fetch_and_sub(&spiBytesQueued, tasks[queueHead].bytes);
  queueHead = (queueHead + 1) % SPI_QUEUE_LENGTH;
  __sync_synchronize();
}

uint64_t tick()
{
  struct timespec start;
  clock_gettime(CLOCK_REALTIME, &start);
  return start.tv_sec * 1000000 + start.tv_nsec / 1000;
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
#endif
      syscall(SYS_futex, &queueTail, FUTEX_WAIT, queueHead, 0, 0, 0); // Start sleeping until we get new tasks
#ifdef STATISTICS
      uint64_t t1 = tick();
      __sync_fetch_and_add(&spiThreadIdleUsecs, t1-t0);
#endif
    }
  }
}

#ifdef USE_GPU_VSYNC
volatile /*bool*/uint32_t gpuFrameAvailable = 0;

void VsyncCallback(DISPMANX_UPDATE_HANDLE_T u, void *arg)
{
  __atomic_store_n(&gpuFrameAvailable, 1, __ATOMIC_SEQ_CST);
  syscall(SYS_futex, &gpuFrameAvailable, FUTEX_WAKE, 1, 0, 0, 0); // Wake the main thread to process a new frame
}

uint64_t EstimateFrameRateInterval()
{
  return 1000000/60;
}

#else // !USE_GPU_VSYNC

// Since we are polling for received GPU frames, run a histogram to predict when the next frame will arrive.
// The histogram needs to be sufficiently small as to not cause a lag when frame rate suddenly changes on e.g.
// main menu <-> ingame transitions
#define HISTOGRAM_SIZE 20
uint64_t frameArrivalTimes[HISTOGRAM_SIZE];
uint64_t frameArrivalTimesTail = 0;
uint64_t lastFramePollTime = 0;
int histogramSize = 0;

// Returns Nth most recent entry in the frame times histogram, 0 = most recent, (histogramSize-1) = oldest
#define GET_HISTOGRAM(idx) frameArrivalTimes[(frameArrivalTimesTail - 1 - (idx) + HISTOGRAM_SIZE) % HISTOGRAM_SIZE]

void AddHistogramSample()
{
  frameArrivalTimes[frameArrivalTimesTail] = tick();
  frameArrivalTimesTail = (frameArrivalTimesTail + 1) % HISTOGRAM_SIZE;
  if (histogramSize < HISTOGRAM_SIZE) ++histogramSize;
}

int cmp(const void *e1, const void *e2) { return *(uint64_t*)e1 > *(uint64_t*)e2; }

uint64_t EstimateFrameRateInterval()
{
  if (histogramSize == 0) return 1000000/TARGET_FRAME_RATE;
  uint64_t mostRecentFrame = GET_HISTOGRAM(0);

  // High sleep mode hacks to save battery when ~idle: (These could be removed with an event based VideoCore display refresh API)
  uint64_t timeNow = tick();
  if (timeNow - mostRecentFrame > 60000000) { histogramSize = 1; return 500000; } // if it's been more than one minute since last seen update, assume interval of 500ms.
  if (timeNow - mostRecentFrame > 100000) return 100000; // if it's been more than 100ms since last seen update, assume interval of 100ms.

  if (histogramSize <= 1) return 1000000/TARGET_FRAME_RATE;

  // Look at the intervals of all previous arrived frames, and take their 40% percentile as our expected current frame rate
  uint64_t intervals[HISTOGRAM_SIZE-1];
  for(int i = 0; i < histogramSize-1; ++i)
    intervals[i] = GET_HISTOGRAM(i) - GET_HISTOGRAM(i+1);
  qsort(intervals, histogramSize-1, sizeof(uint64_t), cmp);
  uint64_t interval = intervals[(histogramSize-1)*2/5];

  // With bad luck, we may actually have synchronized to observing every second update, so halve the computed interval if it looks like a long period of time
  if (interval >= 2000000/TARGET_FRAME_RATE) interval /= 2;
  if (interval > 100000) interval = 100000;
  return MAX(interval, 1000000/TARGET_FRAME_RATE);
}

uint64_t PredictNextFrameArrivalTime()
{
  uint64_t mostRecentFrame = histogramSize > 0 ? GET_HISTOGRAM(0) : tick();

  // High sleep mode hacks to save battery when ~idle: (These could be removed with an event based VideoCore display refresh API)
  uint64_t timeNow = tick();
  if (timeNow - mostRecentFrame > 60000000) { histogramSize = 1; return lastFramePollTime + 100000; } // if it's been more than one minute since last seen update, assume interval of 500ms.
  if (timeNow - mostRecentFrame > 100000) return lastFramePollTime + 100000; // if it's been more than 100ms since last seen update, assume interval of 100ms.

  uint64_t interval = EstimateFrameRateInterval();
  // Assume that frames are arriving at times mostRecentFrame + k * interval.
  // Find integer k such that mostRecentFrame + k * interval >= timeNow
  // i.e. k = ceil((timeNow - mostRecentFrame) / interval)
  uint64_t k = (timeNow - mostRecentFrame + interval - 1) / interval;
  uint64_t nextFrameArrivalTime = mostRecentFrame + k * interval;
  uint64_t timeOfPreviousMissedFrame = nextFrameArrivalTime - interval;

  // If there should have been a frame just 1/3rd of our interval window ago, assume it was just missed and report back "the next frame is right now"
  if (timeNow - timeOfPreviousMissedFrame < interval/3 && timeOfPreviousMissedFrame > mostRecentFrame) return timeNow;
  else return nextFrameArrivalTime;
}

#endif // ~USE_GPU_VSYNC

// Spans track dirty rectangular areas on screen
struct Span
{
  uint16_t x, endX, y, endY, lastScanEndX, size; // Specifies a box of width [x, endX[ * [y, endY[, where scanline endY-1 can be partial, and ends in lastScanEndX.
  Span *next; // Maintain a linked skip list inside the array for fast seek to next active element when pruning
};
Span spans[DISPLAY_WIDTH*DISPLAY_HEIGHT/2];

int main()
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
  const int busDivisor = 8;
  spi->clk = busDivisor; // Clock Divider determines SPI bus speed, resulting speed=256MHz/clk
  const double usecsPerByte = 8.0 * busDivisor / 256; // How many microseconds does transferring a single byte over the SPI take?

  // Initialize display
  BEGIN_SPI_COMMUNICATION();
  {
    SPI_TRANSFER(0xC0/*Power Control 1*/, 0x23/*VRH=4.60V*/); // Set the GVDD level, which is a reference level for the VCOM level and the grayscale voltage level.
    SPI_TRANSFER(0xC1/*Power Control 2*/, 0x10/*AVCC=VCIx2,VGH=VCIx7,VGL=-VCIx4*/); // Sets the factor used in the step-up circuits. To reduce power consumption, set a smaller factor.
    SPI_TRANSFER(0xC5/*VCOM Control 1*/, 0x3e/*VCOMH=4.250V*/, 0x28/*VCOML=-1.500V*/);
    SPI_TRANSFER(0xC7/*VCOM Control 2*/, 0x86/*VCOMH=VMH-58,VCOML=VML-58*/);
    SPI_TRANSFER(0x3A/*COLMOD: Pixel Format Set*/, 0x55/*DPI=16bits/pixel,DBI=16bits/pixel*/);
    SPI_TRANSFER(0xB1/*Frame Rate Control (In Normal Mode/Full Colors)*/, 0x00/*DIVA=fosc*/, 0x18/*RTNA(Frame Rate)=79Hz*/);
    SPI_TRANSFER(0xB6/*Display Function Control*/, 0x08/*PTG=Interval Scan,PT=V63/V0/VCOML/VCOMH*/, 0x82/*REV=1(Normally white),ISC(Scan Cycle)=5 frames*/, 0x27/*LCD Driver Lines=320*/);
    SPI_TRANSFER(0x26/*Gamma Set*/, 0x01/*Gamma curve 1 (G2.2)*/);
    SPI_TRANSFER(0xE0/*Positive Gamma Correction*/, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00);
    SPI_TRANSFER(0xE1/*Negative Gamma Correction*/, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F);
    SPI_TRANSFER(0x11/*Sleep Out*/);
    usleep(120 * 1000);
    SPI_TRANSFER(/*Display ON*/0x29);

    // Since we are doing delta updates to only changed pixels, clear display initially to black for known starting state
    SPITask clearLine = {};
    clearLine.cmd = 0x2C;
    clearLine.bytes = SCANLINE_SIZE;
    for(int y = 0; y < DISPLAY_HEIGHT; ++y)
    {
      SPI_TRANSFER(0x2A/*X*/, 0, 0, DISPLAY_WIDTH >> 8, DISPLAY_WIDTH & 0xFF);
      SPI_TRANSFER(0x2B/*Y*/, y >> 8, y & 0xFF, DISPLAY_HEIGHT >> 8, DISPLAY_HEIGHT & 0xFF);
      RunSPITask(&clearLine);
    }
    SPI_TRANSFER(0x2A/*X*/, 0, 0, DISPLAY_WIDTH >> 8, DISPLAY_WIDTH & 0xFF);
    SPI_TRANSFER(0x2B/*Y*/, 0, 0, DISPLAY_HEIGHT >> 8, DISPLAY_HEIGHT & 0xFF);
  }
  END_SPI_COMMUNICATION();

  // Track SPI display controller write X and Y cursors.
  int spiX = 0;
  int spiY = 0;
  int spiEndX = DISPLAY_WIDTH;

  uint16_t *framebuffer[2] = { (uint16_t *)malloc(FRAMEBUFFER_SIZE), (uint16_t *)malloc(FRAMEBUFFER_SIZE) };
  memset(framebuffer[0], 0, FRAMEBUFFER_SIZE); // Doublebuffer received GPU memory contents, first buffer contains current GPU memory,
  memset(framebuffer[1], 0, FRAMEBUFFER_SIZE); // second buffer contains whatever the display is currently showing. This allows diffing pixels between the two.

  uint16_t *gpuFramebuffer = (uint16_t *)malloc(FRAMEBUFFER_SIZE);
  memset(gpuFramebuffer, 0, FRAMEBUFFER_SIZE); // third buffer contains last seen GPU memory contents, used to compare polled frames to whether they actually have changed.

  // Create a dedicated thread to feed the SPI bus. While this is fast, it consumes a lot of CPU. It would be best to replace
  // this thread with a kernel module that processes the created SPI task queue using interrupts. (while juggling the GPIO D/C line as well)
  pthread_t thread;
  int rc = pthread_create(&thread, NULL, spi_thread, NULL); // After creating the thread, it is assumed to have ownership of the SPI bus, so no SPI chat on the main thread after this.
  if (rc != 0) FATAL_ERROR("Failed to create SPI thread!");

  // Initialize GPU frame grabbing subsystem
  bcm_host_init();
  DISPMANX_DISPLAY_HANDLE_T display = vc_dispmanx_display_open(0);
  if (!display) FATAL_ERROR("Unable to open primary display");
  DISPMANX_MODEINFO_T display_info;
  int ret = vc_dispmanx_display_get_info(display, &display_info);
  if (ret) FATAL_ERROR("Unable to get primary display information");
  syslog(LOG_INFO, "Primary display is %d x %d", display_info.width, display_info.height);
  int fb1 = open("/dev/fb1", O_RDWR);
  if (fb1 == -1) FATAL_ERROR("Unable to open secondary display");
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;
  if (ioctl(fb1, FBIOGET_FSCREENINFO, &finfo)) FATAL_ERROR("Unable to get secondary display information");
  if (ioctl(fb1, FBIOGET_VSCREENINFO, &vinfo)) FATAL_ERROR("Unable to get secondary display information");
  syslog(LOG_INFO, "Second display is %d x %d %dbps\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
  uint32_t image_prt;
  DISPMANX_RESOURCE_HANDLE_T screen_resource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, vinfo.xres, vinfo.yres, &image_prt);
  if (!screen_resource) FATAL_ERROR("Unable to create screen buffer");
  VC_RECT_T rect;
  vc_dispmanx_rect_set(&rect, 0, 0, vinfo.xres, vinfo.yres);

#ifdef USE_GPU_VSYNC
  // Register to receive vsync notifications. This is a heuristic, since the application might not be locked at vsync, and even
  // if it was, this signal is not a guaranteed edge trigger for availability of new frames.
  vc_dispmanx_vsync_callback(display, VsyncCallback, 0);
#endif

#ifdef STATISTICS
  uint64_t statsBytesTransferred = 0;
  uint64_t statsLastPrint = tick();
  uint64_t statsProgressiveFramesRendered = 0;
  uint64_t statsInterlacedFramesRendered = 0;
  uint64_t statsFramesPassed = 0;
  uint64_t statsMainThreadWaitedForSPIUsecs = 0;
  uint64_t statsMainThreadSleptOnNoActivityUsecs = 0;
  uint32_t statsNumberOfSpans = 0;
  uint32_t statsSpanLength = 0;
#endif

  bool interlacedUpdate = false; // True if the previous update we did was an interlaced half field update.
  uint64_t tFieldStart = tick();
  int frameParity = 0; // For interlaced frame updates, this is either 0 or 1 to denote evens or odds.
  for(;;)
  {

#ifdef USE_GPU_VSYNC
    // Synchronously block to wait until GPU vsync occurs and we should have a new frame to present.
    if (!gpuFrameAvailable)
    {
#ifdef STATISTICS
      uint64_t t0 = tick();
#endif
      syscall(SYS_futex, &gpuFrameAvailable, FUTEX_WAIT, queueHead, 0, 0, 0);
#ifdef STATISTICS
      statsMainThreadSleptOnNoActivityUsecs += tick() - t0;
#endif
    }
    if (!gpuFrameAvailable) continue;

#else // ~USE_GPU_VSYNC

    uint64_t nextFrameArrivalTime = PredictNextFrameArrivalTime();
    int64_t timeToSleep = nextFrameArrivalTime - tick();
    const int64_t minimumSleepTime = 2500; // Don't sleep if the next frame is due to arrive in less than this much time
    if (timeToSleep > minimumSleepTime && !interlacedUpdate)
    {
#ifdef STATISTICS
      uint64_t t0 = tick();
#endif
      usleep(timeToSleep - minimumSleepTime);
#ifdef STATISTICS
      statsMainThreadSleptOnNoActivityUsecs += tick() - t0;
#endif
    }
#endif // ~USE_GPU_VSYNC

    // Peek at the SPI thread's workload and throttle a bit if it has got a lot of work still to do.
    double usecsUntilSpiQueueEmpty = spiBytesQueued*usecsPerByte;
    if (usecsUntilSpiQueueEmpty > 2000)
    {
#ifdef STATISTICS
      statsMainThreadWaitedForSPIUsecs += (int)(usecsUntilSpiQueueEmpty * 0.5);
#endif
      usleep((int)(usecsUntilSpiQueueEmpty * 0.5));
    }

    uint64_t tFrameStart = tick();

    // Grab a new frame from the GPU. TODO: Figure out a way to get a frame callback for each GPU-rendered frame,
    // that would be vastly superior for lower latency, reduced stuttering and lighter processing overhead.
    // Currently this implemented method just takes a snapshot of the most current GPU framebuffer contents,
    // without any concept of "finished frames". If this is the case, it's possible that this could grab the same
    // frame twice, and then potentially missing, or displaying the later appearing new frame at a very last moment.
    // Profiling, the following two lines take around ~1msec of time.
    vc_dispmanx_snapshot(display, screen_resource, (DISPMANX_TRANSFORM_T)0);
    vc_dispmanx_resource_read_data(screen_resource, &rect, framebuffer[0], vinfo.xres * vinfo.bits_per_pixel / 8);
    lastFramePollTime = tFrameStart;

    bool gotNewFramebuffer = false;
    for(uint32_t *newfb = (uint32_t*)framebuffer[0], *oldfb = (uint32_t*)gpuFramebuffer, *endfb = (uint32_t*)gpuFramebuffer + FRAMEBUFFER_SIZE/4; oldfb < endfb;)
      if (*newfb++ != *oldfb++)
      {
        gotNewFramebuffer = true;
        break;
      }

#ifndef USE_GPU_VSYNC
    if (gotNewFramebuffer)
    {
      AddHistogramSample();
      memcpy(gpuFramebuffer, framebuffer[0], FRAMEBUFFER_SIZE);
    }
#endif

    // Count how many pixels overall have changed on the new GPU frame, compared to what is being displayed on the SPI screen.
    uint16_t *scanline = framebuffer[0];
    uint16_t *prevScanline = framebuffer[1];
    int changedPixels = 0;
    for(int i = 0; i < DISPLAY_WIDTH*DISPLAY_HEIGHT; ++i)
      if (*scanline++ != *prevScanline++)
        ++changedPixels;

    // If too many pixels have changed on screen, drop adaptively to interlaced updating to keep up the frame rate.
    const double tooMuchToUpdateUsecs = 1000000 / TARGET_FRAME_RATE * 2 / 3; // Use a rather arbitrary 2/3rds heuristic as an estimate of too much workload.
#ifdef NO_INTERLACING
    interlacedUpdate = false;
#else
    interlacedUpdate = (changedPixels * 2 * usecsPerByte > tooMuchToUpdateUsecs); // Decide whether to do interlacedUpdate - only updates half of the screen
    if (interlacedUpdate) frameParity = 1-frameParity; // Swap even-odd fields every second time we do an interlaced update (progressive updates ignore field order)
#endif

#ifdef USE_GPU_VSYNC
    else __atomic_store_n(&gpuFrameAvailable, 0, __ATOMIC_RELAXED); // Doing a progressive update, so mark the latest GPU frame fully processed (in interlaced update, we'll come right back to this at next cycle)
#endif
    int y = interlacedUpdate ? frameParity : 0;
    scanline = framebuffer[0] + y*DISPLAY_WIDTH;
    prevScanline = framebuffer[1] + y*DISPLAY_WIDTH;

    int bytesTransferred = 0;

    // Collect all spans in this image
    int numSpans = 0;
    for(;y < DISPLAY_HEIGHT; ++y, scanline += DISPLAY_WIDTH, prevScanline += DISPLAY_WIDTH)
    {
      for(int x = 0; x < DISPLAY_WIDTH; ++x)
      {
        if (scanline[x] == prevScanline[x]) continue;
        int endX = x+1;
        while(endX < DISPLAY_WIDTH && scanline[endX] != prevScanline[endX]) ++endX; // Find where this span ends
        spans[numSpans].x = x;
        spans[numSpans].endX = spans[numSpans].lastScanEndX = endX;
        spans[numSpans].y = y;
        spans[numSpans].endY = y+1;
        spans[numSpans].size = endX - x;
        if (numSpans > 0) spans[numSpans-1].next = &spans[numSpans];
        spans[numSpans++].next = 0;
        x = endX;
      }

      // If doing an interlaced update, skip over every second scanline.
      if (interlacedUpdate) ++y, scanline += DISPLAY_WIDTH, prevScanline += DISPLAY_WIDTH;
    }

#define SPAN_MERGE_THRESHOLD 4

    // Merge spans together on the same scanline
    for(Span *i = &spans[0]; i; i = i->next)
    {
      Span *prev = i;
      for(Span *j = i->next; j; j = j->next)
      {
        // If the spans i and j are vertically apart, don't attempt to merge span i any further, since all spans >= j will also be farther vertically apart.
        // (the list is nondecreasing with respect to Span::y)
        if (j->y != i->y) break;
        int newSize = j->endX-i->x-1;
        int wastedPixels = newSize - i->size - j->size;
        if (wastedPixels <= SPAN_MERGE_THRESHOLD && newSize*DISPLAY_BYTESPERPIXEL <= MAX_SPI_TASK_SIZE)
        {
          i->endX = j->endX;
          i->lastScanEndX = j->endX;
          i->size = newSize;
          prev->next = j->next;
          j = prev;
        }
        else // Not merging - travel to next node remembering where we came from
          prev = j;
      }
    }

    // Merge spans together on adjacent scanlines - works only if doing a progressive update
    if (!interlacedUpdate)
      for(Span *i = &spans[0]; i; i = i->next)
      {
        int iSize = (i->endX-i->x)*(i->endY-i->y-1) + (i->lastScanEndX - i->x);
        Span *prev = i;
        for(Span *j = i->next; j; j = j->next)
        {
          // If the spans i and j are vertically apart, don't attempt to merge span i any further, since all spans >= j will also be farther vertically apart.
          // (the list is nondecreasing with respect to Span::y)
          if (j->y > i->endY) break;

          int jSize = (j->endX-j->x)*(j->endY-j->y-1) + (j->lastScanEndX - j->x);

          // Merge the spans i and j, and figure out the wastage of doing so
          int x = MIN(i->x, j->x);
          int y = MIN(i->y, j->y);
          int endX = MAX(i->endX, j->endX);
          int endY = MAX(i->endY, j->endY);
          int lastScanEndX = (endY > i->endY) ? j->lastScanEndX : ((endY > j->endY) ? i->lastScanEndX : MAX(i->lastScanEndX, j->lastScanEndX));
          int newSize = (endX-x)*(endY-y-1) + (lastScanEndX - x);
          int wastedPixels = newSize - iSize - jSize;
          if (wastedPixels <= SPAN_MERGE_THRESHOLD && newSize*DISPLAY_BYTESPERPIXEL <= MAX_SPI_TASK_SIZE)
          {
            i->x = x;
            i->y = y;
            i->endX = endX;
            i->endY = endY;
            i->lastScanEndX = lastScanEndX;
            iSize = newSize;
            prev->next = j->next;
            j = prev;
          }
          else // Not merging - travel to next node remembering where we came from
            prev = j;
        }
      }

    // Submit spans
    for(Span *i = &spans[0]; i; i = i->next)
    {
      if (i->x == i->endX) continue;

      // Update the write cursor if needed
      if (spiY != i->y)
      {
        QUEUE_MOVE_CURSOR_TASK(CURSOR_Y, i->y);
        spiY = i->y;
      }

      if (i->endY != i->y + 1 && (spiX != i->x || spiEndX != i->endX)) // Multiline span?
      {
        QUEUE_SET_X_WINDOW_TASK(i->x, i->endX-1);
        spiX = i->x;
        spiEndX = i->endX;
      }
      else // Singleline span
      {
        if (spiEndX < i->endX) // Need to push the X end window?
        {
          // We are doing a single line span and need to increase the X window. If possible,
          // peek ahead to cater to the next multiline span update if that will be compatible.
          int nextEndX = DISPLAY_WIDTH;
          for(Span *j = i->next; j; j = j->next)
            if (j->endY != j->y)
            {
              if (j->endX >= i->endX) nextEndX = j->endX;
              break;
            }
          QUEUE_SET_X_WINDOW_TASK(i->x, nextEndX-1);
          spiX = i->x;
          spiEndX = nextEndX;
        }
        else if (spiX != i->x)
        {
          QUEUE_MOVE_CURSOR_TASK(CURSOR_X, i->x);
          spiX = i->x;
        }
      }

      // Submit the span pixels
      SPITask *task = AllocTask();
      task->cmd = 0x2C;
      int iSize = (i->endX-i->x)*(i->endY-i->y-1) + (i->lastScanEndX - i->x);
      task->bytes = iSize*DISPLAY_BYTESPERPIXEL;

#ifdef STATISTICS
      ++statsNumberOfSpans;
      statsSpanLength += iSize;
#endif
      bytesTransferred += task->bytes+1;
      uint16_t *scanline = framebuffer[0] + i->y * DISPLAY_WIDTH;
      uint16_t *prevScanline = framebuffer[1] + i->y * DISPLAY_WIDTH;
      uint16_t *data = (uint16_t*)task->data;
      for(int y = i->y; y < i->endY; ++y, scanline += DISPLAY_WIDTH, prevScanline += DISPLAY_WIDTH)
      {
        int endX = (y+1==i->endY) ? i->lastScanEndX : i->endX;
        for(int x = i->x; x < endX; ++x)
        {
          *data++ = __builtin_bswap16(scanline[x]); // Write out the RGB565 data, swapping to big endian byte order for the SPI bus
        }
        memcpy(prevScanline+i->x, scanline+i->x, (endX-i->x)*DISPLAY_BYTESPERPIXEL);
      }
      CommitTask();
    }

#ifdef STATISTICS
    if (bytesTransferred > 0)
    {
      if (interlacedUpdate) ++statsInterlacedFramesRendered;
      else ++statsProgressiveFramesRendered;
    }
    ++statsFramesPassed;
    statsBytesTransferred += bytesTransferred;

    uint64_t elapsed = tick() - statsLastPrint;
    if (elapsed > 1000000)
    {
      statsLastPrint = tick();
      uint64_t spiThreadIdleFor = __atomic_load_n(&spiThreadIdleUsecs, __ATOMIC_RELAXED);
      __sync_fetch_and_sub(&spiThreadIdleUsecs, spiThreadIdleFor);

      printf("Fr.pollrate:%.3f, est.frameinterval: %.3fms, frms xfred: %.3f (%.2f%% prog), SPI bus: %.2fbps, transferred: %lldB (%.2f%% of total frame data), SPI idle: %lldus, main: wait for SPI: %lldus, no activity: %lldus, spans:%.2f/frm,%.2fpx/span\n",
        statsFramesPassed * 1000.0 / (elapsed / 1000.0),
        EstimateFrameRateInterval() / 1000.0,
        (statsProgressiveFramesRendered + statsInterlacedFramesRendered) * 1000.0 / (elapsed / 1000.0),
        statsProgressiveFramesRendered * 100.0 / (statsProgressiveFramesRendered + statsInterlacedFramesRendered),
        (double)8.0 * statsBytesTransferred * 1000.0 / (elapsed / 1000.0),
        statsBytesTransferred, 
        statsBytesTransferred*100.0/((statsProgressiveFramesRendered + statsInterlacedFramesRendered)*FRAMEBUFFER_SIZE),
        spiThreadIdleFor, statsMainThreadWaitedForSPIUsecs, statsMainThreadSleptOnNoActivityUsecs,
        (double)statsNumberOfSpans/(statsProgressiveFramesRendered + statsInterlacedFramesRendered), (double)statsSpanLength/statsNumberOfSpans);
      statsBytesTransferred = statsProgressiveFramesRendered = statsInterlacedFramesRendered = statsFramesPassed = statsMainThreadWaitedForSPIUsecs = statsMainThreadSleptOnNoActivityUsecs = 0;
      statsNumberOfSpans = statsSpanLength = 0;
    }
#endif
  }

  // At exit, set all pins back to the default GPIO state (input 0x00) (we never actually reach here, since it's not possible ATM to gracefully quit..)
  SET_GPIO_MODE(GPIO_TFT_DATA_CONTROL, 0);
  SET_GPIO_MODE(GPIO_SPI0_CE1, 0);
  SET_GPIO_MODE(GPIO_SPI0_CE0, 0);
  SET_GPIO_MODE(GPIO_SPI0_MISO, 0);
  SET_GPIO_MODE(GPIO_SPI0_MOSI, 0);
  SET_GPIO_MODE(GPIO_SPI0_CLK, 0);
}
