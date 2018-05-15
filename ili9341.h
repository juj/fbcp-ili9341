#pragma once

// Specifies how fast to communicate the SPI bus at. Possible values are 4, 6, 8, 10, 12, ... Smaller
// values are faster. On my PiTFT 2.8 and Waveshare32b displays, divisor value of 4 does not work, and
// 6 is the fastest possible. While developing, it was observed that a value of 12 or higher did not
// actually work either, and only 6, 8 and 10 were functioning properly.
#ifndef SPI_BUS_CLOCK_DIVISOR
#define SPI_BUS_CLOCK_DIVISOR 6
#endif

// Data specific to the ILI9341 controller
#define DISPLAY_BYTESPERPIXEL 2
#define DISPLAY_SET_CURSOR_X 0x2A
#define DISPLAY_SET_CURSOR_Y 0x2B
#define DISPLAY_WRITE_PIXELS 0x2C

// ILI9341 displays are able to update at any rate between 61Hz to up to 119Hz. Default at power on is 70Hz.
#define ILI9341_FRAMERATE_61_HZ 0x1F
#define ILI9341_FRAMERATE_70_HZ 0x1B
#define ILI9341_FRAMERATE_79_HZ 0x18
#define ILI9341_FRAMERATE_119_HZ 0x10

// Visually estimating NES Super Mario Bros 3 "match mushroom, flower, star" arcade game, 119Hz gives most tear
// free scrolling, so default to using that.
#define ILI9341_UPDATE_FRAMERATE ILI9341_FRAMERATE_119_HZ

#ifdef ADAFRUIT_ILI9341_PITFT
#include "pitft_28r_ili9341.h"
#elif defined(FREEPLAYTECH_WAVESHARE32B)
#include "freeplaytech_waveshare32b.h"
#elif !defined(ILI9341)
#error Please reconfigure CMake with -DADAFRUIT_ILI9341_PITFT=ON, -FREEPLAYTECH_WAVESHARE32B=ON or -DILI9341=ON (or contribute ports to more displays yourself)
#endif

#define DISPLAY_SCANLINE_SIZE (DISPLAY_WIDTH*DISPLAY_BYTESPERPIXEL)

#if !defined(GPIO_TFT_DATA_CONTROL)
#error Please reconfigure CMake with -DGPIO_TFT_DATA_CONTROL=<int> specifying which pin your display is using for the Data/Control line!
#endif

void InitILI9341(void);
