#pragma once

// Configures the desired display update rate.
#define TARGET_FRAME_RATE 60

// Suggestion to structure porting to other displays:
// #if DISPLAY == pitft_28r_ili9341
#include "pitft_28r_ili9341.h"
// #elif DISPLAY == some_other_supported_display
// #include "some_other_display_config.h"
// #endif

#define SCANLINE_SIZE (DISPLAY_WIDTH*DISPLAY_BYTESPERPIXEL)
#define FRAMEBUFFER_SIZE (DISPLAY_WIDTH*DISPLAY_HEIGHT*DISPLAY_BYTESPERPIXEL)
