#include <bcm_host.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/spi/spidev.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <endian.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

// Build options: Uncomment any of these, or set at the command line to configure:

// If defined, prints out performance logs to stdout every second
// #define STATISTICS

// If defined, no sleeps are specified and the code runs as fast as possible. This should not improve
// performance, as the code has been developed with the mindset that sleeping should only occur at
// times when there is no work to do, rather than sleeping to reduce power usage. The only expected
// effect of this is that CPU usage shoots to 200%, while display update FPS is the same. Present
// here to allow toggling to debug this assumption.
//#define NO_THROTTLING

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

struct SPITask
{
  uint8_t cmd;
  uint8_t data[SCANLINE_SIZE];
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
    task->cmd = cursor; \
    task->data[0] = pos >> 8; \
    task->data[1] = pos & 0xFF; \
    task->bytes = 2; \
    bytesTransferred += 2; \
    CommitTask(); \
  } while(0)

// Main thread will dispatch SPI write tasks in a ring buffer to a worker thread
#define SPI_QUEUE_LENGTH (DISPLAY_HEIGHT*3*6) // Entering a scanline costs one SPI task, setting X coordinate a second, and data span a third; have enough room for a couple of these for each scanline.
SPITask tasks[SPI_QUEUE_LENGTH];
volatile uint16_t queueHead = 0, queueTail = 0;
volatile int spiBytesQueued = 0;

#ifdef NO_THROTTLING
#define usleep(x) ((void)0)
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
  queueTail = (queueTail + 1) % SPI_QUEUE_LENGTH;
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
      usleep(500); // Throttle not to run too fast (this is practically never reached if game renders faster than 25-30fps) TODO: Use a signal/condvar here
  }
}

uint64_t tick()
{
  struct timespec start;
  clock_gettime(CLOCK_REALTIME, &start);
  return start.tv_sec * 1000000 + start.tv_nsec / 1000;
}

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

  uint16_t *framebuffer[2] = { (uint16_t *)malloc(FRAMEBUFFER_SIZE), (uint16_t *)malloc(FRAMEBUFFER_SIZE) };
  memset(framebuffer[0], 0, FRAMEBUFFER_SIZE); // Doublebuffer received GPU memory contents, first buffer contains current GPU memory,
  memset(framebuffer[1], 0, FRAMEBUFFER_SIZE); // second buffer contains whatever the display is currently showing. This allows diffing pixels between the two.

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

#ifdef STATISTICS
  uint64_t statsBytesTransferred = 0;
  uint64_t statsLastPrint = tick();
  uint64_t statsProgressiveFramesRendered = 0;
  uint64_t statsInterlacedFramesRendered = 0;
  uint64_t statsFramesPassed = 0;
