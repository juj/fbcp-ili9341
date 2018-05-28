#pragma once

#ifdef HX8357D

#ifndef SPI_BUS_CLOCK_DIVISOR
#error Please define -DSPI_BUS_CLOCK_DIVISOR=<some even number> on the CMake command line! (see file ili9341.h for details). This parameter along with core_freq=xxx in /boot/config.txt defines the SPI display speed.
#endif

// Data specific to the HX8357D controller
#define DISPLAY_BYTESPERPIXEL 2
#define DISPLAY_SET_CURSOR_X 0x2A
#define DISPLAY_SET_CURSOR_Y 0x2B
#define DISPLAY_WRITE_PIXELS 0x2C

#ifdef ADAFRUIT_HX8357D_PITFT
#include "pitft_35r_hx8357d.h"
#elif !defined(HX8357D)
#error Please reconfigure CMake with -DADAFRUIT_ILI9341_PITFT=ON, -FREEPLAYTECH_WAVESHARE32B=ON or -DILI9341=ON (or contribute ports to more displays yourself)
#endif

#if !defined(GPIO_TFT_DATA_CONTROL)
#error Please reconfigure CMake with -DGPIO_TFT_DATA_CONTROL=<int> specifying which pin your display is using for the Data/Control line!
#endif

void InitHX8357D(void);

void TurnDisplayOn(void);
void TurnDisplayOff(void);

#endif
