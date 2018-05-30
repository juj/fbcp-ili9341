#include <bcm_host.h>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <stdio.h>

#include "config.h"
#include "gpu.h"
#include "display.h"
#include "tick.h"
#include "util.h"
#include "statistics.h"

// Uncomment these build options to make the display output a random performance test pattern instead of the actual
// display content. Used to debug/measure performance.
// #define RANDOM_TEST_PATTERN

#ifdef DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE
#define RANDOM_TEST_PATTERN_STRIPE_WIDTH DISPLAY_HEIGHT
#else
#define RANDOM_TEST_PATTERN_STRIPE_WIDTH DISPLAY_WIDTH
#endif

#define RANDOM_TEST_PATTERN_FRAME_RATE 120

DISPMANX_DISPLAY_HANDLE_T display;
DISPMANX_RESOURCE_HANDLE_T screen_resource;
VC_RECT_T rect;

int frameTimeHistorySize = 0;

FrameHistory frameTimeHistory[FRAME_HISTORY_MAX_SIZE] = {};

uint16_t *videoCoreFramebuffer[2] = {};
volatile int numNewGpuFrames = 0;

int displayXOffset = 0;
int displayYOffset = 0;
int gpuFrameWidth = 0;
int gpuFrameHeight = 0;
int gpuFramebufferScanlineStrideBytes = 0;
int gpuFramebufferSizeBytes = 0;

int excessPixelsLeft = 0;
int excessPixelsRight = 0;
int excessPixelsTop = 0;
int excessPixelsBottom = 0;

pthread_t gpuPollingThread;

int RoundUpToMultipleOf(int val, int multiple)
{
  return ((val + multiple - 1) / multiple) * multiple;
}

// Tests if the pixels on the given new captured frame actually contain new image data from the previous frame
bool IsNewFramebuffer(uint16_t *possiblyNewFramebuffer, uint16_t *oldFramebuffer)
{
  for(uint32_t *newfb = (uint32_t*)possiblyNewFramebuffer, *oldfb = (uint32_t*)oldFramebuffer, *endfb = (uint32_t*)oldFramebuffer + gpuFramebufferSizeBytes/4; oldfb < endfb;)
    if (*newfb++ != *oldfb++)
      return true;
  return false;
}

void SnapshotFramebuffer(uint16_t *destination)
{
  // Grab a new frame from the GPU. TODO: Figure out a way to get a frame callback for each GPU-rendered frame,
  // that would be vastly superior for lower latency, reduced stuttering and lighter processing overhead.
  // Currently this implemented method just takes a snapshot of the most current GPU framebuffer contents,
  // without any concept of "finished frames". If this is the case, it's possible that this could grab the same
  // frame twice, and then potentially missing, or displaying the later appearing new frame at a very last moment.
  // Profiling, the following two lines take around ~1msec of time.
  int failed = vc_dispmanx_snapshot(display, screen_resource, (DISPMANX_TRANSFORM_T)0);
  if (failed)
  {
    printf("vc_dispmanx_snapshot() failed with return code %d!\n", failed);
    exit(failed);
  }
  // BUG in vc_dispmanx_resource_read_data(!!): If one is capturing a small subrectangle of a large screen resource rectangle, the destination pointer 
  // is in vc_dispmanx_resource_read_data() incorrectly still taken to point to the top-left corner of the large screen resource, instead of the top-left
  // corner of the subrectangle to capture. Therefore do dirty pointer arithmetic to adjust for this. To make this safe, videoCoreFramebuffer is allocated
  // double its needed size so that this adjusted pointer does not reference outside allocated memory (if it did, vc_dispmanx_resource_read_data() was seen
  // to randomly fail and then subsequently hang if called a second time)
#ifdef DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE
  static uint16_t *tempTransposeBuffer = 0; // Allocate as static here to keep the number of #ifdefs down a bit
  const int pixelWidth = gpuFrameHeight+excessPixelsTop+excessPixelsBottom;
  const int pixelHeight = gpuFrameWidth + excessPixelsLeft + excessPixelsRight;
  const int stride = RoundUpToMultipleOf(pixelWidth*sizeof(uint16_t), 32);
  if (!tempTransposeBuffer)
  {
    tempTransposeBuffer = (uint16_t *)malloc(pixelHeight * stride * 2);
    tempTransposeBuffer += pixelHeight * (stride>>1);
  }
  uint16_t *destPtr = tempTransposeBuffer - excessPixelsLeft * (stride >> 1) - excessPixelsTop;
#else
  uint16_t *destPtr = destination - excessPixelsTop*(gpuFramebufferScanlineStrideBytes>>1) - excessPixelsLeft;
  const int stride = gpuFramebufferScanlineStrideBytes;
#endif
  failed = vc_dispmanx_resource_read_data(screen_resource, &rect, destPtr, stride);
  if (failed)
  {
    printf("vc_dispmanx_resource_read_data failed with return code %d!\n", failed);
    exit(failed);
  }
#ifdef DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE
  // Transpose the snapshotted frame from landscape to portrait. The following takes around 0.5-1.0 msec
  // of extra CPU time, so while this improves tearing to be perhaps a bit nicer visually, it probably
  // is not good on the Pi Zero.
  for(int y = 0; y < gpuFrameHeight; ++y)
    for(int x = 0; x < gpuFrameWidth; ++x)
      destination[y*(gpuFramebufferScanlineStrideBytes>>1)+x] = tempTransposeBuffer[x*(stride>>1)+y];
#endif
}

