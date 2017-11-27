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

#include <math.h>

// Spans track dirty rectangular areas on screen
struct Span
{
  uint16_t x, endX, y, endY, lastScanEndX, size; // Specifies a box of width [x, endX[ * [y, endY[, where scanline endY-1 can be partial, and ends in lastScanEndX.
  Span *next; // Maintain a linked skip list inside the array for fast seek to next active element when pruning
};
Span spans[DISPLAY_WIDTH*DISPLAY_HEIGHT/2];

int main()
{
  InitSPI();

  // Track current SPI display controller write X and Y cursors.
  int spiX = 0;
  int spiY = 0;
  int spiEndX = DISPLAY_WIDTH;

  uint16_t *framebuffer[2] = { (uint16_t *)malloc(FRAMEBUFFER_SIZE), (uint16_t *)malloc(FRAMEBUFFER_SIZE) };
  memset(framebuffer[0], 0, FRAMEBUFFER_SIZE); // Doublebuffer received GPU memory contents, first buffer contains current GPU memory,
  memset(framebuffer[1], 0, FRAMEBUFFER_SIZE); // second buffer contains whatever the display is currently showing. This allows diffing pixels between the two.

  uint16_t *gpuFramebuffer = (uint16_t *)malloc(FRAMEBUFFER_SIZE);
  memset(gpuFramebuffer, 0, FRAMEBUFFER_SIZE); // third buffer contains last seen GPU memory contents, used to compare polled frames to whether they actually have changed.

  InitGPU();

  uint32_t curFrameEnd = queueTail;
  uint32_t prevFrameEnd = queueTail;

  bool prevFrameWasInterlacedUpdate = false;
  bool interlacedUpdate = false; // True if the previous update we did was an interlaced half field update.
  int frameParity = 0; // For interlaced frame updates, this is either 0 or 1 to denote evens or odds.
  for(;;)
  {
    prevFrameWasInterlacedUpdate = interlacedUpdate;

#ifndef THROTTLE_INTERLACING
    if (!prevFrameWasInterlacedUpdate)
#endif
      while(__atomic_load_n(&numNewGpuFrames, __ATOMIC_SEQ_CST) == 0)
      {
        PollHardwareInfo(); // This is a good time to update new hardware stats, since we are going to sleep anyways.
        syscall(SYS_futex, &numNewGpuFrames, FUTEX_WAIT, 0, 0, 0, 0); // Start sleeping until we get new tasks
      }

    bool spiThreadWasWorkingHardBefore = false;
    uint64_t detectTime = tick();
    uint32_t bytesInQueueThen = spiBytesQueued;
    uint32_t tasksInQueueThen = (queueTail + SPI_QUEUE_LENGTH - queueHead) % SPI_QUEUE_LENGTH;

    int oldFrameSize = tasksInQueueThen - (queueTail + SPI_QUEUE_LENGTH - prevFrameEnd) % SPI_QUEUE_LENGTH; 

    // At all times keep at most two rendered frames in the SPI task queue pending to be displayed. Only proceed to submit a new frame
    // once the older of those has been displayed.
    bool once = true;
    while ((queueTail + SPI_QUEUE_LENGTH - queueHead) % SPI_QUEUE_LENGTH > (queueTail + SPI_QUEUE_LENGTH - prevFrameEnd) % SPI_QUEUE_LENGTH)
    {
      PollHardwareInfo(); // This is a good time to update new hardware stats, since we are going to sleep anyways.

      if (spiBytesQueued > 10000)
        spiThreadWasWorkingHardBefore = true; // SPI thread had too much work in queue atm (2 full frames)

      // Peek at the SPI thread's workload and throttle a bit if it has got a lot of work still to do.
      double usecsUntilSpiQueueEmpty = spiBytesQueued*spiUsecsPerByte;
      if (usecsUntilSpiQueueEmpty > 0)
      {
        uint32_t bytesInQueueBefore = spiBytesQueued;
        uint32_t tasksInQueueBefore = (queueTail + SPI_QUEUE_LENGTH - queueHead) % SPI_QUEUE_LENGTH;
        uint32_t sleepUsecs = (uint32_t)(usecsUntilSpiQueueEmpty*0.4);
#ifdef STATISTICS
        uint64_t t0 = tick();
#endif
        if (sleepUsecs > 1000) usleep(500);

#ifdef STATISTICS
        uint64_t t1 = tick();
        uint32_t bytesInQueueAfter = spiBytesQueued;
        uint32_t tasksInQueueAfter = (queueTail + SPI_QUEUE_LENGTH - queueHead) % SPI_QUEUE_LENGTH;
        bool starved = (queueHead == queueTail);
        if (starved) spiThreadWasWorkingHardBefore = false;

        if (once && starved)
        {
          printf("Had %u bytes/%u tasks in queue, asked to sleep for %u usecs, got %u usecs sleep, afterwards %u bytes/%u tasks in queue. (got %.2f%% work done)%s\n",
            bytesInQueueBefore, tasksInQueueBefore, sleepUsecs, (uint32_t)(t1 - t0), bytesInQueueAfter, tasksInQueueAfter, (bytesInQueueBefore-bytesInQueueAfter)*100.0/bytesInQueueBefore,
            starved ? "  SLEPT TOO LONG, SPI THREAD STARVED" : "");
          once = false;
        }
#endif
      }
    }

    uint64_t detectTimeNoSleep = tick();
    uint32_t tasksInQueueAfterSleep = (queueTail + SPI_QUEUE_LENGTH - queueHead) % SPI_QUEUE_LENGTH;

    uint64_t tFrameStart = tick();

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
    while(expiredSkippedFrames < frameSkipTimeHistorySize && now - frameSkipTimeHistory[expiredSkippedFrames] >= FRAMERATE_HISTORY_LENGTH) ++expiredSkippedFrames;
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
      memcpy(framebuffer[0], videoCoreFramebuffer[0], FRAMEBUFFER_SIZE);
#ifdef STATISTICS
      for(int i = 0; i < numNewFrames - 1 && frameSkipTimeHistorySize < FRAMERATE_HISTORY_LENGTH; ++i)
        frameSkipTimeHistory[frameSkipTimeHistorySize++] = now;
#endif
      __atomic_fetch_sub(&numNewGpuFrames, numNewFrames, __ATOMIC_SEQ_CST);
    }

    if (gotNewFramebuffer)
    {
      RefreshStatisticsOverlayText();
      DrawStatisticsOverlay(framebuffer[0]);
      AddHistogramSample();
      memcpy(gpuFramebuffer, framebuffer[0], FRAMEBUFFER_SIZE);
    }

    // Count how many pixels overall have changed on the new GPU frame, compared to what is being displayed on the SPI screen.
    uint16_t *scanline = framebuffer[0];
    uint16_t *prevScanline = framebuffer[1];
    int changedPixels = 0;
    for(int i = 0; i < DISPLAY_WIDTH*DISPLAY_HEIGHT; ++i)
      if (*scanline++ != *prevScanline++)
        ++changedPixels;

    // If too many pixels have changed on screen, drop adaptively to interlaced updating to keep up the frame rate.
    double inputDataFps = 1000000.0 / EstimateFrameRateInterval();
    double desiredTargetFps = MAX(1, MIN(inputDataFps, TARGET_FRAME_RATE));
    const double tooMuchToUpdateUsecs = 1000000 / desiredTargetFps * 4 / 5; // Use a rather arbitrary 4/5ths heuristic as an estimate of too much workload.
    if (gotNewFramebuffer) prevFrameWasInterlacedUpdate = false; // If we receive a new frame from the GPU, forget that previous frame was interlaced to count this frame as fully progressive in statistics.
#ifdef NO_INTERLACING
    interlacedUpdate = false;
#elif defined(ALWAYS_INTERLACING)
    interlacedUpdate = (changedPixels > 0);
#else
    uint32_t bytesToSend = changedPixels * DISPLAY_BYTESPERPIXEL + (DISPLAY_WIDTH+DISPLAY_HEIGHT*4);
    interlacedUpdate = ((bytesToSend + spiBytesQueued) * spiUsecsPerByte > tooMuchToUpdateUsecs); // Decide whether to do interlacedUpdate - only updates half of the screen
#endif

    if (interlacedUpdate) frameParity = 1-frameParity; // Swap even-odd fields every second time we do an interlaced update (progressive updates ignore field order)
    int y = interlacedUpdate ? frameParity : 0;
    scanline = framebuffer[0] + y*DISPLAY_WIDTH;
    prevScanline = framebuffer[1] + y*DISPLAY_WIDTH;

    int bytesTransferred = 0;

    // Collect all spans in this image
    int numSpans = 0;
    Span *head = 0;
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
        else head = &spans[0];
        spans[numSpans++].next = 0;
        x = endX;
      }

      // If doing an interlaced update, skip over every second scanline.
      if (interlacedUpdate) ++y, scanline += DISPLAY_WIDTH, prevScanline += DISPLAY_WIDTH;
    }

    // Merge spans together on the same scanline
    for(Span *i = head; i; i = i->next)
      for(Span *j = i->next; j; j = j->next)
      {
        if (j->y != i->y) break; // On the next scanline?

        int newSize = j->endX-i->x;
        int wastedPixels = newSize - i->size - j->size;

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
#define SPAN_MERGE_THRESHOLD 4

        if (wastedPixels > SPAN_MERGE_THRESHOLD) break; // Too far away?

        i->endX = j->endX;
        i->lastScanEndX = j->endX;
        i->size = newSize;
        i->next = j->next;
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
    for(Span *i = head; i; i = i->next)
    {
      // Update the write cursor if needed
      if (spiY != i->y)
      {
        QUEUE_MOVE_CURSOR_TASK(DISPLAY_SET_CURSOR_Y, displayYOffset + i->y);
        spiY = i->y;
      }

      if (i->endY > i->y + 1 && (spiX != i->x || spiEndX != i->endX)) // Multiline span?
      {
        QUEUE_SET_X_WINDOW_TASK(i->x, displayXOffset + i->endX - 1);
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
            if (j->endY > j->y+1)
            {
              if (j->endX >= i->endX) nextEndX = j->endX;
              break;
            }
          QUEUE_SET_X_WINDOW_TASK(i->x, displayXOffset + nextEndX - 1);
          spiX = i->x;
          spiEndX = nextEndX;
        }
        else if (spiX != i->x)
        {
          QUEUE_MOVE_CURSOR_TASK(DISPLAY_SET_CURSOR_X, displayXOffset + i->x);
          spiX = i->x;
        }
      }

      // Submit the span pixels
      SPITask *task = AllocTask();
      task->cmd = DISPLAY_WRITE_PIXELS;
      task->bytes = i->size*DISPLAY_BYTESPERPIXEL;

      bytesTransferred += task->bytes+1;
      uint16_t *scanline = framebuffer[0] + i->y * DISPLAY_WIDTH;
      uint16_t *prevScanline = framebuffer[1] + i->y * DISPLAY_WIDTH;
      uint16_t *data = (uint16_t*)task->data;
      for(int y = i->y; y < i->endY; ++y, scanline += DISPLAY_WIDTH, prevScanline += DISPLAY_WIDTH)
      {
        int endX = (y + 1 == i->endY) ? i->lastScanEndX : i->endX;
        for(int x = i->x; x < endX; ++x) *data++ = __builtin_bswap16(scanline[x]); // Write out the RGB565 data, swapping to big endian byte order for the SPI bus
        memcpy(prevScanline+i->x, scanline+i->x, (endX - i->x)*DISPLAY_BYTESPERPIXEL);
      }
      CommitTask();
    }

    // Remember where in the command queue this frame ends, to keep track of the SPI thread's progress over it
    if (bytesTransferred > 0)
    {
      prevFrameEnd = curFrameEnd;
      curFrameEnd = queueTail;
    }

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
