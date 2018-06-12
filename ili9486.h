#pragma once

#include "config.h"

// Data specific to the ILI9486 controller
#define DISPLAY_BYTESPERPIXEL 2
#define DISPLAY_SET_CURSOR_X 0x2A
#define DISPLAY_SET_CURSOR_Y 0x2B
#define DISPLAY_WRITE_PIXELS 0x2C

#ifdef WAVESHARE35B_ILI9486
#include "waveshare35b.h"
#endif

#if defined(DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE) || !defined(DISPLAY_OUTPUT_LANDSCAPE)
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 480
#else
#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 320
#endif

// On ILI9486 the display bus commands and data are 16 bits rather than the usual 8 bits that most other controllers have.
#define DISPLAY_SPI_BUS_IS_16BITS_WIDE

// ILI9486 does not behave well if one sends partial commands, but must finish each command or the command does not apply
#define MUST_SEND_FULL_CURSOR_WINDOW

void InitILI9486(void);
#define InitSPIDisplay InitILI9486
