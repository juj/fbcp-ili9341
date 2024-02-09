#pragma once

#if defined(GC9A01)

// Data specific to the GC9A01 controller
#define DISPLAY_SET_CURSOR_X 0x2A
#define DISPLAY_SET_CURSOR_Y 0x2B
#define DISPLAY_WRITE_PIXELS 0x2C

#define MUST_SEND_FULL_CURSOR_WINDOW

#define DISPLAY_NATIVE_WIDTH 240
#define DISPLAY_NATIVE_HEIGHT 240

#define ALL_TASKS_SHOULD_DMA
#define UPDATE_FRAMES_WITHOUT_DIFFING
#undef SAVE_BATTERY_BY_SLEEPING_WHEN_IDLE	// workaround for preventing crash in idle.

#ifdef WAVESHARE_GC9A01
#include "waveshare_gc9a01.h"
#endif

#define InitSPIDisplay InitGC9A01

void InitGC9A01(void);

#endif
