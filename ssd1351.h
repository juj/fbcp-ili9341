#pragma once

#ifdef SSD1351

#ifndef SPI_BUS_CLOCK_DIVISOR
#error Please define -DSPI_BUS_CLOCK_DIVISOR=<some even number> on the CMake command line! (see file ssd1351.h for details). This parameter along with core_freq=xxx in /boot/config.txt defines the SPI display speed.
#endif

// On Adafruit's Adafruit 1.27" and 1.5" Color OLED Breakout Board 128x96 SSD1351 display, the following speed configurations have been tested (on a Pi 3B):

// core_freq=360: CDIV=20, results in 18.00MHz, works
// core_freq=370: CDIV=20, would result in 18.50MHz, this made the screen work for a while, but then hang
// core_freq=375: CDIV=20, would result in 18.75MHz, this made the screen work for a few seconds, but then go blank shortly after
// core_freq=355: CDIV=18, would result in 19.72MHz, this made the screen work for a few seconds, but then go blank shortly after

// Bandwidth needed to update at 60fps: 128*96*16*60 = 11,796,480 bits/sec.
// , so the above obtained best refresh rate allows driving the screen at 60fps.

// Data specific to the SSD1351 controller
#define DISPLAY_BYTESPERPIXEL 2
#define DISPLAY_SET_CURSOR_X 0x15
#define DISPLAY_SET_CURSOR_Y 0x75
#define DISPLAY_WRITE_PIXELS 0x5C

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 96

#define MUST_SEND_FULL_CURSOR_WINDOW

// The DISPLAY_WRITE_PIXELS command on this display seems to continue from the x&y where previous command left off. This is unlike
// other displays, where issuing a DISPLAY_WRITE_PIXELS command resets the x&y cursor coordinates.
#define DISPLAY_WRITE_PIXELS_CMD_DOES_NOT_RESET_WRITE_CURSOR

#define InitSPIDisplay InitSSD1351

#if !defined(GPIO_TFT_DATA_CONTROL)
#error Please reconfigure CMake with -DGPIO_TFT_DATA_CONTROL=<int> specifying which pin your display is using for the Data/Control line!
#endif

void InitSSD1351(void);

void TurnDisplayOn(void);
void TurnDisplayOff(void);

#endif
