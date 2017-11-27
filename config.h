#pragma once

// Build options: Uncomment any of these, or set at the command line to configure:

// If defined, prints out performance logs to stdout every second
#define STATISTICS

#define STATISTICS_REFRESH_INTERVAL 200000
#define FRAMERATE_HISTORY_LENGTH 400000

// If defined, no sleeps are specified and the code runs as fast as possible. This should not improve
// performance, as the code has been developed with the mindset that sleeping should only occur at
// times when there is no work to do, rather than sleeping to reduce power usage. The only expected
// effect of this is that CPU usage shoots to 200%, while display update FPS is the same. Present
// here to allow toggling to debug this assumption.
// #define NO_THROTTLING

// If defined, display updates are synced to the vsync signal provided by the VideoCore GPU. That seems
// to occur quite precisely at 60 Hz. Testing on PAL NES games that run at 50Hz, this will not work well,
// since they produce new frames at every 20msecs, and the VideoCore functions for snapshotting also will
// output new frames at this vsync-detached interval, so there's a 50 Hz vs 60 Hz mismatch that results
// in visible microstuttering. Still, providing this as an option, this might be good for content that
// is known to run at native 60Hz.
// #define USE_GPU_VSYNC

// If defined, progressive updating is always used (at the expense of slowing down refresh rate if it's
// too much for the display to handle)
// #define NO_INTERLACING

// #define THROTTLE_INTERLACING

// #define ALWAYS_INTERLACING

// If defined, the GPU polling thread sleeps in TARGET_FRAMERATE intervals
#define SAVE_BATTERY_BY_SLEEPING_UNTIL_TARGET_FRAME

// Detects when the activity on the screen is mostly idle, and goes to low power mode
#define SAVE_BATTERY_BY_SLEEPING_WHEN_IDLE

// Builds a histogram of observed frame intervals and uses that to sync to a known update rate
#define SAVE_BATTERY_BY_PREDICTING_FRAME_ARRIVAL_TIMES

// Specifies how fast to communicate the SPI bus at. Possible values are 4, 6, 8, 10, 12, ... Smaller
// values are faster. On my PiTFT 2.8 display, divisor value of 4 does not work, and 6 is the fastest
// possible.
#define SPI_BUS_CLOCK_DIVISOR 6

// If defined, rotates the display 180 degrees
#define DISPLAY_ROTATE_180_DEGREES

// If defined, displays in landscape. Undefine to display in portrait. When changing this, swap
// values of  DISPLAY_WIDTH and DISPLAY_HEIGHT accordingly
#define DISPLAY_OUTPUT_LANDSCAPE
