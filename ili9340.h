#pragma once

#ifdef ILI9340

// SPI_BUS_CLOCK_DIVISOR specifies how fast to communicate the SPI bus at. Possible values are 4, 6, 8, 10, 12, ... Smaller
// values are faster. On my PiTFT 2.8 and Waveshare32b displays, divisor value of 4 does not work, and
// 6 is the fastest possible.

// Data specific to the ILI9340 controller
#define DISPLAY_SET_CURSOR_X 0x2A
#define DISPLAY_SET_CURSOR_Y 0x2B
#define DISPLAY_WRITE_PIXELS 0x2C

// ILI9340 displays are able to update at any rate between 61Hz to up to 119Hz. Default at power on is 70Hz.
#define ILI9340_FRAMERATE_61_HZ 0x1F
#define ILI9340_FRAMERATE_63_HZ 0x1E
#define ILI9340_FRAMERATE_65_HZ 0x1D
#define ILI9340_FRAMERATE_68_HZ 0x1C
#define ILI9340_FRAMERATE_70_HZ 0x1B
#define ILI9340_FRAMERATE_73_HZ 0x1A
#define ILI9340_FRAMERATE_76_HZ 0x19
#define ILI9340_FRAMERATE_79_HZ 0x18
#define ILI9340_FRAMERATE_83_HZ 0x17
#define ILI9340_FRAMERATE_86_HZ 0x16
#define ILI9340_FRAMERATE_90_HZ 0x15
#define ILI9340_FRAMERATE_95_HZ 0x14
#define ILI9340_FRAMERATE_100_HZ 0x13
#define ILI9340_FRAMERATE_106_HZ 0x12
#define ILI9340_FRAMERATE_112_HZ 0x11
#define ILI9340_FRAMERATE_119_HZ 0x10

// Visually estimating NES Super Mario Bros 3 "match mushroom, flower, star" arcade game, 119Hz gives visually
// most pleasing result, so default to using that. You can also try other settings above. 119 Hz should give
// lowest latency, perhaps 61 Hz might give least amount of tearing, although this can be quite subjective.
#define ILI9340_UPDATE_FRAMERATE ILI9340_FRAMERATE_119_HZ

#define DISPLAY_NATIVE_WIDTH 240
#define DISPLAY_NATIVE_HEIGHT 320

#define InitSPIDisplay InitILI9340

void InitILI9340(void);

#endif
