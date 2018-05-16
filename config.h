#pragma once

// Build options: Uncomment any of these, or set at the command line to configure:

// If defined, prints out performance logs to stdout every second
#define STATISTICS

// How often the on-screen statistics is refreshed (in usecs)
#define STATISTICS_REFRESH_INTERVAL 200000

// How many usecs worth of past frame rate data do we preserve in the history buffer. Higher values
// make the frame rate display counter smoother and respond to changes with a delay, whereas smaller
// values can make the display fluctuate a bit erratically.
#define FRAMERATE_HISTORY_LENGTH 400000

#ifdef USE_DMA_TRANSFERS
#define ALIGN_TASKS_FOR_DMA_TRANSFERS
#endif

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

// Always enable GPU VSync on the Pi Zero. Even though it is suboptimal and can cause stuttering, it saves battery.
#if defined(PI_ZERO) && !defined(USE_GPU_VSYNC)
#define USE_GPU_VSYNC
#endif

#ifndef PI_ZERO
// If defined, communication with the SPI bus is handled with a dedicated thread. On the Pi Zero, this does
// not gain much, since it only has one hardware thread.
#define USE_SPI_THREAD

#endif

// If enabled, the main thread and SPI thread are executed with realtime priority
// #define RUN_WITH_REALTIME_THREAD_PRIORITY

// If defined, progressive updating is always used (at the expense of slowing down refresh rate if it's
// too much for the display to handle)
// #define NO_INTERLACING

#if defined(FREEPLAYTECH_WAVESHARE32B) && USE_DMA_TRANSFERS && !defined(NO_INTERLACING) && !defined(PI_ZERO)
// The Freeplaytech CM3/Zero displays actually only have a visible display resolution of 302x202, instead of
// 320x240, and this is enough to give them full progressive 320x240x60fps without ever resorting to
// interlacing.
#define NO_INTERLACING
#endif

// If defined, all frames are always rendered as interlaced, and never use progressive rendering.
// #define ALWAYS_INTERLACING

// By default, if the SPI bus is idle after rendering an interlaced frame, but the GPU has not yet produced
// a new application frame to be displayed, the same frame will be rendered again for its other field.
// Define this option to disable this behavior, in which case when an interlaced frame is rendered, the 
// remaining other field half of the image will never be uploaded.
// #define THROTTLE_INTERLACING

// The ILI9486 has to resort to interlacing as a rule rather than exception, and it works much smoother
// when applying throttling to interlacing, so enable it by default there.
#ifdef WAVESHARE35B_ILI9486
#define THROTTLE_INTERLACING
#endif

// If defined, the GPU polling thread will be put to sleep for 1/TARGET_FRAMERATE seconds after receiving
// each new GPU frame, to wait for the earliest moment that the next frame could arrive.
#define SAVE_BATTERY_BY_SLEEPING_UNTIL_TARGET_FRAME

// Detects when the activity on the screen is mostly idle, and goes to low power mode, in which new
// frames will be polled first at 10fps, and ultimately at only 2fps.
#define SAVE_BATTERY_BY_SLEEPING_WHEN_IDLE

// Builds a histogram of observed frame intervals and uses that to sync to a known update rate. This aims
// to detect if an application uses a non-60Hz update rate, and synchronizes to that instead.
#define SAVE_BATTERY_BY_PREDICTING_FRAME_ARRIVAL_TIMES

// If defined, rotates the display 180 degrees
// #define DISPLAY_ROTATE_180_DEGREES

// If defined, displays in landscape. Undefine to display in portrait. When changing this, swap
// values of  DISPLAY_WIDTH and DISPLAY_HEIGHT accordingly
#define DISPLAY_OUTPUT_LANDSCAPE

// If enabled, build to utilize DMA transfers to communicate with the SPI peripheral. Otherwise polling
// writes will be performed (possibly with interrupts, if using kernel side driver module)
// #define USE_DMA_TRANSFERS

#ifndef KERNEL_MODULE

// Define this if building the client side program to run against the kernel driver module, rather than
// as a self-contained userland program.
// #define KERNEL_MODULE_CLIENT

#endif

// Experimental/debugging: If defined, let the userland side program create and run the SPI peripheral
// driving thread. Otherwise, let the kernel drive SPI (e.g. via interrupts or its own thread)
// This should be unset, only available for debugging.
// #define KERNEL_MODULE_CLIENT_DRIVES
