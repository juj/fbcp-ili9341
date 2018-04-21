#pragma once

// Data specific to the ILI9341 controller
#define DISPLAY_BYTESPERPIXEL 2
#define DISPLAY_SET_CURSOR_X 0x2A
#define DISPLAY_SET_CURSOR_Y 0x2B
#define DISPLAY_WRITE_PIXELS 0x2C

#ifdef ADAFRUIT_ILI9341_PITFT
#include "pitft_28r_ili9341.h"
#elif defined(FREEPLAYTECH_WAVESHARE32B)
#include "freeplaytech_waveshare32b.h"
#else
#error Please reconfigure CMake with -DADAFRUIT_ILI9341_PITFT=ON or -FREEPLAYTECH_WAVESHARE32B=ON (or contribute ports to more displays yourself)
#endif
