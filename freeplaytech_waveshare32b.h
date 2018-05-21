#pragma once

// Data specific to the Waveshare32b display, as present on FreePlayTech's CM3/Zero devices (https://www.freeplaytech.com/)
#ifdef FREEPLAYTECH_WAVESHARE32B

#if !defined(GPIO_TFT_DATA_CONTROL)
#define GPIO_TFT_DATA_CONTROL 22
#endif

#if !defined(GPIO_TFT_RESET_PIN)
#define GPIO_TFT_RESET_PIN 27
#endif

#if defined(DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE) || !defined(DISPLAY_OUTPUT_LANDSCAPE)
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

#define DISPLAY_COVERED_TOP_SIDE 18
#define DISPLAY_COVERED_LEFT_SIDE 9
#define DISPLAY_COVERED_RIGHT_SIDE 29
#define DISPLAY_COVERED_BOTTOM_SIDE 0

#else

#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240

// On FreePlayTech GBA devices, a part of the screen is hidden by the bezels, since the GBA has a 2.8" screen surface area, but a 3.2" display is enclosed inside the case.
// The hidden area is placed under the left edge of the display horizontally, and under top and bottom edges of the display vertically, so reduce those out from the drawable area.
// Effective display area is then 320-18=302 pixels horizontally, and 202 pixels vertically.
#define DISPLAY_COVERED_TOP_SIDE 9
#define DISPLAY_COVERED_LEFT_SIDE 18
#define DISPLAY_COVERED_RIGHT_SIDE 0
#define DISPLAY_COVERED_BOTTOM_SIDE 29

#endif

#define DISPLAY_DRAWABLE_WIDTH (DISPLAY_WIDTH-DISPLAY_COVERED_LEFT_SIDE-DISPLAY_COVERED_RIGHT_SIDE)
#define DISPLAY_DRAWABLE_HEIGHT (DISPLAY_HEIGHT-DISPLAY_COVERED_TOP_SIDE-DISPLAY_COVERED_BOTTOM_SIDE)

#define InitSPIDisplay InitILI9341

#endif
