#include "config.h"
#include "diff.h"
#include "util.h"
#include "display.h"
#include "gpu.h"
#include "spi.h"

Span *spans = 0;

// Naive non-diffing functionality: just submit the whole display contents
void NoDiffChangedRectangle(Span *&head)
{
  head = spans;
  head->x = 0;
  head->endX = head->lastScanEndX = gpuFrameWidth;
  head->y = 0;
  head->endY = gpuFrameHeight;
  head->size = (head->endX-head->x)*(head->endY-head->y-1) + (head->lastScanEndX - head->x);
  head->next = 0;
}

void DiffFramebuffersToSingleChangedRectangle(uint16_t *framebuffer, uint16_t *prevFramebuffer, Span *&head)
{
  int minY = 0;
  int minX = -1;

  const int stride = gpuFramebufferScanlineStrideBytes>>1; // Stride as uint16 elements.

  uint16_t *scanline = framebuffer;
  uint16_t *prevScanline = prevFramebuffer;

  const int WidthAligned4 = (uint32_t)gpuFrameWidth & ~3u;
  while(minY < gpuFrameHeight)
  {
    int x = 0;
    // diff 4 pixels at a time
    for(; x < WidthAligned4; x += 4)
    {
      uint64_t diff = *(uint64_t*)(scanline+x) ^ *(uint64_t*)(prevScanline+x);
      if (diff)
      {
        minX = x + (__builtin_ctz(diff) >> 4);
        goto found_top;
      }
    }
    // tail unaligned 0-3 pixels one by one
    for(; x < gpuFrameWidth; ++x)
    {
      uint16_t diff = *(scanline+x) ^ *(prevScanline+x);
      if (diff)
      {
        minX = x;
        goto found_top;
      }
    }
    scanline += stride;
    prevScanline += stride;
    ++minY;
  }
  return; // No pixels changed, nothing to do.
found_top:

  scanline = framebuffer + (gpuFrameHeight - 1)*stride;
  prevScanline = prevFramebuffer + (gpuFrameHeight - 1)*stride; // (same scanline from previous frame, not preceding scanline)

  int maxX = -1;
  int maxY = gpuFrameHeight-1;
  while(maxY >= minY)
  {
    int x = gpuFrameWidth-1;
    // tail unaligned 0-3 pixels one by one
    for(; x >= WidthAligned4; --x)
    {
      if (scanline[x] != prevScanline[x])
      {
        maxX = x;
        goto found_bottom;
      }
    }
    // diff 4 pixels at a time
    x = x & ~3u;
    for(; x >= 0; x -= 4)
    {
      uint64_t diff = *(uint64_t*)(scanline+x) ^ *(uint64_t*)(prevScanline+x);
      if (diff)
      {
        maxX = x + 3 - (__builtin_clz(diff) >> 4);
        goto found_bottom;
      }
    }
    scanline -= stride;
    prevScanline -= stride;
    --maxY;
  }
found_bottom:
  scanline = framebuffer + minY*stride;
  prevScanline = prevFramebuffer + minY*stride;
  int lastScanEndX = maxX;
  if (minX > maxX) SWAPU32(minX, maxX);
  int leftX = 0;
  while(leftX < minX)
  {
    uint16_t *s = scanline + leftX;
    uint16_t *prevS = prevScanline + leftX;
    for(int y = minY; y <= maxY; ++y)
    {
      if (*s != *prevS)
        goto found_left;
      s += stride;
      prevS += stride;
    }
    ++leftX;
  }
found_left:

  int rightX = gpuFrameWidth-1;
  while(rightX > maxX)
  {
    uint16_t *s = scanline + rightX;
    uint16_t *prevS = prevScanline + rightX;
    for(int y = minY; y <= maxY; ++y)
    {
      if (*s != *prevS)
        goto found_right;
      s += stride;
      prevS += stride;
    }
    --rightX;
  }
found_right:

  head = spans;
  head->x = leftX;
  head->endX = rightX+1;
  head->lastScanEndX = lastScanEndX+1;
  head->y = minY;
  head->endY = maxY+1;
  head->size = (head->endX-head->x)*(head->endY-head->y-1) + (head->lastScanEndX - head->x);
  head->next = 0;
}

void DiffFramebuffersToScanlineSpans(uint16_t *framebuffer, uint16_t *prevFramebuffer, bool interlacedDiff, int interlacedFieldParity, Span *&head)
{
  int numSpans = 0;
  int y = interlacedDiff ? interlacedFieldParity : 0;
  int yInc = interlacedDiff ? 2 : 1;
  // If doing an interlaced update, skip over every second scanline.
  int scanlineInc = interlacedDiff ? gpuFramebufferScanlineStrideBytes : (gpuFramebufferScanlineStrideBytes>>1);
  int scanlineEndInc = scanlineInc - gpuFrameWidth;
  uint16_t *scanline = framebuffer + y*(gpuFramebufferScanlineStrideBytes>>1);
  uint16_t *prevScanline = prevFramebuffer + y*(gpuFramebufferScanlineStrideBytes>>1); // (same scanline from previous frame, not preceding scanline)

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
}

void MergeScanlineSpanList(Span *listHead)
{
  for(Span *i = listHead; i; i = i->next)
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
      if (wastedPixels <= SPAN_MERGE_THRESHOLD
#ifdef MAX_SPI_TASK_SIZE
        && newSize*SPI_BYTESPERPIXEL <= MAX_SPI_TASK_SIZE
#endif
      )
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
}
