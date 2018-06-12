#pragma once

#ifdef ST7735R

// On Arduino "A000096" 160x128 ST7735R LCD Screen, the following speed configurations have been tested (on a Pi 3B):
// core_freq=355: CDIV=6, results in 59.167MHz, works
// core_freq=360: CDIV=6, would result in 60.00MHz, this would work for several minutes, but then the display would turn all white at random

// Data specific to the ILI9341 controller
#define DISPLAY_BYTESPERPIXEL 2
#define DISPLAY_SET_CURSOR_X 0x2A
#define DISPLAY_SET_CURSOR_Y 0x2B
#define DISPLAY_WRITE_PIXELS 0x2C

#if defined(DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE) || !defined(DISPLAY_OUTPUT_LANDSCAPE)
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 160
#else
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 128
#endif

#define InitSPIDisplay InitST7735R

void InitST7735R(void);

void TurnDisplayOn(void);
void TurnDisplayOff(void);

#endif
