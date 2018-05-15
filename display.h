#pragma once

#include "config.h"

// Configure the desired display update rate. Use 120 for max performance/minimized latency, and 60/50/30/24 etc. for regular content, or to save battery.
#if defined(PI_ZERO) || defined(USE_GPU_VSYNC)
#define TARGET_FRAME_RATE 60
#else
#define TARGET_FRAME_RATE 120
#endif

#ifdef ILI9341
#include "ili9341.h"
#elif defined(ILI9486)
#include "ili9486.h"
#else
#error Please reconfigure CMake with -DADAFRUIT_ILI9341_PITFT=ON or -DFREEPLAYTECH_WAVESHARE32B=ON (or contribute ports to more displays yourself)
#endif
