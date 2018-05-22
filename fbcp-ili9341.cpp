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
#include <inttypes.h>

#include "config.h"
#include "text.h"
#include "spi.h"
#include "gpu.h"
#include "statistics.h"
#include "tick.h"
#include "display.h"
#include "util.h"
#include "mailbox.h"

#include <math.h>

// Spans track dirty rectangular areas on screen
struct Span
{
  uint16_t x, endX, y, endY, lastScanEndX, size; // Specifies a box of width [x, endX[ * [y, endY[, where scanline endY-1 can be partial, and ends in lastScanEndX.
  Span *next; // Maintain a linked skip list inside the array for fast seek to next active element when pruning
};
Span *spans = 0;

int CountNumChangedPixels(uint16_t *framebuffer, uint16_t *prevFramebuffer)
{
  int changedPixels = 0;
  for(int y = 0; y < gpuFrameHeight; ++y)
  {
    for(int x = 0; x < gpuFrameWidth; ++x)
      if (framebuffer[x] != prevFramebuffer[x])
        ++changedPixels;

    framebuffer += gpuFramebufferScanlineStrideBytes >> 1;
    prevFramebuffer += gpuFramebufferScanlineStrideBytes >> 1;
  }
  return changedPixels;
}

uint64_t displayContentsLastChanged = 0;
bool displayOff = false;