#ifdef USE_GPU_VSYNC

void VsyncCallback(DISPMANX_UPDATE_HANDLE_T u, void *arg)
{
  // If TARGET_FRAME_RATE is e.g. 30 or 20, decimate only every second or third vsync callback to be processed.
  static int frameSkipCounter = 0;
  frameSkipCounter += TARGET_FRAME_RATE;
  if (frameSkipCounter < 60) return;
  frameSkipCounter -= 60;

  __atomic_fetch_add(&numNewGpuFrames, 1, __ATOMIC_SEQ_CST);
  syscall(SYS_futex, &numNewGpuFrames, FUTEX_WAKE, 1, 0, 0, 0); // Wake the main thread if it was sleeping to get a new frame
}

uint64_t EstimateFrameRateInterval()
{
  return 1000000/60;
}

uint64_t PredictNextFrameArrivalTime()
{
  return tick() + EstimateFrameRateInterval();
}

#else // !USE_GPU_VSYNC

// Since we are polling for received GPU frames, run a histogram to predict when the next frame will arrive.
// The histogram needs to be sufficiently small as to not cause a lag when frame rate suddenly changes on e.g.
// main menu <-> ingame transitions
#define HISTOGRAM_SIZE (2*TARGET_FRAME_RATE)
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
#ifdef RANDOM_TEST_PATTERN
  return 1000000/RANDOM_TEST_PATTERN_FRAME_RATE;
#endif
  if (histogramSize == 0) return 1000000/TARGET_FRAME_RATE;
  uint64_t mostRecentFrame = GET_HISTOGRAM(0);

  // High sleep mode hacks to save battery when ~idle: (These could be removed with an event based VideoCore display refresh API)
  uint64_t timeNow = tick();
#ifdef SAVE_BATTERY_BY_SLEEPING_WHEN_IDLE
  if (timeNow - mostRecentFrame > 60000000) { histogramSize = 1; return 500000; } // if it's been more than one minute since last seen update, assume interval of 500ms.
  if (timeNow - mostRecentFrame > 1000000) return 250000; // if it's been more than a second since last seen update, assume interval of 100ms.
  if (histogramSize < HISTOGRAM_SIZE) return 1000000/TARGET_FRAME_RATE;
#ifndef SAVE_BATTERY_BY_PREDICTING_FRAME_ARRIVAL_TIMES
  return 1000000/TARGET_FRAME_RATE;
