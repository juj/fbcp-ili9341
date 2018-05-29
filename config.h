#pragma once

// Build options: Uncomment any of these, or set at the command line to configure:

// If defined, renders a performance overlay on top of the screen
#define STATISTICS

// How often the on-screen statistics is refreshed (in usecs)
#define STATISTICS_REFRESH_INTERVAL 200000

// How many usecs worth of past frame rate data do we preserve in the history buffer. Higher values
// make the frame rate display counter smoother and respond to changes with a delay, whereas smaller
// values can make the display fluctuate a bit erratically.
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

// Always enable GPU VSync on the Pi Zero. Even though it is suboptimal and can cause stuttering, it saves battery.
#if defined(PI_ZERO) && !defined(USE_GPU_VSYNC)
#define USE_GPU_VSYNC
#endif

#ifndef PI_ZERO
// If defined, communication with the SPI bus is handled with a dedicated thread. On the Pi Zero, this does
// not gain much, since it only has one hardware thread.
#define USE_SPI_THREAD

// If USE_GPU_VSYNC is defined, then enabling this causes new frames to be snapshot more often than at
// TARGET_FRAME_RATE interval to try to keep up smoother 60fps instead of stuttering. Consumes more CPU.
#define SELF_SYNCHRONIZE_TO_GPU_VSYNC_PRODUCED_NEW_FRAMES

#endif

// If enabled, the source video frame is not scaled to fit to the screen, but instead if the source frame
// is bigger than the SPI display, then content is cropped away, i.e. the source is displayed "centered"
// on the SPI screen:
// #define DISPLAY_CROPPED_INSTEAD_OF_SCALING

// If enabled, the main thread and SPI thread are executed with realtime priority
// #define RUN_WITH_REALTIME_THREAD_PRIORITY

// If defined, progressive updating is always used (at the expense of slowing down refresh rate if it's
// too much for the display to handle)
// #define NO_INTERLACING

#if (defined(FREEPLAYTECH_WAVESHARE32B) || (defined(ILI9341) && SPI_BUS_CLOCK_DIVISOR <= 4)) && defined(USE_DMA_TRANSFERS) && !defined(NO_INTERLACING)
// The Freeplaytech CM3/Zero displays actually only have a visible display resolution of 302x202, instead of
// 320x240, and this is enough to give them full progressive 320x240x60fps without ever resorting to
// interlacing. Also, ILI9341 displays running with clock divisor of 4 have enough bandwidth to never need
// interlacing either.
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
#if defined(ILI9486) || defined(HX8357D)
#define THROTTLE_INTERLACING
#endif

// If defined, DMA usage is foremost used to save power consumption and CPU usage. If not defined,
// DMA usage is tailored towards maximum performance.
// #define ALL_TASKS_SHOULD_DMA

#if defined(PI_ZERO) && defined(USE_DMA_TRANSFERS) && !defined(ALL_TASKS_SHOULD_DMA)
// This is a prerequisite for good performance on Pi Zero
#define ALL_TASKS_SHOULD_DMA
#endif

#if defined(ALL_TASKS_SHOULD_DMA)
// This makes all submitted tasks go through DMA, and not use a hybrid Polled SPI + DMA approach.
#define ALIGN_TASKS_FOR_DMA_TRANSFERS
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
// values of DISPLAY_WIDTH and DISPLAY_HEIGHT accordingly
#define DISPLAY_OUTPUT_LANDSCAPE

// If defined, flipping the display between portrait<->landscape is done in software, rather than
// asking the display controller to adjust its RAM write direction.
// Doing the flip in software reduces tearing, since neither the ILI9341 nor ILI9486 displays (and
// probably no other displays in existence?) allow one to adjust the direction that the scanline refresh
// cycle runs in, but the scanline refresh always runs in portrait mode in these displays. Not having
// this defined reduces CPU usage at the expense of more tearing, although it is debatable which
// effect is better - this can be subjective. Impact is around 0.5-1.0msec of extra CPU time.
// DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE disabled: diagonal tearing
// DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE enabled: traditional no-vsync tearing (tear line runs in portrait
// i.e. narrow direction)
#if !defined(PI_ZERO) && !defined(SSD1351)
#define DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE
#endif

// If enabled, build to utilize DMA transfers to communicate with the SPI peripheral. Otherwise polling
// writes will be performed (possibly with interrupts, if using kernel side driver module)
// #define USE_DMA_TRANSFERS

// If defined, enables code to manage the backlight.
// #define BACKLIGHT_CONTROL

#if defined(ADAFRUIT_ILI9341_PITFT) && defined(BACKLIGHT_CONTROL)

// If enabled, the display backlight will be turned off after this many usecs of no activity on screen.
#define TURN_DISPLAY_OFF_AFTER_USECS_OF_INACTIVITY (1 * 60 * 1000000)

#endif

// If less than this much % of the screen changes per frame, the screen is considered to be inactive, and
// the display backlight can automatically turn off, if TURN_DISPLAY_OFF_AFTER_USECS_OF_INACTIVITY is 
// defined.
#define DISPLAY_CONSIDERED_INACTIVE_PERCENTAGE (0.5 / 100.0)

#ifndef KERNEL_MODULE

// Define this if building the client side program to run against the kernel driver module, rather than
// as a self-contained userland program.
// #define KERNEL_MODULE_CLIENT

#endif

// Experimental/debugging: If defined, let the userland side program create and run the SPI peripheral
// driving thread. Otherwise, let the kernel drive SPI (e.g. via interrupts or its own thread)
// This should be unset, only available for debugging.
// #define KERNEL_MODULE_CLIENT_DRIVES
