#pragma once

// Configures the desired display update rate. Use 120 for max performance/minimized latency, and 60/50/30/24 etc. for regular content, or to save battery.
#define TARGET_FRAME_RATE 120

#ifdef ILI9341
#include "ili9341.h"
#else
#error Please reconfigure CMake with -DADAFRUIT_ILI9341_PITFT=ON or -DFREEPLAYTECH_WAVESHARE32B=ON (or contribute ports to more displays yourself)
#endif