#endif
#endif

  // Look at the intervals of all previous arrived frames, and take their 10% percentile as our expected current frame rate
  uint64_t intervals[HISTOGRAM_SIZE-1];
  for(int i = 0; i < histogramSize-1; ++i)
    intervals[i] = GET_HISTOGRAM(i) - GET_HISTOGRAM(i+1);
  qsort(intervals, histogramSize-1, sizeof(uint64_t), cmp);
  uint64_t interval = intervals[(histogramSize-1)/10];

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
#ifdef SAVE_BATTERY_BY_SLEEPING_WHEN_IDLE
  if (timeNow - mostRecentFrame > 60000000) { histogramSize = 1; return lastFramePollTime + 100000; } // if it's been more than one minute since last seen update, assume interval of 500ms.
  if (timeNow - mostRecentFrame > 100000) return lastFramePollTime + 100000; // if it's been more than 100ms since last seen update, assume interval of 100ms.
#endif
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

extern volatile bool programRunning;

void *gpu_polling_thread(void*)
{
  uint64_t lastNewFrameReceivedTime = tick();
  while(programRunning)
  {
#ifdef SAVE_BATTERY_BY_SLEEPING_UNTIL_TARGET_FRAME
    const int64_t earlyFramePrediction = 500;
    uint64_t earliestNextFrameArrivaltime = lastNewFrameReceivedTime + 1000000/TARGET_FRAME_RATE - earlyFramePrediction;
    uint64_t now = tick();
    if (earliestNextFrameArrivaltime > now)
    {
      usleep(earliestNextFrameArrivaltime - now);
    }
#endif

#if defined(SAVE_BATTERY_BY_PREDICTING_FRAME_ARRIVAL_TIMES) || defined(SAVE_BATTERY_BY_SLEEPING_WHEN_IDLE)
    uint64_t nextFrameArrivalTime = PredictNextFrameArrivalTime();
    int64_t timeToSleep = nextFrameArrivalTime - tick();
    const int64_t minimumSleepTime = 2500; // Don't sleep if the next frame is expected to arrive in less than this much time
    if (timeToSleep > minimumSleepTime)
    {
      usleep(timeToSleep - minimumSleepTime);
    }
#endif

    uint64_t t0 = tick();

#ifdef RANDOM_TEST_PATTERN
    // Generate random noise that updates each frame
    // uint32_t randomColor = rand() % 65536;
    static int col = 0;
    static int barY = 0;
    static uint64_t lastTestImage = tick();
    uint32_t randomColor = ((31 + ABS(col - 32)) << 5);
    now = tick();
    if (now - lastTestImage >= 1000000/RANDOM_TEST_PATTERN_FRAME_RATE)
    {
      col = (col + 2) & 31;
      lastTestImage = now;
    }
    randomColor = randomColor | (randomColor << 16);
    uint32_t *newfb = (uint32_t*)videoCoreFramebuffer[0];
    for(int y = 0; y < gpuFrameHeight; ++y)
    {
      int x = 0;
      const int XX = RANDOM_TEST_PATTERN_STRIPE_WIDTH>>1;
      while(x <= gpuFrameWidth>>1)
      {
        for(int X = 0; x+X < gpuFrameWidth>>1; ++X)
        {
          if (y == barY)
            newfb[x+X] = 0xFFFFFFFF;
          else if (y == barY+1 || y == barY-1)
            newfb[x+X] = 0;
          else
            newfb[x+X] = randomColor;
        }
        x += XX + 6;
      }
      newfb += gpuFramebufferScanlineStrideBytes>>2;
    }
    barY = (barY + 1) % gpuFrameHeight;
#else
    SnapshotFramebuffer(videoCoreFramebuffer[0]);
#endif

#ifndef USE_GPU_VSYNC
    lastFramePollTime = t0;
#endif

    // Check the pixel contents of the snapshot to see if we actually received a new frame to render
    bool gotNewFramebuffer = IsNewFramebuffer(videoCoreFramebuffer[0], videoCoreFramebuffer[1]);
    if (gotNewFramebuffer)
      lastNewFrameReceivedTime = t0;

    uint64_t t1 = tick();
    if (!gotNewFramebuffer)
    {
#ifdef STATISTICS
      __atomic_fetch_add(&timeWastedPollingGPU, t1-t0, __ATOMIC_RELAXED);
#endif
      continue;
    }
    else
    {
      memcpy(videoCoreFramebuffer[1], videoCoreFramebuffer[0], gpuFramebufferSizeBytes);
      __atomic_fetch_add(&numNewGpuFrames, 1, __ATOMIC_SEQ_CST);
      syscall(SYS_futex, &numNewGpuFrames, FUTEX_WAKE, 1, 0, 0, 0); // Wake the main thread if it was sleeping to get a new frame
    }
  }
  pthread_exit(0);
}