#endif

  // GPU is assumed to be producing frames at this rate. TODO: Figure out how this variable could just be removed, perhaps
  // either query GPU vsync rate, or statistically measure it from vsync_callback() interarrival times? For now hardcoded to 60
  // since that seems to be what VideoCore GPU always does(?)
  const int targetFrameRate = 60;

  uint64_t tFieldStart = tick();
  int frameParity = 0; // For interlaced frame updates, this is either 0 or 1 to denote evens or odds.
  for(;;)
  {
    // Peek at the SPI thread's workload and throttle a bit if it has got a lot of work still to do.
    double usecsUntilSpiQueueEmpty = spiBytesQueued*usecsPerByte;
    if (usecsUntilSpiQueueEmpty > 2000) usleep((int)(usecsUntilSpiQueueEmpty * 0.75));

    uint64_t tFrameStart = tick();

    // Grab a new frame from the GPU. TODO: Figure out a way to get a frame callback for each GPU-rendered frame,
    // that would be vastly superior for lower latency, reduced stuttering and lighter processing overhead.
    // Currently this implemented method just takes a snapshot of the most current GPU framebuffer contents,
    // without any concept of "finished frames". If this is the case, it's possible that this could grab the same
    // frame twice, and then potentially missing, or displaying the later appearing new frame at a very last moment.
    // Profiling, the following two lines take around ~1msec of time.
    vc_dispmanx_snapshot(display, screen_resource, (DISPMANX_TRANSFORM_T)0);
    vc_dispmanx_resource_read_data(screen_resource, &rect, framebuffer[0], vinfo.xres * vinfo.bits_per_pixel / 8);

    // Count how many pixels overall have changed on the new GPU frame, compared to what is being displayed on the SPI screen.
    uint16_t *scanline = framebuffer[0];
    uint16_t *prevScanline = framebuffer[1];
    int changedPixels = 0;
    for(int i = 0; i < DISPLAY_WIDTH*DISPLAY_HEIGHT; ++i)
      if (*scanline++ != *prevScanline++)
        ++changedPixels;

    // If too many pixels have changed on screen, drop adaptively to interlaced updating to keep up the frame rate.
    const double tooMuchToUpdateUsecs = 1000000 / targetFrameRate * 2 / 3; // Use a rather arbitrary 2/3rds heuristic as an estimate of too much workload.
    const bool interlacedUpdate = (changedPixels * 2 * usecsPerByte > tooMuchToUpdateUsecs); // Decide whether to do interlacedUpdate - only updates half of the screen
    if (interlacedUpdate) frameParity = 1-frameParity; // Swap even-odd fields every second time we do an interlaced update (progressive updates ignore field order)
    int y = interlacedUpdate ? frameParity : 0;
    scanline = framebuffer[0] + y*DISPLAY_WIDTH;
    prevScanline = framebuffer[1] + y*DISPLAY_WIDTH;

    int bytesTransferred = 0;
    for(;y < DISPLAY_HEIGHT; ++y, scanline += DISPLAY_WIDTH, prevScanline += DISPLAY_WIDTH)
    {
      int x = 0;
      while(x < DISPLAY_WIDTH && scanline[x] == prevScanline[x]) ++x; // Find first changed pixel on this scanline
      if (x < DISPLAY_WIDTH) // Need to enter this scanline?
      {
        QUEUE_MOVE_CURSOR_TASK(CURSOR_Y, y);

        // Upload spans of changed pixels
        while(x < DISPLAY_WIDTH)
        {
          QUEUE_MOVE_CURSOR_TASK(CURSOR_X, x);

          // Find where this span ends
          int nextSpanX = DISPLAY_WIDTH;
          int xEnd = x+1;
          while(xEnd < DISPLAY_WIDTH)
          {
            if (scanline[xEnd] != prevScanline[xEnd])
                ++xEnd;
            else
            {
              // Optimization: if multiple changed pixels on the same scanline have only few unchanged pixels between them,
              // it is faster to just keep transferring those as well, since moving the pixel cursor would mean flushing
              // the transfer FIFO to sync the Data/Control GPIO line, so transferring a few bytes more in order to keep the FIFO
              // warm is a perf win.

              // Peek ahead to find where the next span would start; if it's close enough we'll merge these.
              nextSpanX = xEnd+1;
              while(nextSpanX < DISPLAY_WIDTH && scanline[nextSpanX] == prevScanline[nextSpanX]) ++nextSpanX;

// BCM2835 TX FIFO is 16 bytes in size, 4 pixels = 8 bytes, which seems to be a sweet spot
#define SPAN_MERGE_DISTANCE 4

              if (nextSpanX - xEnd < SPAN_MERGE_DISTANCE) xEnd = nextSpanX;
              else break;
            }
          }

          // Submit the span
          SPITask *task = AllocTask();
          task->cmd = 0x2C;
          task->bytes = (xEnd-x)*DISPLAY_BYTESPERPIXEL;
          bytesTransferred += task->bytes;
          for(int i = 0; i < xEnd-x; ++i)
            ((uint16_t*)task->data)[i] = (scanline[x+i] >> 8) | (scanline[x+i] << 8); // Write out the RGB565 data, swapping to big endian byte order for the SPI bus
          memcpy(prevScanline+x, scanline+x, task->bytes);
          CommitTask();

          // Proceed to submitting the next span on this scanline
          x = nextSpanX;
        }
      }

      // If doing an interlaced update, skip over every second scanline.
      if (interlacedUpdate) ++y, scanline += DISPLAY_WIDTH, prevScanline += DISPLAY_WIDTH;
    }

    // If we did not do any work, throttle the main loop in short slices, so that we observe updated frames
    // from VideoCore GPU at low latency as they come available. Effective this means we poll snapshots of new frames at these intervals since I'm not aware
    // of an API that would give event-based delivery of new frames. Since we are polling to shoot for low latency on all possible refresh rates, the least
    // common multiple of all of {60, 50, 30, 25, 20, 15, 10} is 300 Hz, so poll at that frequency (~3.333ms intervals)
    if (changedPixels == 0) usleep(1000000/300);

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
      printf("Frame poll rate: %.3f, frames transferred: %.3f (of which %.2f%% progressive), SPI bus speed: %.2f bits/sec, bytes transferred: %lld (%.2f%% of total frame data)\n", 
        statsFramesPassed * 1000.0 / (elapsed / 1000.0),
        (statsProgressiveFramesRendered + statsInterlacedFramesRendered) * 1000.0 / (elapsed / 1000.0),
        statsProgressiveFramesRendered * 100.0 / (statsProgressiveFramesRendered + statsInterlacedFramesRendered),
        (double)8.0 * statsBytesTransferred * 1000.0 / (elapsed / 1000.0),
        statsBytesTransferred, 
        statsBytesTransferred*100.0/((statsProgressiveFramesRendered + statsInterlacedFramesRendered)*FRAMEBUFFER_SIZE));
      statsBytesTransferred = statsProgressiveFramesRendered = statsInterlacedFramesRendered = statsFramesPassed = 0;
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
