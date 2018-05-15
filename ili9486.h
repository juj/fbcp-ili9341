#pragma once

#include "config.h"

// Data specific to the ILI9486 controller
#define DISPLAY_BYTESPERPIXEL 2
#define DISPLAY_SET_CURSOR_X 0x2A
#define DISPLAY_SET_CURSOR_Y 0x2B
#define DISPLAY_WRITE_PIXELS 0x2C

#include "waveshare35b.h"

#define DISPLAY_SCANLINE_SIZE (DISPLAY_WIDTH*DISPLAY_BYTESPERPIXEL)

#if !defined(GPIO_TFT_DATA_CONTROL)
#error Please reconfigure CMake with -DGPIO_TFT_DATA_CONTROL=<int> specifying which pin your display is using for the Data/Control line!
#endif

void InitILI9486(void);
