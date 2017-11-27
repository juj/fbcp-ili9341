#include "config.h"
#include "text.h"
#include "display.h"

void DrawText(uint16_t *framebuffer, const char *text, int x, int y, uint16_t color, uint16_t bgColor)
{
  const int Y = y;
  while(*text)
  {
    uint8_t ch = (uint8_t)*text;
    if (ch < 32 || ch >= 127) ch = 0;
    else ch -= 32;

    const int X = x;
    const int endX = x + MONACO_WIDTH;

    for(y = Y-1; y < Y + monaco_height_adjust[ch]; ++y)
      for(int x = X; x < endX+1; ++x)
      if (x >= 0 && y >= 0 && x < DISPLAY_WIDTH && y < DISPLAY_HEIGHT)
      {
        framebuffer[y*DISPLAY_WIDTH+x] = bgColor;
      }

    y = Y + monaco_height_adjust[ch];
    int yEnd = Y + MONACO_HEIGHT - 1;// - monaco_height_adjust[ch];

    const uint8_t *byte = monaco_font + ch*MONACO_BYTES_PER_CHAR;
    for(int i = 0; i < MONACO_BYTES_PER_CHAR; ++i, ++byte)
    {
      for(uint8_t bit = 1; bit; bit <<= 1)
      {
        if (x >= 0 && y >= 0 && x < DISPLAY_WIDTH && y < DISPLAY_HEIGHT)
        {
          if ((*byte & bit)) framebuffer[y*DISPLAY_WIDTH+x] = color;
          else framebuffer[y*DISPLAY_WIDTH+x] = bgColor;
        }
        ++x;
        if (x == endX)
        {
          if (x < DISPLAY_WIDTH) framebuffer[y*DISPLAY_WIDTH+x] = bgColor;
          x = X;
          ++y;
          if (y == yEnd)
          {
            i = MONACO_BYTES_PER_CHAR;
            bit = 0;
            break;
          }
        }
      }
    }
    ++text;
    x += 6;
  }
}