#endif // ~USE_GPU_VSYNC

void InitGPU()
{
  // Initialize GPU frame grabbing subsystem
  bcm_host_init();
  display = vc_dispmanx_display_open(0);
  if (!display) FATAL_ERROR("vc_dispmanx_display_open failed!");
  DISPMANX_MODEINFO_T display_info;
  int ret = vc_dispmanx_display_get_info(display, &display_info);
  if (ret) FATAL_ERROR("vc_dispmanx_display_get_info failed!");

#ifdef DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE
  // Pretend that the display framebuffer would be in portrait mode for the purposes of size computation etc.
  // Snapshotting code transposes the obtained framebuffer immediately after capture from landscape to portrait to make it so.
  SWAPU32(display_info.width, display_info.height);
  printf("DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE: Swapping width/height to update display in portrait mode to minimize tearing.\n");
#endif
  // We may need to scale the main framebuffer to fit the native pixel size of the display. Always want to do such scaling in aspect ratio fixed mode to not stretch the image.
  // (For non-square pixels or similar, could apply a correction factor here to fix aspect ratio)

  displayXOffset = 0;
  displayYOffset = 0;
  int scaledWidth = DISPLAY_DRAWABLE_WIDTH;
  int scaledHeight = DISPLAY_DRAWABLE_HEIGHT;
  double scalingFactor = 1.0;

  // Often it happens that the content that is being rendered already has black letterboxes/pillarboxes if it was produced for a different aspect ratio than
  // what the current HDMI resolution is. However the current HDMI resolution might not be in the same aspect ratio as DISPLAY_DRAWABLE_WIDTH x DISPLAY_DRAWABLE_HEIGHT.
  // Therefore we may be aspect ratio correcting content that has already letterboxes/pillarboxes on it, which can result in letterboxes-on-pillarboxes, or vice versa.

  // To enable removing the double aspect ratio correction, the following settings enable "overscan": crop left/right and top/down parts of the source image
  // to remove the letterboxed parts of the source. This overscan method can also used to crop excess edges of old emulator based games intended for analog TVs,
  // e.g. NES games often had graphical artifacts on left or right edge of the screen when the game scrolls, which usually were hidden on analog TVs with overscan.

  /* In /opt/retropie/configs/nes/retroarch.cfg, if running fceumm NES emulator, put:
      aspect_ratio_index = "22"
      custom_viewport_width = "256"
      custom_viewport_height = "224"
      custom_viewport_x = "32"
      custom_viewport_y = "8"
      (see https://github.com/RetroPie/RetroPie-Setup/wiki/Smaller-RetroArch-Screen)
    and configure /boot/config.txt to 320x240 HDMI mode to get pixel perfect rendering without blurring scaling.

    Curiously, if using quicknes emulator instead, it seems to render to a horizontally 16 pixels smaller resolution. Therefore put in
      aspect_ratio_index = "22"
      custom_viewport_width = "240"
      custom_viewport_height = "224"
      custom_viewport_x = "40"
      custom_viewport_y = "8"
    instead for pixel perfect rendering. Also in /opt/retropie/configs/all/retroarch.cfg, set

      video_fullscreen_x = "320"
      video_fullscreen_y = "240"
  */

  // The overscan values are in normalized 0.0 .. 1.0 percentages of the total width/height of the screen.
  double overscanLeft = 0.00;
  double overscanRight = 0.00;
  double overscanTop = 0.00;
  double overscanBottom = 0.00;

  // If specified, computes overscan that crops away equally much content from all sides of the source frame
  // to display the center of the source frame pixel perfect.
#ifdef DISPLAY_CROPPED_INSTEAD_OF_SCALING
  if (DISPLAY_DRAWABLE_WIDTH < display_info.width)
  {
    overscanLeft = (display_info.width - DISPLAY_DRAWABLE_WIDTH) * 0.5 / display_info.width;
    overscanRight = overscanLeft;
  }
  if (DISPLAY_DRAWABLE_HEIGHT < display_info.height)
  {
    overscanTop = (display_info.height - DISPLAY_DRAWABLE_HEIGHT) * 0.5 / display_info.height;
    overscanBottom = overscanTop;
  }
#endif

  int relevantDisplayWidth = (int)(display_info.width * (1.0 - overscanLeft - overscanRight) + 0.5);
  int relevantDisplayHeight = (int)(display_info.height * (1.0 - overscanTop - overscanBottom) + 0.5);
  printf("Relevant source display area size with overscan cropped away: %dx%d.\n", relevantDisplayWidth, relevantDisplayHeight);

  if (DISPLAY_DRAWABLE_WIDTH * relevantDisplayHeight < DISPLAY_DRAWABLE_HEIGHT * relevantDisplayWidth)
  {
    scaledHeight = (int)((double)DISPLAY_DRAWABLE_WIDTH * relevantDisplayHeight / relevantDisplayWidth + 0.5);
    scalingFactor = (double)DISPLAY_DRAWABLE_WIDTH/relevantDisplayWidth;
    displayXOffset = DISPLAY_COVERED_LEFT_SIDE;
    displayYOffset = DISPLAY_COVERED_TOP_SIDE + (DISPLAY_DRAWABLE_HEIGHT - scaledHeight) / 2;
  }
  else
  {
    scaledWidth = (int)((double)DISPLAY_DRAWABLE_HEIGHT * relevantDisplayWidth / relevantDisplayHeight + 0.5);
    scalingFactor = (double)DISPLAY_DRAWABLE_HEIGHT/relevantDisplayHeight;
    displayXOffset = DISPLAY_COVERED_LEFT_SIDE + (DISPLAY_DRAWABLE_WIDTH - scaledWidth) / 2;
    displayYOffset = DISPLAY_COVERED_TOP_SIDE;
  }

  excessPixelsLeft = (int)(display_info.width * overscanLeft * scalingFactor + 0.5);
  excessPixelsRight = (int)(display_info.width * overscanRight * scalingFactor + 0.5);
  excessPixelsTop = (int)(display_info.height * overscanTop * scalingFactor + 0.5);
  excessPixelsBottom = (int)(display_info.height * overscanBottom * scalingFactor + 0.5);

  gpuFrameWidth = scaledWidth;
  gpuFrameHeight = scaledHeight;
  gpuFramebufferScanlineStrideBytes = RoundUpToMultipleOf((gpuFrameWidth + excessPixelsLeft + excessPixelsRight) * 2, 32);
  gpuFramebufferSizeBytes = gpuFramebufferScanlineStrideBytes * (gpuFrameHeight + excessPixelsTop + excessPixelsBottom);

  // BUG in vc_dispmanx_resource_read_data(!!): If one is capturing a small subrectangle of a large screen resource rectangle, the destination pointer 
  // is in vc_dispmanx_resource_read_data() incorrectly still taken to point to the top-left corner of the large screen resource, instead of the top-left
  // corner of the subrectangle to capture. Therefore do dirty pointer arithmetic to adjust for this. To make this safe, videoCoreFramebuffer is allocated
  // double its needed size so that this adjusted pointer does not reference outside allocated memory (if it did, vc_dispmanx_resource_read_data() was seen
  // to randomly fail and then subsequently hang if called a second time)
  videoCoreFramebuffer[0] = (uint16_t *)malloc(gpuFramebufferSizeBytes*2);
  videoCoreFramebuffer[1] = (uint16_t *)malloc(gpuFramebufferSizeBytes*2);
  memset(videoCoreFramebuffer[0], 0, gpuFramebufferSizeBytes*2);
  memset(videoCoreFramebuffer[1], 0, gpuFramebufferSizeBytes*2);
  videoCoreFramebuffer[0] += (gpuFramebufferSizeBytes>>1);
  videoCoreFramebuffer[1] += (gpuFramebufferSizeBytes>>1);

  syslog(LOG_INFO, "GPU display is %dx%d. SPI display is %dx%d with drawable area of %dx%d. Applying scaling factor %.2fx, xOffset: %d, yOffset: %d, scaledWidth: %d, scaledHeight: %d", display_info.width, display_info.height, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_DRAWABLE_WIDTH, DISPLAY_DRAWABLE_HEIGHT, scalingFactor, displayXOffset, displayYOffset, scaledWidth, scaledHeight);
  printf("Source GPU display is %dx%d. Output SPI display is %dx%d with a drawable area of %dx%d. Applying scaling factor %.2fx, xOffset: %d, yOffset: %d, scaledWidth: %d, scaledHeight: %d\n", display_info.width, display_info.height, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_DRAWABLE_WIDTH, DISPLAY_DRAWABLE_HEIGHT, scalingFactor, displayXOffset, displayYOffset, scaledWidth, scaledHeight);

  uint32_t image_prt;
  printf("Creating dispmanX resource of size %dx%d (aspect ratio=%f).\n", scaledWidth + excessPixelsLeft + excessPixelsRight, scaledHeight + excessPixelsTop + excessPixelsBottom, (double)(scaledWidth + excessPixelsLeft + excessPixelsRight) / (scaledHeight + excessPixelsTop + excessPixelsBottom));
#ifdef DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE
  screen_resource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, scaledHeight + excessPixelsTop + excessPixelsBottom, scaledWidth + excessPixelsLeft + excessPixelsRight, &image_prt);
  vc_dispmanx_rect_set(&rect, excessPixelsTop, excessPixelsLeft, scaledHeight, scaledWidth);