int main()
{
#ifdef RUN_WITH_REALTIME_THREAD_PRIORITY
  SetRealtimeThreadPriority();
#endif
  OpenMailbox();
  InitSPI();
  displayContentsLastChanged = tick();
  displayOff = false;

  // Track current SPI display controller write X and Y cursors.
  int spiX = -1;
  int spiY = -1;
  int spiEndX = DISPLAY_WIDTH;

  InitGPU();

  spans = (Span*)malloc((gpuFrameWidth * gpuFrameHeight / 2) * sizeof(Span));
  int size = gpuFramebufferSizeBytes;
#ifdef USE_GPU_VSYNC
  // BUG in vc_dispmanx_resource_read_data(!!): If one is capturing a small subrectangle of a large screen resource rectangle, the destination pointer 
  // is in vc_dispmanx_resource_read_data() incorrectly still taken to point to the top-left corner of the large screen resource, instead of the top-left
  // corner of the subrectangle to capture. Therefore do dirty pointer arithmetic to adjust for this. To make this safe, videoCoreFramebuffer is allocated
  // double its needed size so that this adjusted pointer does not reference outside allocated memory (if it did, vc_dispmanx_resource_read_data() was seen
  // to randomly fail and then subsequently hang if called a second time)
  size *= 2;
#endif
  uint16_t *framebuffer[2] = { (uint16_t *)malloc(size), (uint16_t *)malloc(gpuFramebufferSizeBytes) };
  memset(framebuffer[0], 0, size); // Doublebuffer received GPU memory contents, first buffer contains current GPU memory,
  memset(framebuffer[1], 0, gpuFramebufferSizeBytes); // second buffer contains whatever the display is currently showing. This allows diffing pixels between the two.
#ifdef USE_GPU_VSYNC
  // Due to the above bug. In USE_GPU_VSYNC mode, we directly snapshot to framebuffer[0], so it has to be prepared specially to work around the
  // dispmanx bug.
  framebuffer[0] += (gpuFramebufferSizeBytes>>1);
#endif

  uint32_t curFrameEnd = spiTaskMemory->queueTail;
  uint32_t prevFrameEnd = spiTaskMemory->queueTail;

  bool prevFrameWasInterlacedUpdate = false;
  bool interlacedUpdate = false; // True if the previous update we did was an interlaced half field update.
  int frameParity = 0; // For interlaced frame updates, this is either 0 or 1 to denote evens or odds.
  for(;;)
  {
    prevFrameWasInterlacedUpdate = interlacedUpdate;

    // If last update was interlaced, it means we still have half of the image pending to be updated. In such a case,
    // sleep only until when we expect the next new frame of data to appear, and then continue independent of whether
    // a new frame was produced or not - if not, then we will submit the rest of the unsubmitted fields. If yes, then
    // the half fields of the new frame will be sent (or full, if the new frame has very little content)
    if (prevFrameWasInterlacedUpdate)
    {
#ifdef THROTTLE_INTERLACING
      timespec timeout = {};
      timeout.tv_nsec = 1000 * MIN(1000000, MAX(1, 750/*0.75ms extra sleep so we know we should likely sleep long enough to see the next frame*/ + PredictNextFrameArrivalTime() - tick()));
      syscall(SYS_futex, &numNewGpuFrames, FUTEX_WAIT, 0, &timeout, 0, 0); // Start sleeping until we get new tasks
#endif
      // If THROTTLE_INTERLACING is not defined, we'll fall right through and immediately submit the rest of the remaining content on screen to attempt to minimize the visual
      // observable effect of interlacing, although at the expense of smooth animation (falling through here causes jitter)
    }
    else
    {
      uint64_t waitStart = tick();
      while(__atomic_load_n(&numNewGpuFrames, __ATOMIC_SEQ_CST) == 0)
      {
#ifdef TURN_DISPLAY_OFF_AFTER_USECS_OF_INACTIVITY
        if (!displayOff && tick() - waitStart > TURN_DISPLAY_OFF_AFTER_USECS_OF_INACTIVITY)
        {
          TurnDisplayOff();
          displayOff = true;
        }

        if (!displayOff)
        {
          timespec timeout = {};
          timeout.tv_sec = ((uint64_t)TURN_DISPLAY_OFF_AFTER_USECS_OF_INACTIVITY * 1000) / 1000000000;
          timeout.tv_nsec = ((uint64_t)TURN_DISPLAY_OFF_AFTER_USECS_OF_INACTIVITY * 1000) % 1000000000;
          syscall(SYS_futex, &numNewGpuFrames, FUTEX_WAIT, 0, &timeout, 0, 0); // Sleep until the next frame arrives
        }
        else
#endif
          syscall(SYS_futex, &numNewGpuFrames, FUTEX_WAIT, 0, 0, 0, 0); // Sleep until the next frame arrives
      }
    }

    bool spiThreadWasWorkingHardBefore = false;

    // At all times keep at most two rendered frames in the SPI task queue pending to be displayed. Only proceed to submit a new frame
    // once the older of those has been displayed.
    bool once = true;
    while ((spiTaskMemory->queueTail + SPI_QUEUE_SIZE - spiTaskMemory->queueHead) % SPI_QUEUE_SIZE > (spiTaskMemory->queueTail + SPI_QUEUE_SIZE - prevFrameEnd) % SPI_QUEUE_SIZE)
    {
      if (spiTaskMemory->spiBytesQueued > 10000)
        spiThreadWasWorkingHardBefore = true; // SPI thread had too much work in queue atm (2 full frames)

      // Peek at the SPI thread's workload and throttle a bit if it has got a lot of work still to do.
      double usecsUntilSpiQueueEmpty = spiTaskMemory->spiBytesQueued*spiUsecsPerByte;
      if (usecsUntilSpiQueueEmpty > 0)
      {
        uint32_t bytesInQueueBefore = spiTaskMemory->spiBytesQueued;
        uint32_t sleepUsecs = (uint32_t)(usecsUntilSpiQueueEmpty*0.4);
#ifdef STATISTICS
        uint64_t t0 = tick();
#endif
        if (sleepUsecs > 1000) usleep(500);

#ifdef STATISTICS
        uint64_t t1 = tick();
        uint32_t bytesInQueueAfter = spiTaskMemory->spiBytesQueued;
        bool starved = (spiTaskMemory->queueHead == spiTaskMemory->queueTail);
        if (starved) spiThreadWasWorkingHardBefore = false;

/*
        if (once && starved)
        {
          printf("Had %u bytes in queue, asked to sleep for %u usecs, got %u usecs sleep, afterwards %u bytes in queue. (got %.2f%% work done)%s\n",
            bytesInQueueBefore, sleepUsecs, (uint32_t)(t1 - t0), bytesInQueueAfter, (bytesInQueueBefore-bytesInQueueAfter)*100.0/bytesInQueueBefore,
            starved ? "  SLEPT TOO LONG, SPI THREAD STARVED" : "");
          once = false;
        }
*/
#endif
      }
    }

    int expiredFrames = 0;
    uint64_t now = tick();
    while(expiredFrames < frameTimeHistorySize && now - frameTimeHistory[expiredFrames].time >= FRAMERATE_HISTORY_LENGTH) ++expiredFrames;
    if (expiredFrames > 0)
    {
      frameTimeHistorySize -= expiredFrames;
      for(int i = 0; i < frameTimeHistorySize; ++i) frameTimeHistory[i] = frameTimeHistory[i+expiredFrames];
    }

#ifdef STATISTICS
    int expiredSkippedFrames = 0;
    while(expiredSkippedFrames < frameSkipTimeHistorySize && now - frameSkipTimeHistory[expiredSkippedFrames] >= 1000000/*FRAMERATE_HISTORY_LENGTH*/) ++expiredSkippedFrames;
    if (expiredSkippedFrames > 0)
    {
      frameSkipTimeHistorySize -= expiredSkippedFrames;
      for(int i = 0; i < frameSkipTimeHistorySize; ++i) frameSkipTimeHistory[i] = frameSkipTimeHistory[i+expiredSkippedFrames];
    }
#endif

    int numNewFrames = __atomic_load_n(&numNewGpuFrames, __ATOMIC_SEQ_CST);
    bool gotNewFramebuffer = (numNewFrames > 0);
    if (gotNewFramebuffer)
    {
#ifdef USE_GPU_VSYNC
      static uint64_t lastFrameObtainedTime = 0;
      // TODO: Hardcoded vsync interval to 60 for now. Would be better to compute yet another histogram of the vsync arrival times, if vsync is not set to 60hz.
      uint64_t earliestNextFrameArrivalTime = lastFrameObtainedTime + 1000000/60;
      uint64_t now = tick();
      uint64_t framePollingStartTime = now;
      while(now < earliestNextFrameArrivalTime)
      {
        if (earliestNextFrameArrivalTime - now > 70)
          usleep(earliestNextFrameArrivalTime - now - 70);
        now = tick();
      }
      // N.B. copying directly to videoCoreFramebuffer[1] that may be directly accessed by the main thread, so this could
      // produce a visible tear between two adjacent frames, but since we don't have vsync anyways, currently not caring too much.

      uint64_t frameObtainedTime = tick();
      SnapshotFramebuffer(framebuffer[0]);
#else
      memcpy(framebuffer[0], videoCoreFramebuffer[1], gpuFramebufferSizeBytes);
#endif

#ifdef STATISTICS
      for(int i = 0; i < numNewFrames - 1 && frameSkipTimeHistorySize < FRAMERATE_HISTORY_LENGTH; ++i)
        frameSkipTimeHistory[frameSkipTimeHistorySize++] = now;
#endif
      __atomic_fetch_sub(&numNewGpuFrames, numNewFrames, __ATOMIC_SEQ_CST);

      DrawStatisticsOverlay(framebuffer[0]);

#ifdef USE_GPU_VSYNC

#ifdef STATISTICS
      uint64_t completelyUnnecessaryTimeWastedPollingGPUStart = tick();
#endif

      // DispmanX PROBLEM! When latching onto the vsync signal, the DispmanX API sends the signal at arbitrary phase with respect to the application actually producing its frames.
      // Therefore even while we do get a smooth 16.666.. msec interval vsync signal, we have no idea whether the application has actually produced a new frame at that time. Therefore
      // we must keep polling for frames until we find one that it has produced.
      bool isNewFramebuffer = IsNewFramebuffer(framebuffer[0], framebuffer[1]);
      uint64_t timeToGiveUpThereIsNotGoingToBeANewFrame = framePollingStartTime + 1000000/TARGET_FRAME_RATE/2;
      while(!isNewFramebuffer && tick() < timeToGiveUpThereIsNotGoingToBeANewFrame)
      {
        usleep(200);
        frameObtainedTime = tick();
        SnapshotFramebuffer(framebuffer[0]);
        DrawStatisticsOverlay(framebuffer[0]);
        isNewFramebuffer = IsNewFramebuffer(framebuffer[0], framebuffer[1]);
      }

      if (isNewFramebuffer && !displayOff)
        RefreshStatisticsOverlayText();

      numNewFrames = __atomic_load_n(&numNewGpuFrames, __ATOMIC_SEQ_CST);
#ifdef STATISTICS
      for(int i = 0; i < numNewFrames - 1 && frameSkipTimeHistorySize < FRAMERATE_HISTORY_LENGTH; ++i)
        frameSkipTimeHistory[frameSkipTimeHistorySize++] = now;
#endif
      __atomic_fetch_sub(&numNewGpuFrames, numNewFrames, __ATOMIC_SEQ_CST);

#ifdef STATISTICS
      uint64_t completelyUnnecessaryTimeWastedPollingGPUStop = tick();
      __atomic_fetch_add(&timeWastedPollingGPU, completelyUnnecessaryTimeWastedPollingGPUStop-completelyUnnecessaryTimeWastedPollingGPUStart, __ATOMIC_RELAXED);
#endif

#endif

#ifndef USE_GPU_VSYNC
      AddHistogramSample();
#else
      lastFrameObtainedTime = frameObtainedTime;
#endif
    }

    // If too many pixels have changed on screen, drop adaptively to interlaced updating to keep up the frame rate.
    double inputDataFps = 1000000.0 / EstimateFrameRateInterval();
    double desiredTargetFps = MAX(1, MIN(inputDataFps, TARGET_FRAME_RATE));
#ifdef PI_ZERO
    const double timesliceToUseForScreenUpdates = 250000;
#elif defined(ILI9486)
    const double timesliceToUseForScreenUpdates = 750000;
#else
    const double timesliceToUseForScreenUpdates = 1500000;
#endif
    const double tooMuchToUpdateUsecs = timesliceToUseForScreenUpdates / desiredTargetFps; // If updating the current and new frame takes too many frames worth of allotted time, drop to interlacing.
    //if (gotNewFramebuffer) prevFrameWasInterlacedUpdate = false; // If we receive a new frame from the GPU, forget that previous frame was interlaced to count this frame as fully progressive in statistics.

#if !defined(NO_INTERLACING) || defined(TURN_DISPLAY_OFF_AFTER_USECS_OF_INACTIVITY)
    int numChangedPixels = CountNumChangedPixels(framebuffer[0], framebuffer[1]);
#endif

#ifdef NO_INTERLACING
    interlacedUpdate = false;
#elif defined(ALWAYS_INTERLACING)
    interlacedUpdate = (numChangedPixels > 0);
#else
    uint32_t bytesToSend = numChangedPixels * DISPLAY_BYTESPERPIXEL + (DISPLAY_DRAWABLE_HEIGHT<<1);
    interlacedUpdate = ((bytesToSend + spiTaskMemory->spiBytesQueued) * spiUsecsPerByte > tooMuchToUpdateUsecs); // Decide whether to do interlacedUpdate - only updates half of the screen
#endif

    if (interlacedUpdate) frameParity = 1-frameParity; // Swap even-odd fields every second time we do an interlaced update (progressive updates ignore field order)
    int y = interlacedUpdate ? frameParity : 0;
    uint16_t *scanline = framebuffer[0] + y*(gpuFramebufferScanlineStrideBytes>>1);
    uint16_t *prevScanline = framebuffer[1] + y*(gpuFramebufferScanlineStrideBytes>>1); // (same scanline from previous frame, not preceding scanline)

    int bytesTransferred = 0;

    // Looking at SPI communication in a logic analyzer, it is observed that waiting for the finish of an SPI command FIFO causes pretty exactly one byte of delay to the command stream.
    // Therefore the time/bandwidth cost of ending the current span and starting a new span is as follows:
    // 1 byte to wait for the current SPI FIFO batch to finish,
    // +1 byte to send the cursor X coordinate change command,
    // +1 byte to wait for that FIFO to flush,
    // +2 bytes to send the new X coordinate,
    // +1 byte to wait for the FIFO to flush again,
    // +1 byte to send the data_write command,
    // +1 byte to wait for that FIFO to flush,
    // after which the communication is ready to start pushing pixels. This totals to 8 bytes, or 4 pixels, meaning that if there are 4 unchanged pixels or less between two adjacent dirty
    // spans, it is all the same to just update through those pixels as well to not have to wait to flush the FIFO.
#if defined(ALL_TASKS_SHOULD_DMA)
#define SPAN_MERGE_THRESHOLD 320
#elif defined(ILI9486)
#define SPAN_MERGE_THRESHOLD 10
#else
#define SPAN_MERGE_THRESHOLD 4
#endif

    // If doing an interlaced update, skip over every second scanline.
    int scanlineInc = interlacedUpdate ? gpuFramebufferScanlineStrideBytes : (gpuFramebufferScanlineStrideBytes>>1);
    int yInc = interlacedUpdate ? 2 : 1;

    int scanlineEndInc = scanlineInc - gpuFrameWidth;

    int numSpans = 0;
    Span *head = 0;

    // Collect all spans in this image
    while(y < gpuFrameHeight)
    {
      uint16_t *scanlineStart = scanline;
      uint16_t *scanlineEnd = scanline + gpuFrameWidth;
      while(scanline < scanlineEnd)
      {
        uint32_t diff;
        uint16_t *spanStart;
        uint16_t *spanEnd;
        int numConsecutiveUnchangedPixels = 0;

        if (scanline + 1 < scanlineEnd)
        {
          diff = (*(uint32_t *)scanline) ^ (*(uint32_t *)prevScanline);
          scanline += 2;
          prevScanline += 2;

          if (diff == 0) // Both 1st and 2nd pixels are the same
            continue;

          if (diff & 0xFFFF == 0) // 1st pixels are the same, 2nd pixels are not
          {
            spanStart = scanline - 1;
            spanEnd = scanline;
          }
          else // 1st pixels are different
          {
            spanStart = scanline - 2;
            if ((diff & 0xFFFF0000u) != 0) // 2nd pixels are different?
            {
              spanEnd = scanline;
            }
            else
            {
              spanEnd = scanline - 1;
              ++numConsecutiveUnchangedPixels;
            }
          }
        }
        else
        {
          if (*scanline++ == *prevScanline++)
            continue;

          spanStart = scanline - 1;
          spanEnd = scanline;
        }

        // We've found a start of a span of different pixels on this scanline, now find where this span ends
        while(scanline < scanlineEnd)
        {
          if (*scanline++ != *prevScanline++)
          {
            spanEnd = scanline;
            numConsecutiveUnchangedPixels = 0;
          }
          else
          {
            if (++numConsecutiveUnchangedPixels > SPAN_MERGE_THRESHOLD)
              break;
          }
        }

        // Submit the span update task
        Span *span = spans + numSpans;
        span->x = spanStart - scanlineStart;
        span->endX = span->lastScanEndX = spanEnd - scanlineStart;
        span->y = y;
        span->endY = y+1;
        span->size = spanEnd - spanStart;
        if (numSpans > 0) span[-1].next = span;
        else head = span;
        span->next = 0;
        ++numSpans;
      }
      y += yInc;
      scanline += scanlineEndInc;
      prevScanline += scanlineEndInc;
    }

    // Merge spans together on adjacent scanlines - works only if doing a progressive update
    if (!interlacedUpdate)
      for(Span *i = head; i; i = i->next)
      {
        Span *prev = i;
        for(Span *j = i->next; j; j = j->next)
        {
          // If the spans i and j are vertically apart, don't attempt to merge span i any further, since all spans >= j will also be farther vertically apart.
          // (the list is nondecreasing with respect to Span::y)
          if (j->y > i->endY) break;

          // Merge the spans i and j, and figure out the wastage of doing so
          int x = MIN(i->x, j->x);
          int y = MIN(i->y, j->y);
          int endX = MAX(i->endX, j->endX);
          int endY = MAX(i->endY, j->endY);
          int lastScanEndX = (endY > i->endY) ? j->lastScanEndX : ((endY > j->endY) ? i->lastScanEndX : MAX(i->lastScanEndX, j->lastScanEndX));
          int newSize = (endX-x)*(endY-y-1) + (lastScanEndX - x);
          int wastedPixels = newSize - i->size - j->size;
          if (wastedPixels <= SPAN_MERGE_THRESHOLD && newSize*DISPLAY_BYTESPERPIXEL <= MAX_SPI_TASK_SIZE)
          {
            i->x = x;
            i->y = y;
            i->endX = endX;
            i->endY = endY;
            i->lastScanEndX = lastScanEndX;
            i->size = newSize;
            prev->next = j->next;
            j = prev;
          }
          else // Not merging - travel to next node remembering where we came from
            prev = j;
        }
      }

    // Submit spans
    if (!displayOff)
    for(Span *i = head; i; i = i->next)
    {
#ifdef ALIGN_TASKS_FOR_DMA_TRANSFERS
      // DMA transfers smaller than 4 bytes are causing trouble, so in order to ensure smooth DMA operation,
      // make sure each message is at least 4 bytes in size, hence one pixel spans are forbidden:
      if (i->size == 1)
      {
        if (i->endX < DISPLAY_DRAWABLE_WIDTH) { ++i->endX; ++i->lastScanEndX; }
        else --i->x;
        ++i->size;
      }
#endif
      // Update the write cursor if needed
      if (spiY != i->y)
      {
#if defined(ILI9486) || defined(ALIGN_TASKS_FOR_DMA_TRANSFERS)
        QUEUE_SET_WRITE_WINDOW_TASK(DISPLAY_SET_CURSOR_Y, displayYOffset + i->y, displayYOffset + DISPLAY_DRAWABLE_HEIGHT - 1);
#else
        QUEUE_MOVE_CURSOR_TASK(DISPLAY_SET_CURSOR_Y, displayYOffset + i->y);
#endif
        IN_SINGLE_THREADED_MODE_RUN_TASK();
        spiY = i->y;
      }

      if (i->endY > i->y + 1 && (spiX != i->x || spiEndX != i->endX)) // Multiline span?
      {
        QUEUE_SET_WRITE_WINDOW_TASK(DISPLAY_SET_CURSOR_X, displayXOffset + i->x, displayXOffset + i->endX - 1);
        IN_SINGLE_THREADED_MODE_RUN_TASK();
        spiX = i->x;
        spiEndX = i->endX;
      }
      else // Singleline span
      {
#ifdef ALIGN_TASKS_FOR_DMA_TRANSFERS
        if (spiX != i->x || spiEndX < i->endX)
        {
          QUEUE_SET_WRITE_WINDOW_TASK(DISPLAY_SET_CURSOR_X, displayXOffset + i->x, displayXOffset + DISPLAY_DRAWABLE_WIDTH - 1);
          IN_SINGLE_THREADED_MODE_RUN_TASK();
          spiX = i->x;
          spiEndX = DISPLAY_DRAWABLE_WIDTH;
        }
#else
        if (spiEndX < i->endX) // Need to push the X end window?
        {
          // We are doing a single line span and need to increase the X window. If possible,
          // peek ahead to cater to the next multiline span update if that will be compatible.
          int nextEndX = gpuFrameWidth;
          for(Span *j = i->next; j; j = j->next)
            if (j->endY > j->y+1)
            {
              if (j->endX >= i->endX) nextEndX = j->endX;
              break;
            }
          QUEUE_SET_WRITE_WINDOW_TASK(DISPLAY_SET_CURSOR_X, displayXOffset + i->x, displayXOffset + nextEndX - 1);
          IN_SINGLE_THREADED_MODE_RUN_TASK();
          spiX = i->x;
          spiEndX = nextEndX;
        }
        else if (spiX != i->x)
        {
#ifdef ILI9486
          QUEUE_SET_WRITE_WINDOW_TASK(DISPLAY_SET_CURSOR_X, displayXOffset + i->x, displayXOffset + spiEndX - 1);
#else
          QUEUE_MOVE_CURSOR_TASK(DISPLAY_SET_CURSOR_X, displayXOffset + i->x);
#endif
          IN_SINGLE_THREADED_MODE_RUN_TASK();
          spiX = i->x;
        }
#endif
      }

      // Submit the span pixels
      SPITask *task = AllocTask(i->size*DISPLAY_BYTESPERPIXEL);
      task->cmd = DISPLAY_WRITE_PIXELS;

      bytesTransferred += task->size+1;
      uint16_t *scanline = framebuffer[0] + i->y * (gpuFramebufferScanlineStrideBytes>>1);
      uint16_t *prevScanline = framebuffer[1] + i->y * (gpuFramebufferScanlineStrideBytes>>1);
      uint16_t *data = (uint16_t*)task->data;
      for(int y = i->y; y < i->endY; ++y, scanline += gpuFramebufferScanlineStrideBytes>>1, prevScanline += gpuFramebufferScanlineStrideBytes>>1)
      {
        int endX = (y + 1 == i->endY) ? i->lastScanEndX : i->endX;
        for(int x = i->x; x < endX; ++x) *data++ = __builtin_bswap16(scanline[x]); // Write out the RGB565 data, swapping to big endian byte order for the SPI bus
        memcpy(prevScanline+i->x, scanline+i->x, (endX - i->x)*DISPLAY_BYTESPERPIXEL);
      }
      CommitTask(task);
      IN_SINGLE_THREADED_MODE_RUN_TASK();
    }

#ifdef KERNEL_MODULE_CLIENT
    // Wake the kernel module up to run tasks. TODO: This might not be best placed here, we could pre-empt
    // to start running tasks already half-way during task submission above.
    if (spiTaskMemory->queueHead != spiTaskMemory->queueTail && !(spi->cs & BCM2835_SPI0_CS_TA))
      spi->cs |= BCM2835_SPI0_CS_TA;
#endif

    // Remember where in the command queue this frame ends, to keep track of the SPI thread's progress over it
    if (bytesTransferred > 0)
    {
      prevFrameEnd = curFrameEnd;
      curFrameEnd = spiTaskMemory->queueTail;
    }

#ifdef TURN_DISPLAY_OFF_AFTER_USECS_OF_INACTIVITY
    double percentageOfScreenChanged = (double)numChangedPixels/(DISPLAY_DRAWABLE_WIDTH*DISPLAY_DRAWABLE_HEIGHT);
    if (percentageOfScreenChanged > DISPLAY_CONSIDERED_INACTIVE_PERCENTAGE)
    {
      displayContentsLastChanged = tick();
      if (displayOff)
      {
        TurnDisplayOn();
        displayOff = false;
      }
    }
    else if (!displayOff && tick() - displayContentsLastChanged > TURN_DISPLAY_OFF_AFTER_USECS_OF_INACTIVITY)
    {
      TurnDisplayOff();
      displayOff = true;
    }
#endif

#ifdef STATISTICS
    if (bytesTransferred > 0 && frameTimeHistorySize < FRAME_HISTORY_MAX_SIZE)
    {
      frameTimeHistory[frameTimeHistorySize].interlaced = interlacedUpdate || prevFrameWasInterlacedUpdate;
      frameTimeHistory[frameTimeHistorySize++].time = tick();
    }
    statsBytesTransferred += bytesTransferred;
#endif
  }

  // At exit, set all pins back to the default GPIO state (input 0x00) (we never actually reach here, since it's not possible atm to gracefully quit..)
  DeinitSPI();
}
