#pragma once

// Configures the desired display update rate.
#define TARGET_FRAME_RATE 60

#ifdef ILI9341
#include "ili9341.h"
#else
#error Please reconfigure CMake with -DADAFRUIT_ILI9341_PITFT=ON or -FREEPLAYTECH_WAVESHARE32B=ON (or contribute ports to more displays yourself)
#endif