#else
  screen_resource = vc_dispmanx_resource_create(VC_IMAGE_RGB565, scaledWidth + excessPixelsLeft + excessPixelsRight, scaledHeight + excessPixelsTop + excessPixelsBottom, &image_prt);
  vc_dispmanx_rect_set(&rect, excessPixelsLeft, excessPixelsTop, scaledWidth, scaledHeight);
#endif
  if (!screen_resource) FATAL_ERROR("vc_dispmanx_resource_create failed!");
  printf("GPU grab rectangle is offset x=%d,y=%d, size w=%dxh=%d, aspect ratio=%f\n", excessPixelsLeft, excessPixelsTop, scaledWidth, scaledHeight, (double)scaledWidth / scaledHeight);

#ifdef USE_GPU_VSYNC
  // Register to receive vsync notifications. This is a heuristic, since the application might not be locked at vsync, and even
  // if it was, this signal is not a guaranteed edge trigger for availability of new frames.
  vc_dispmanx_vsync_callback(display, VsyncCallback, 0);
#else
  int rc = pthread_create(&gpuPollingThread, NULL, gpu_polling_thread, NULL); // After creating the thread, it is assumed to have ownership of the SPI bus, so no SPI chat on the main thread after this.
  if (rc != 0) FATAL_ERROR("Failed to create GPU polling thread!");
#endif
}

void DeinitGPU()
{
#ifdef USE_GPU_VSYNC
  if (display) vc_dispmanx_vsync_callback(display, NULL, 0);
#else
  pthread_join(gpuPollingThread, NULL);
  gpuPollingThread = (pthread_t)0;
#endif

  if (screen_resource)
  {
    vc_dispmanx_resource_delete(screen_resource);
    screen_resource = 0;
  }

  if (display)
  {
    vc_dispmanx_display_close(display);
    display = 0;
  }

  bcm_host_deinit();
}
