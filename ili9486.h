#pragma once

#include "config.h"

// Data specific to the ILI9486 controller
#define DISPLAY_SET_CURSOR_X 0x2A001100
#define DISPLAY_SET_CURSOR_Y 0x2B001100
#define DISPLAY_WRITE_PIXELS 0x2C001100

#ifdef WAVESHARE35B_ILI9486
#include "waveshare35b.h"
#endif

#define DISPLAY_NATIVE_WIDTH 320
#define DISPLAY_NATIVE_HEIGHT 480

// On ILI9486 the display bus commands and data are 16 bits rather than the usual 8 bits that most other controllers have.
// (On ILI9486L however the command width is 8 bits, so they are quite different)
#define DISPLAY_SPI_BUS_IS_16BITS_WIDE

// ILI9486 does not behave well if one sends partial commands, but must finish each command or the command does not apply
#define MUST_SEND_FULL_CURSOR_WINDOW

//// #define DISPLAY_NEEDS_CHIP_SELECT_SIGNAL

void InitILI9486(void);
#define InitSPIDisplay InitILI9486
