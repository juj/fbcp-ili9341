#pragma once

#ifdef ILI9341

// Specifies how fast to communicate the SPI bus at. Possible values are 4, 6, 8, 10, 12, ... Smaller
// values are faster. On my PiTFT 2.8 and Waveshare32b displays, divisor value of 4 does not work, and
// 6 is the fastest possible. While developing, it was observed that a value of 12 or higher did not
// actually work either, and only 6, 8 and 10 were functioning properly.
#ifndef SPI_BUS_CLOCK_DIVISOR
#error Please define -DSPI_BUS_CLOCK_DIVISOR=<some even number> on the CMake command line! (see file ili9341.h for details). This parameter along with core_freq=xxx in /boot/config.txt defines the SPI display speed.
#endif

// On Adafruit PiTFT 2.8", the following speed configurations have been tested (on a Pi 3B):
// core_freq=400: CDIV=6, results in 66.67MHz, works
// core_freq=294: CDIV=4, results in 73.50MHz, works
// core_freq=320: CDIV=4, would result in 80.00MHz, but this was too fast for the display
// core_freq=300: CDIV=4, would result in 75.00MHz, and would work for ~99% of the time, but develop rare occassional pixel glitches once a minute or so.
// core_freq=296: CDIV=4, would result in 74.50MHz, would produce tiny individual pixel glitches very rarely, once every few 10 minutes or so.

// On Waveshare 3.2", the following speed configurations have been observed to work (on a Pi 3B):
// core_freq=400: CDIV=6, results in 66.67MHz, works
// core_freq=310: CDIV=4, results in 77.50MHz, works
// core_freq=320: CDIV=4, would result in 80.00MHz, would work most of the time, but produced random occassional glitches every few minutes or so.

// On Adafruit 2.2" PiTFT HAT - 320x240 Display with ILI9340 controller, the following speed configurations have been tested (on a Pi 3B):
// core_freq=338: CDIV=4, results in 84.5MHz, works
// core_freq=339: CDIV=4, would result in 84.75MHz, would work most of the time, but every few minutes generated random glitched pixels.


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

void TurnDisplayOn(void);
void TurnDisplayOff(void);

#endif
