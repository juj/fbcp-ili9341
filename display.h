#pragma once

#include "config.h"

// Configure the desired display update rate. Use 120 for max performance/minimized latency, and 60/50/30/24 etc. for regular content, or to save battery.
#define TARGET_FRAME_RATE 60

#ifdef ILI9341
#include "ili9341.h"
#elif defined(ILI9486)
#include "ili9486.h"
#elif defined(HX8357D)
#include "hx8357d.h"
#elif defined(ST7735R) || defined(ST7789)
#include "st7735r.h"
#elif defined(SSD1351)
#include "ssd1351.h"
#elif defined(MZ61581)
#include "mz61581.h"
#else
#error Please reconfigure CMake with -DADAFRUIT_ILI9341_PITFT=ON or -DFREEPLAYTECH_WAVESHARE32B=ON (or contribute ports to more displays yourself)
#endif

// The native display resolution is in portrait/landscape, but we want to display in the opposite landscape/portrait orientation?
#if ((DISPLAY_NATIVE_WIDTH < DISPLAY_NATIVE_HEIGHT && defined(DISPLAY_OUTPUT_LANDSCAPE)) || (DISPLAY_NATIVE_WIDTH > DISPLAY_NATIVE_HEIGHT && !defined(DISPLAY_OUTPUT_LANDSCAPE)))
#define DISPLAY_SHOULD_FLIP_ORIENTATION
#endif

#if defined(DISPLAY_SHOULD_FLIP_ORIENTATION) && !defined(DISPLAY_FLIP_ORIENTATION_IN_SOFTWARE)
// Need to do orientation flip, but don't want to do it on the CPU, so pretend the display dimensions are of the flipped form,
// and use display controller initialization sequence to do the flipping
#define DISPLAY_WIDTH DISPLAY_NATIVE_HEIGHT
#define DISPLAY_HEIGHT DISPLAY_NATIVE_WIDTH
#define DISPLAY_FLIP_ORIENTATION_IN_HARDWARE
#else
#define DISPLAY_WIDTH DISPLAY_NATIVE_WIDTH
#define DISPLAY_HEIGHT DISPLAY_NATIVE_HEIGHT
#endif

#if !defined(DISPLAY_SHOULD_FLIP_ORIENTATION) && defined(DISPLAY_FLIP_ORIENTATION_IN_SOFTWARE)
#undef DISPLAY_FLIP_ORIENTATION_IN_SOFTWARE
#endif

#ifndef DISPLAY_COVERED_LEFT_SIDE
#define DISPLAY_COVERED_LEFT_SIDE 0
#endif

#ifndef DISPLAY_COVERED_RIGHT_SIDE
#define DISPLAY_COVERED_RIGHT_SIDE 0
#endif

#ifndef DISPLAY_COVERED_TOP_SIDE
#define DISPLAY_COVERED_TOP_SIDE 0
#endif

#ifndef DISPLAY_COVERED_BOTTOM_SIDE
#define DISPLAY_COVERED_BOTTOM_SIDE 0
#endif

#define DISPLAY_DRAWABLE_WIDTH (DISPLAY_WIDTH-DISPLAY_COVERED_LEFT_SIDE-DISPLAY_COVERED_RIGHT_SIDE)
#define DISPLAY_DRAWABLE_HEIGHT (DISPLAY_HEIGHT-DISPLAY_COVERED_TOP_SIDE-DISPLAY_COVERED_BOTTOM_SIDE)
#define DISPLAY_SCANLINE_SIZE (DISPLAY_WIDTH*DISPLAY_BYTESPERPIXEL)

#ifndef DISPLAY_SPI_DRIVE_SETTINGS
#define DISPLAY_SPI_DRIVE_SETTINGS (0)
#endif

void ClearScreen(void);
void DeinitSPIDisplay(void);

#if !defined(SPI_BUS_CLOCK_DIVISOR)
#error Please define -DSPI_BUS_CLOCK_DIVISOR=<some even number> on the CMake command line! This parameter along with core_freq=xxx in /boot/config.txt defines the SPI display speed. (spi speed = core_freq / SPI_BUS_CLOCK_DIVISOR)
#endif

#if !defined(GPIO_TFT_DATA_CONTROL)
#error Please reconfigure CMake with -DGPIO_TFT_DATA_CONTROL=<int> specifying which pin your display is using for the Data/Control line!
#endif
