#pragma once

#ifdef ST7735R

#ifndef SPI_BUS_CLOCK_DIVISOR
#error Please define -DSPI_BUS_CLOCK_DIVISOR=<some even number> on the CMake command line! (see file ili9341.h for details). This parameter along with core_freq=xxx in /boot/config.txt defines the SPI display speed.
#endif

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

#define DISPLAY_COVERED_LEFT_SIDE 0
#define DISPLAY_COVERED_TOP_SIDE 0
#define DISPLAY_COVERED_BOTTOM_SIDE 0

#define DISPLAY_DRAWABLE_WIDTH (DISPLAY_WIDTH-DISPLAY_COVERED_LEFT_SIDE)
#define DISPLAY_DRAWABLE_HEIGHT (DISPLAY_HEIGHT-DISPLAY_COVERED_TOP_SIDE-DISPLAY_COVERED_BOTTOM_SIDE)

#define InitSPIDisplay InitST7735R

#define DISPLAY_SCANLINE_SIZE (DISPLAY_WIDTH*DISPLAY_BYTESPERPIXEL)

#if !defined(GPIO_TFT_DATA_CONTROL)
#error Please reconfigure CMake with -DGPIO_TFT_DATA_CONTROL=<int> specifying which pin your display is using for the Data/Control line!
#endif

void InitST7735R(void);

void TurnDisplayOn(void);
void TurnDisplayOff(void);

#endif
