#pragma once

// Configures the desired display update rate.
#define TARGET_FRAME_RATE 60

#include "pitft_28r_ili9341.h"

#define SCANLINE_SIZE (DISPLAY_WIDTH*DISPLAY_BYTESPERPIXEL)
#define FRAMEBUFFER_SIZE (DISPLAY_WIDTH*DISPLAY_HEIGHT*DISPLAY_BYTESPERPIXEL)
