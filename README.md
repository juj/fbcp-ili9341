# Introduction

This repository implements a driver for certain SPI-based LCD displays for Raspberry Pi 3 and Zero.

![PiTFT display](/example.jpg "Adafruit PiTFT 2.8 with ILI9341 controller")

The work was motivated by curiosity after seeing this series of videos on the RetroManCave YouTube channel:
 - [RetroManCave: Waveshare 3.5" Raspberry Pi Screen | Review](https://www.youtube.com/watch?v=SGMC0t33C50)
 - [RetroManCave: Waveshare 3.2" vs 3.5" LCD screen gaming test | Raspberry Pi / RetroPie](https://www.youtube.com/watch?v=8bazEcXemiA)
 - [Elecrow 5 Inch LCD Review | RetroPie & Raspberry Pi](https://www.youtube.com/watch?v=8VgNBDMOssg)

In these videos, the SPI (GPIO) bus is referred to being the bottleneck. SPI based displays update over a serial data bus, transmitting one bit per clock cycle on the bus. A 320x240x16bpp display hence requires a SPI bus clock rate of 73.728MHz to achieve a full 60fps refresh frequency. Not many SPI LCD controllers can communicate this fast in practice, but are constrained to e.g. a 16-50MHz SPI bus clock speed, capping the maximum update rate significantly. Can we do anything about this?

The `fbcp-ili9341` project started out as a display driver for the [Adafruit 2.8" 320x240 TFT w/ Touch screen for Raspberry Pi](https://www.adafruit.com/product/1601) display that utilizes the ILI9341 controller. On that display, `fbcp-ili9341` can achieve a 60fps update rate, depending on the content that is being displayed. Check out these videos for examples of the driver in action:

 - [fbcp-ili9341 ported to ILI9486 WaveShare 3.5" (B) SpotPear 320x480 SPI display](https://www.youtube.com/watch?v=dqOLIHOjLq4)
 - [Quake 60 fps inside Gameboy Advance (ILI9341)](https://www.youtube.com/watch?v=xmO8t3XlxVM)
 - First implementation of a statistics overlay: [fbcp-ili9341 SPI display driver on Adafruit PiTFT 2.8"](http://youtu.be/rKSH048XRjA)
 - Initial proof of concept video: [fbcp-ili9341 driver first demo](https://youtu.be/h1jhuR-oZm0)

### How It Works

Given that the SPI bus can be so constrained on bandwidth, how come `fbcp-ili9341` seems to be able to update at up to 60fps? The way this is achieved is by what could be called *adaptive display stream updates*. Instead of uploading each pixel at each display refresh cycle, only the actually changed pixels on screen are submitted to the display. This is doable because the ILI9341 controller, as many other popular controllers, have communication interface functions that allow specifying partial screen updates, down to subrectangles or even individual pixel levels. This allows beating the bandwidth limit: for example in Quake, even though it is a fast pacing game, on average only about 46% of all pixels on screen change each rendered frame. Some parts, such as the UI stay practically constant across multiple frames.

Other optimizations are also utilized to squeeze out even more performance:
 - The program directly communicates with the BCM2835 ARM Peripherals controller registers, bypassing the usual Linux software stack.
 - A hybrid of both Polled Mode SPI and DMA based transfers are utilized. Long sequential transfer bursts are performed using DMA, and when DMA would have too much latency, Polled Mode SPI is applied instead.
 - Undocumented BCM2835 features are used to squeeze out maximum bandwidth: [SPI CDIV is driven at even numbers](https://www.raspberrypi.org/forums/viewtopic.php?t=43442) (and not just powers of two), and the [SPI DLEN register is forced in non-DMA mode](https://www.raspberrypi.org/forums/viewtopic.php?t=181154) to avoid an idle 9th clock cycle for each transferred byte.
 - Good old **interlacing** is added into the mix: if the amount of pixels that needs updating is detected to be too much that the SPI bus cannot handle it, the driver adaptively resorts to doing an interlaced update, uploading even and odd scanlines at subsequent frames. Once the number of pending pixels to write returns to manageable amounts, progressive updating is resumed. This effectively doubles the maximum display update rate.
 - A dedicated SPI communication thread is used in order to keep the SPI bus active at all times.
 - A number of other micro-optimization techniques are used, such as batch updating rectangular spans of pixels, merging disjoint-but-close spans of pixels on the same scanline, and latching Column and Page End Addresses to bottom-right corner of the display to be able to cut CASET and PASET messages in mid-communication.

The result is that the SPI bus can be kept close to 100% saturation, ~94-97% usual, to maximize the utilization rate of the bus, while only transmitting practically the minimum number of bytes needed to describe each new frame.

### Tested Devices

The driver has been worked on the following systems:

 - Raspberry Pi 3 Model B+ with Raspbian Stretch (GCC 6.3.0)
 - Raspberry Pi 3 Model B Rev 1.2 with Raspbian Jessie (GCC 4.9.2) and Raspbian Stretch (GCC 6.3.0)
 - Raspberry Pi Zero W with Raspbian Jessie (GCC 4.9.2) and Raspbian Stretch (GCC 6.3.0)

### Tested Displays

The following LCD displays have been tested:

 - [Adafruit 2.8" 320x240 TFT w/ Touch screen for Raspberry Pi](https://www.adafruit.com/product/1601) with ILI9341 controller
 - [Adafruit PiTFT 2.2" HAT Mini Kit - 320x240 2.2" TFT - No Touch](https://www.adafruit.com/product/2315) with ILI9340 controller
 - [Adafruit PiTFT - Assembled 480x320 3.5" TFT+Touchscreen for Raspberry Pi](https://www.adafruit.com/product/2097) with HX8357D controller
 - [Adafruit 128x96 OLED Breakout Board - 16-bit Color 1.27" w/microSD holder](https://www.adafruit.com/product/1673) with SSD1351 controller
 - [Waveshare 3.5inch RPi LCD (B) 320*480 Resolution Touch Screen IPS TFT Display](https://www.amazon.co.uk/dp/B01N48NOXI/ref=pe_3187911_185740111_TE_item) with ILI9486 controller
 - [BuyDisplay.com 320x480 Serial SPI 3.2"TFT LCD Module Display](https://www.buydisplay.com/default/serial-spi-3-2-inch-tft-lcd-module-display-ili9341-power-than-sainsmart) with ILI9341 controller
 - [Arduino A000096 1.77" 160x128 LCD Screen](https://store.arduino.cc/arduino-lcd-screen) with ST7735R controller
 - [Tontec 3.5" 320x480 LCD Display](https://www.ebay.com/p/Tontec-3-5-Inches-Touch-Screen-for-Raspberry-Pi-Display-TFT-Monitor-480x320-LCD/1649448059) with MZ61581-PI-EXT 2016.1.28 controller

### Known Issues

Be aware of the following limitations:

###### Specific to BCM2835
 - The codebase has been written with a hardcoded assumption of the ARM BCM2835 chip. Since it bypasses the generic drivers for SPI and GPIO, it will definitely not work out of the box on any other display controllers than the ones mentioned earlier. It might not work on other Pis than the ones mentioned earlier either. The driver also assumes it is the exclusive user of the SPI bus, which means that at the moment, touch controllers are not supported and should be disabled.

###### No rendered frame delivery via events from VideoCore IV GPU
 - The codebase captures screen framebuffers by snapshotting via the VideoCore `vc_dispmanx_snapshot()` API, and the obtained pixels are then routed on to the SPI-based display. This kind of polling is performed, since there does not exist an event-based mechanism to get new frames from the GPU as they are produced. The result is inefficient and can easily cause stuttering, since different applications produce frames at different paces. For example an emulated PAL NES game would be producing frames at fixed 50Hz, a native GLES2 game at fixed 60Hz, or perhaps at variable times depending on the GPU workload. **Ideally the code would ask the VideoCore API to receive finished frames in callback notifications immediately after they are rendered**, but this kind of functionality does not exist in the current GPU driver stack. In the absence of such event delivery mechanism, the code has to resort to polling snapshots of the display framebuffer using carefully timed heuristics to balance between keeping latency and stuttering low, while not causing excessive power consumption. These heuristics keep continuously guessing the update rate of the animation on screen, and they have been tuned to ensure that CPU usage goes down to 0% when there is no detected activity on screen, but it is certainly not perfect. This GPU limitation is discussed at https://github.com/raspberrypi/userland/issues/440. If you'd like to see fbcp-ili9341 operation reduce latency, stuttering and power consumption, please throw a (kind!) comment or a thumbs up emoji in that bug thread to share that you care about this, and perhaps Raspberry Pi engineers might pick the improvement up on the development roadmap.

###### Screen resize freezes DispmanX
 - Currently if one resizes the video frame size at runtime, this causes DispmanX API to go sideways. See https://github.com/raspberrypi/userland/issues/461 for more information. Best workaround is to set the desired screen resolution in `/boot/config.txt` and configure all applications to never change that at runtime.

###### CPU Turbo is needed for good SPI bus bandwidth
 - The speed of the SPI bus is linked to the BCM2835 core frequency. This frequency is at 250MHz by default (on e.g. Pi Zero, 3B and 3B+), and under CPU load, the core turbos up to 400MHz. This turboing directly scales up the SPI bus speed by `400/250=+60%` as well. Therefore when choosing the SPI `CDIV` value to use, one has to pick one that works for both idle and turbo clock speeds. Conversely, the BCM core reverts to non-turbo speed when there is only light CPU load active, and this slows down the display, so if an application is graphically intensive but light on CPU, the SPI display bus does not get a chance to run at maximum speeds. A way to work around this is to force the BCM core to always stay in its turbo state with `force_turbo=1` option in `/boot/config.txt`, but this has an unfortunate effect of causing the ARM CPU to always run in turbo speed as well, consuming excessive amounts of power. At the time of writing, there does not yet exist a good solution to have both power saving and good performance. This limitation is being discussed in more detail at https://github.com/raspberrypi/firmware/issues/992.

### Installation

Check the following topics to set up the driver.

##### Boot configuration

This driver does not utilize the [notro/fbtft](https://github.com/notro/fbtft) framebuffer driver, so that needs to be disabled if active. That is, if your `/boot/config.txt` file has a line that looks something like `dtoverlay=pitft28r, ...`, `dtoverlay=waveshare32b, ...` or `dtoverlay=flexfb, ...`, it should be removed.

This program neither utilizes the default SPI driver, so a line such as `dtparam=spi=on` in `/boot/config.txt` should also be removed so that it will not cause conflicts.

Likewise, if you have any touch controller related dtoverlays active, such as `dtoverlay=ads7846,...` or anything that has a `penirq=` directive, that should be removed as well to avoid conflicts. It would be possible to add touch support to `fbcp-ili9341` if someone wants to take a stab at it.

##### Building and running

Run in the console of your Raspberry Pi:

```bash
git clone https://github.com/juj/fbcp-ili9341.git
cd fbcp-ili9341
mkdir build
cd build
cmake [options] ..
make -j
sudo ./fbcp-ili9341
```

See the next section to see what to input under **[options]**.

If you have been running existing `fbcp` driver, make sure to remove that e.g. via a `sudo pkill fbcp` first (while running in SSH prompt or connected to a HDMI display), these two cannot run at the same time. If `/etc/rc.local` or `/etc/init.d` contains an entry to start up `fbcp` at boot, that directive should be deleted.

##### Configuring build options

There are generally two ways to configure build options, at CMake command line, and in the file [config.h](https://github.com/juj/fbcp-ili9341/blob/master/config.h).

On the CMake command line, the following options can be configured:

- `-DPI_ZERO=ON`: Pass this option if you are running on a Pi Zero. If not present, Pi 3 Model B is assumed.
- `-DADAFRUIT_ILI9341_PITFT=ON`: If you are running on the [Adafruit 2.8" 320x240 TFT w/ Touch screen for Raspberry Pi](https://www.adafruit.com/product/1601) display, pass this flag.
- `-DFREEPLAYTECH_WAVESHARE32B=ON`: If you are running on the [Freeplay CM3 or Zero](https://www.freeplaytech.com/product/freeplay-cm3-diy-kit/) device, pass this flag.
- `-DILI9341=ON`: If you are running on any other generic ILI9341 display, or on Waveshare32b display that is standalone and not on the FreeplayTech CM3/Zero device, pass this flag. When this flag is passed, you must also specify the flags `-DGPIO_TFT_DATA_CONTROL=number` and `-DGPIO_TFT_DATA_CONTROL=number` below.
- `-DADAFRUIT_HX8357D_PITFT=ON`: If you have the [Adafruit PiTFT - Assembled 480x320 3.5" TFT+Touchscreen for Raspberry Pi](https://www.adafruit.com/product/2097) display, add this line.
- `-DHX8357D=ON`: If you have any other generic HX8357D display, pass this directive.
- `-DST7735R=ON`: If you have a ST7735R display, use this.
- `-DSSD1351=ON`: If you have a SSD1351 OLED display, use this.
- `-DGPIO_TFT_DATA_CONTROL=number`: Specifies/overrides which GPIO pin to use for the Data/Control (DC) line on the 4-wire SPI communication. This pin number is specified in BCM pin numbers.
- `-DGPIO_TFT_RESET_PIN=number`: Specifies/overrides which GPIO pin to use for the display Reset line. This pin number is specified in BCM pin numbers. If omitted, it is assumed that the display does not have a Reset pin, and is always on.
- `-DGPIO_TFT_BACKLIGHT=number`: Specifies/overrides which GPIO pin to use for the display backlight line. This pin number is specified in BCM pin numbers. If omitted, it is assumed that the display does not have a GPIO-controlled backlight pin, and is always on. If setting this, also see the `#define BACKLIGHT_CONTROL` option in `config.h`.
- `-DWAVESHARE35B_ILI9486=ON`: If specified, targets a Waveshare 3.5" 480x320 display, or possibly any other generic ILI9486 controller. This support is experimental.
- `-DILI9486=ON`: If you have any other generic ILI9486 display, pass this directive.
- `-DUSE_DMA_TRANSFERS=OFF`: If specified, disables using DMA transfers. Pass this if DMA is giving some issues.
- `-DDMA_TX_CHANNEL=<num>`: Specifies the DMA channel number to use for SPI send commands. Change this if you find a DMA channel conflict.
- `-DDMA_RX_CHANNEL=<num>`: Specifies the DMA channel number to use for SPI receive commands. Change this if you find a DMA channel conflict.
- `-DDISPLAY_SWAP_BGR=ON`: If this option is passed, red and blue color channels are reversed (RGB<->BGR) swap. Some displays have an opposite color panel subpixel layout that the display controller does not automatically account for, so define this if blue and red are mixed up.
- `-DDISPLAY_INVERT_COLORS=ON`: If this option is passed, pixel color value interpretation is reversed (white=0, black=31/63). Default: black=0, white=31/63. Pass this option if the display image looks like a color negative of the actual colors.
- `-DSPI_BUS_CLOCK_DIVISOR=even_number`: Sets the clock divisor number which along with the Pi [core_freq=](https://www.raspberrypi.org/documentation/configuration/config-txt/overclocking.md) option in `/boot/config.txt` specifies the overall speed that the display SPI communication bus is driven at. `SPI_frequency = core_freq/divisor`. `SPI_BUS_CLOCK_DIVISOR` must be an even number. Default Pi 3B and Zero W `core_freq` is 400MHz, and generally a value `-DSPI_BUS_CLOCK_DIVISOR=6` seems to be good safe and performant baseline for ILI9341 displays. Try a larger value if the display shows corrupt output, or a smaller value to get higher bandwidth. See [ili9341.h](https://github.com/juj/fbcp-ili9341/blob/master/ili9341.h#L13) and [waveshare35b.h](https://github.com/juj/fbcp-ili9341/blob/master/waveshare35b.h#L10) for data points on tuning the maximum SPI performance.

In addition to the above CMake directives, there are various defines scattered around the codebase, mostly in [config.h](https://github.com/juj/fbcp-ili9341/blob/master/config.h), that control different runtime options. Edit those directly to further tune the behavior of the program. In particular, after you have finished with the setup, you may want to remove the `#define STATISTICS` line in `config.h`.

##### Tuning CPU Usage

Diffing frames to produce minimal SPI communication task lists takes up some performance, so expect to see a moderate backround CPU load coming from `fbcp-ili9341`. The default build configuration of `fbcp-ili9341` is optimized towards maximum performance on the Pi 3, and towards battery saving on the Pi Zero. Check out the build option `ALL_TASKS_SHOULD_DMA` in `config.h`. Enabling that option in the build optimizes towards saving battery at the expense of performance, whereas building with that option disabled causes `fbcp-ili9341` to strive towards maximum screen refresh rate.

To further reduce power consumption beyond enabling `ALL_TASKS_SHOULD_DMA`, try enabling `USE_GPU_VSYNC`, and/or lowering `TARGET_FRAME_RATE` in `display.h` - set it to e.g. 40 or 30. Currently Pi Zero will not likely reach 60fps except in NES games.

If https://github.com/raspberrypi/userland/issues/440 is resolved in the future, CPU usage on Pi Zero is possible to improve.

##### Launching the display driver at startup

To set up the driver to launch at startup, edit the file `/etc/rc.local` in `sudo` mode, and add a line

```bash
sudo /path/to/fbcp-ili9341/build/fbcp-ili9341 &
````

to the end. Make note of the needed ampersand `&` at the end of that line.

##### Configuring HDMI and TFT display sizes

If the size of the default HDMI output `/dev/fb0` framebuffer differs from the resolution of the display, the source video size will by default be rescaled to fit to the size of the SPI display. `fbcp-ili9341` will manage setting up this rescaling if needed, and it will be done by the GPU, so performance should not be impacted too much. However if the resolutions do not match, small text will probably appear illegible. The resizing will be done in aspect ratio preserving manner, so if the aspect ratios do not match, either horizontal or vertical black borders will appear on the display. If you do not use the HDMI output at all, it is probably best to configure the HDMI output to match the SPI display size so that rescaling will not be needed. This can be done by setting the following lines in `/boot/config.txt`:

```
hdmi_group=2
hdmi_mode=87
hdmi_cvt=320 240 60 1 0 0 0
hdmi_force_hotplug=1
```

If your SPI display has a different resolution than 320x240, change the `320 240` part to e.g. `480 320`.

These lines hint native applications about the default display mode, and let them render to the native resolution of the TFT display. This can however prevent the use of the HDMI connector, if the HDMI connected display does not support such a small resolution. As a compromise, if both HDMI and SPI displays want to be used at the same time, some other compatible resolution such as 640x480 can be used. See [Raspberry Pi HDMI documentation](https://www.raspberrypi.org/documentation/configuration/config-txt/video.md) for the available options to do this.

##### Tuning Performance

Check the following tips to optimize the display to run as fast as possible.

1. The main configuration is the SPI bus `CDIV` (Clock DIVider) setting which controls the MHz rate of the SPI0 controller. By default this is set to value `CDIV=6`. To adjust this value, pass the directive `-DSPI_BUS_CLOCK_DIVISOR=even_number` in CMake command line. Possible values are even numbers `2`, `4`, `6`, `8`, `...`. Smaller values result in higher bus speeds. Usually for ILI9341, `CDIV=6` seems good, but if one underclocks `core_freq` to around 300MHz, it looks like `CDIV=4` can also be used. On ILI9486, it seems that `CDIV=14` or `CDIV=16` are good starting values.

2. Ensure turbo speed. This is critical for good frame rates. On the Raspberry Pi 3 Model B, the SPI bus runs by default at `400/CDIV` MHz **if** there is enough power provided to the Pi, and if the CPU temperature does not exceed thermal limits. If for some reason under-voltage protection is kicking in even when enough power should be fed, you can [force-enable turbo when low voltage is present](https://www.raspberrypi.org/forums/viewtopic.php?f=29&t=82373) by setting the value `avoid_warnings=2` in the file `/boot/config.txt`. The effect of turbo speed on performance is significant, 400MHz vs non-turbo 250MHz, which comes out to +60% of more bandwidth. Getting 60fps in Quake, Sonic or Tyrian requires this turbo frequency, but NES and C64 emulators can often reach 60fps even with the stock 250MHz.

3. Perhaps a bit counterintuitively, **underclock** the core. The `core_freq=` option in `/boot/config.txt` affects also the SPI bus speed. Setting a **smaller** core frequency than the default turbo 400MHz can enable using a smaller clock divider to get a higher resulting SPI bus speed. For example, if with default `core_freq=400` SPI `CDIV=8` works (resulting in SPI bus speed `400MHz/8=50MHz`), but `CDIV=6` does not (`400MHz/6=66.67MHz` was too much), you can try lowering `core_freq=360` and set `CDIV=6` to get an effective SPI bus speed of `360MHz/6=60MHz`, a middle ground between the two that might perhaps work. Balancing `core_freq=` and `CDIV` options allows one to find the maximum SPI bus speed up to the last few kHz that the display controller can tolerate.

##### Reducing CPU Usage

On the other hand, it is desirable to control how much CPU time `fbcp-ili9341` is allowed to use. The default build settings are tuned to maximize the display refresh rate at the expense of power consumption on Pi 3B. On Pi Zero, the opposite is done, i.e. by default the driver optimizes for battery saving instead of maximal display update speed. The following options can be controlled to balance between these two:

- The main option to control CPU usage vs performance aspect is the option `#define ALL_TASKS_SHOULD_DMA` in `config.h`. Enabling this option will greatly reduce CPU usage. If this option is disabled, SPI bus utilization is maximized but CPU usage can be up to 80%-120%. When this option is enabled, CPU usage is generally up to around 15%-30%. Maximal CPU usage occurs when watching a video, or playing a fast moving game. If nothing is changing on the screen, CPU consumption of the driver should go down very close to 0-5%. By default `#define ALL_TASKS_SHOULD_DMA` is enabled for Pi Zero, but disabled for Pi 3B.

- The CMake option `-DUSE_DMA_TRANSFERS=ON` should always be enabled for good low CPU usage. If DMA transfers are disabled, the driver will run in Polled SPI mode, which generally utilizes a full dedicated single core of CPU time. If DMA transfers are causing issues, try adjusting the DMA send and receive channels to use for SPI communication with `-DDMA_TX_CHANNEL=<num>` and `-DDMA_RX_CHANNEL=<num>` CMake options.

- The statistics overlay prints out quite detailed information about execution state. Removing `#define STATISTICS` in config.h improves performance and reduces CPU usage. If you want to keep printing statistics, you can try increasing the interval with the `#define STATISTICS_REFRESH_INTERVAL <timeInMicroseconds>` option.

- Enabling `#define USE_GPU_VSYNC` reduces CPU consumption, but because of https://github.com/raspberrypi/userland/issues/440 can cause stuttering. Disabling `#defined USE_GPU_VSYNC` produces less stuttering, but because of https://github.com/raspberrypi/userland/issues/440, increases CPU power consumption.

- The option `#define SELF_SYNCHRONIZE_TO_GPU_VSYNC_PRODUCED_NEW_FRAMES` can be used in conjunction with `#define USE_GPU_VSYNC` to try to find a middle ground between https://github.com/raspberrypi/userland/issues/440 issues - moderate to little stuttering while not trying to consume too much CPU. Try experimenting with enabling or disabling this setting.

- There are a number of `#define SAVE_BATTERY_BY_x` options in config.h, which all default to being enabled. These should be safe to use always without tradeoffs. If you are experiencing latency or performance related issues, you can try to toggle these to troubleshoot.

- The option `#define DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE` does cause a bit of extra CPU usage, so disabling it will lighten up the CPU load a bit.

- If your SPI display bus is able to run really fast in comparison to the size of the display and the amount of content changing on the screen, you can try enabling `#define UPDATE_FRAMES_IN_SINGLE_RECTANGULAR_DIFF` to reduce CPU usage at the expense of increasing the number of bytes sent over the bus.

- The option `#define RUN_WITH_REALTIME_THREAD_PRIORITY` can be enabled to make the driver run at realtime process priority. This can lock up the system however, but still made available for advanced experimentation.

- In `display.h` there is an option `#define TARGET_FRAME_RATE <number>`. Setting this to a smaller value, such as 30, will trade refresh rate to reduce CPU consumption.

### Statistics Overlay

By default `fbcp-ili9341` builds with a statistics overlay enabled because - admit it - when you first run, you like to geek out on all the knobs and numbers anyways. See the video [fbcp-ili9341 ported to ILI9486 WaveShare 3.5" (B) SpotPear 320x480 SPI display](https://www.youtube.com/watch?v=dqOLIHOjLq4) to find details on what each field means. Remove the `#define STATISTICS` option in `config.h` to disable displaying the statistics.

### FAQ and Troubleshooting

#### Does fbcp-ili9341 work on Pi Zero?

Yes, it does, although not quite as well as on Pi 3B. If you'd like it to run better on a Pi Zero, leave a thumbs up at https://github.com/raspberrypi/userland/issues/440 - hard problems are difficult to justify prioritizing unless it is known that many people care about them.

#### The driver works well, but image is upside down. How do I rotate the display?

Enable the option `#define DISPLAY_ROTATE_180_DEGREES` in `config.h`. This should rotate the display to show up the other way around. Another option is to utilize a `/boot/config.txt` option [display_rotate=2](https://www.raspberrypi.org/forums/viewtopic.php?t=120793)

#### How exactly do I edit the build options to e.g. remove the statistics lines or change some other option?

Edit the file `config.h` in a text editor (a command line one such as `pico`, `vim`, `nano`, or SSH map the drive to your host), and find the line `#define STATISTICS` in there. Add comment lines `//` in front of that text to disable the option, or remove the `//` characters to enable it.

After having edited and saved the file, reissue `make -j` in the build directory and restart `fbcp-ili9341`.

#### Does fbcp-ili9341 work with linux command line terminal or X windowing system?

Yes, both work fine. For linux command line terminal, the `/dev/tty1` console should be set to output to Linux framebuffer 0 (`/dev/fb0`). This is the default mode of operation and there do not exist other framebuffers in a default distribution of Raspbian, but if you have manually messed with the `con2fbmap` command in your installation, you may have inadvertently changed this configuration. Run `con2fbmap 1` to see which framebuffer the `/dev/tty1` console is outputting to, it should print `console 1 is mapped to framebuffer 0`. Type `con2fbmap 1 0` to reset console 1 back to outputting to framebuffer 0.

Likewise, the X windowing system should be configured to render to framebuffer 0. This is by default the case. The target framebuffer for X windowing service is usually configured via the `FRAMEBUFFER` environment variable before launching X. If X is not working by default, you can try overriding the framebuffer by launching X with `FRAMEBUFFER=/dev/fb0 startx` instead of just running `startx`.

#### Does fbcp-ili9341 work on Raspberry Pi 1 or Pi 2?

I don't know, I don't currently have any to test. Perhaps the code does need some model specific configuration, or perhaps it might work out of the box. I only have Pi 3B, Pi 3B+, Pi Zero W and a Pi 3 Compute Module based systems to experiment on.

#### Does fbcp-ili9341 work on display XYZ?

If the display controller is one of the currently tested ones (see the list above), and it is wired up to run using 4-line SPI, then it should work. Pay attention to configure the `Data/Control` GPIO pin number correctly, and also specify the `Reset` GPIO pin number if the device has one.

If the display controller is not one of the tested ones, it may still work if it is similar to one of the existing ones. For example, ILI9340 and ILI9341 are practically the same controller. You can just try with a specific one to see how it goes.

If `fbcp-ili9341` does not support your display controller, you will have to write support for it. `fbcp-ili9341` does not have a "generic SPI TFT driver routine" that might work across multiple devices, but needs specific code for each. If you have the spec sheet available, you can ask for advice, but please do not request to add support to a display controller "blind", that is not possible.

#### Does fbcp-ili9341 work with 3-wire SPI displays?

No, only 4-wire SPI displays work. Make sure the display has a Data/Control (DC) GPIO pin to connect.

#### Does fbcp-ili9341 work with I2C, DPI, MIPI DSI or USB connected displays?

No. Those are completely different technologies altogether. It should be possible to port the driver algorithm to work on I2C however, if someone is interested.

#### Does fbcp-ili9341 work with touch displays?

At the moment one cannot utilize the XPT2046/ADS7846 touch controllers while running `fbcp-ili9341`, so touch is mutually incompatible with this driver. In order for `fbcp-ili9341` to function, you will need to remove all `dtoverlay`s in `/boot/config.txt` related to touch.

#### Is it possible to break my display with this driver if I misconfigure something?

I have done close to everything possible to my displays - cut power in middle of operation, sent random data and command bytes, set their operating voltage commands and clock timings to arbitrary high and low values, tested unspecified and reserved command fields, and driven the displays dozens of MHz faster than they managed to keep up with, and I have not yet done permanent damage to any of my displays or Pis.

Easiest way to do permanent damage is to fail at wiring, e.g. drive 5 volts if your display requires 3.3v, or short a connection, or something similar.

The one thing that `fbcp-ili9341` stays clear off is that it does not program the non-volatile memory areas of any of the displays. Therefore a hard power off on a display should clear all performed initialization and reset the display to its initial state at next power on.

That being said, if it breaks, you'll get to purchase a new shiny one to replace it.

#### Can I have both the HDMI and SPI connected at the same time?

Yes, `fbcp-ili9341` shows the output of the HDMI display on the SPI screen, and both can be attached at the same time. A HDMI display does not have to be connected however, although `fbcp-ili9341` operation will still be affected by whatever HDMI display mode is configured. Check out `tvservice -s` on the command line to check what the current DispmanX HDMI output mode is.

#### Do I have to show the same image on HDMI output and the SPI display, or can they be different?

At the moment `fbcp-ili9341` has been developed to only display the contents of the main DispmanX GPU framebuffer over to the SPI display. That is, the SPI display will show the same picture as the HDMI output does. There is no technical restriction that requires this though, so if you know C/C++ well, it should be a manageable project to turn `fbcp-ili9341` to operate as an offscreen display library to show a completely separate (non-GPU-accelerated) image than what the main HDMI display outputs. For example you could have two different outputs, e.g. a HUD overlay, a dashboard for network statistics, weather, temps, etc. showing on the SPI while having the main Raspberry Pi desktop on the HDMI.

In this kind of mode, you would probably strip the DispmanX bits out of `fbcp-ili9341`, and recast it as a static library that you would link to in your drawing application, and instead of snapshotting frames, you can then programmatically write to a framebuffer in memory from your C/C++ code.

#### I am running fbcp-ili9341 on a display that was listed above, but the display stays white after startup?

Unfortunately there are a number of things to go wrong that all result in a white screen. This is probably the hardest part to diagnose. Some ideas:

- double check the wiring,
- double check that the display controller is really what you expected. Trying to drive with the display with wrong initialization code usually results in the display not reacting, and the screen stays white,
- shut down and physically power off the Pi and the display in between multiple tests. Driving a display with a wrong initialization routine may put it in a bad state that needs a physical power off for it to reset,
- if there is a reset pin on the display, make sure to pass it in CMake line. Or alternatively, try driving `fbcp-ili9341` without specifying the reset pin,
- make sure the display is configured to run 4-wire SPI mode, and not in parallel mode or 3-wire SPI mode. You may need to solder or desolder some connections or set a jumper to configure the specific driving mode.

#### The display stays blank at boot without lighting up

This suggests that the power line or the backlight line might not be properly connected. Or if the backlight connects to a GPIO pin on the Pi (and not a voltage pin), then it may be that the pin is not in correct state for the backlight to turn on. Most of the LCD TFT displays I have immediately light up their backlight when they receive power. The Tontec one has a backlight GPIO pin that boots up high but must be pulled low to activate the backlight. OLED displays on the other hand seem to stay all black even after they do get power, while waiting for their initialization to be performed, so for OLEDs it may be normal for nothing to show up on the screen immediately after boot.

If the backlight connects to a GPIO pin, you may need to define `-DGPIO_TFT_BACKLIGHT=<pin>` in CMake command line or `config.h`, and edit `config.h` to enable `#define BACKLIGHT_CONTROL`.

#### The display clears from white to black after starting fbcp-ili9341, but picture does not show up?

`fbcp-ili9341` runs a clear screen command at low speed as first thing after init, so if that goes through, it is a good sign. Try increasing `-DSPI_BUS_CLOCK_DIVISOR=` CMake option to a higher number to see if the display driving rate was too fast. Or try disabling DMA with `-DUSE_DMA_TRANSFERS=OFF` to see if this might be a DMA conflict.

#### Image does show up on display, but it freezes shortly afterwards

This suggests same as above, increase SPI bus divisor or troubleshoot disabling DMA. If DMA is detected to be the culprit, try changing up the DMA channels. Double check that `/boot/config.txt` does not have any `dtoverlay`s regarding other SPI display drivers or touch screen controllers, and that it does **NOT** have a `dtparam=spi=on` line in it - `fbcp-ili9341` does not use the Linux kernel SPI driver.

Make sure other `fbcp` programs are not running, or that another copy of `fbcp-ili9341` is not running on the background.

#### Image does show up on display, but when I start up program XYZ, the image freezes

This is likely caused by the program resizing the video resolution at runtime, which breaks DispmanX. See https://github.com/raspberrypi/userland/issues/461 for more details.

#### The driver is updating pixels on the display, but it looks all garbled

Double check the Data/Command (D/C) GPIO pin physically, and in CMake command line. Whenever `fbcp-ili9341` refers to pin numbers, they are always specified in BCM pin numbers. Try setting a higher `-DSPI_BUS_CLOCK_DIVISOR=` value to CMake. Make sure no other `fbcp` programs or SPI drivers or dtoverlays are enabled.

#### Colors look wrong on the display

![BGR vs RGB and inverted colors](/wrong_colors.jpg)

If the color channels are mixed (red is blue, blue is red, green is green) like shown on the left image, pass the CMake option `-DDISPLAY_SWAP_BGR=ON` to the build.

If the color intensities look wrong (white is black, black is white, color looks like a negative image) like seen in the middle image, pass the CMake option `-DDISPLAY_INVERT_COLORS=ON` to the build.

If the colors looks off in some other fashion, it is possible that the display is just being driven at a too high SPI bus speed, in which case try making the display run slower by choosing a higher `-DSPI_BUS_CLOCK_DIVISOR=` option to CMake. Especially on ILI9486 displays it has been observed that the colors on the display can become distorted if the display is run too fast beyond its maximum capability.

#### The screen is tearing when it updates

Unfortunately a limitation of SPI connected displays is that the VSYNC line signal is not available on the display controllers when they are running in SPI mode, so it is not possible to do vsync locked updates even if the SPI bus bandwidth on the display was fast enough. For example, the 4 ILI9341 displays I have can all be run faster than 75MHz so SPI bus bandwidth-wise all of them would be able to update a full frame in less than a vsync interval, but it is not possible to synchronize the updates to vsync since the display controllers do not report it. (If you do know of a display that does expose a vsync clock signal even in SPI mode, you can try implementing support to locking on to it)

You can choose between two distinct types of tearing artifacts: *straight line tearing* and *diagonal tearing*. Whichever looks better is a bit subjective, which is why both options exist. I prefer the straight line tearing artifact, it seems to be less intrusive than the diagonal tearing one. To toggle this, edit the option `#define DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE` in `config.h`. When this option is enabled, `fbcp-ili9341` produces straight line tearing, and consumes a few % more CPU power. By default Pi 3B builds with straight line tearing, and Pi Zero with the faster diagonal tearing.

To get tearing free updates, you should use a DPI display, or a good quality HDMI display. Beware that cheap small 3.5" HDMI displays such as KeDei do also tear - that is, even if they are controlled via HDMI, they don't actually seem to implement VSYNC timed internal operation.

#### Failed to allocate GPU memory!

`fbcp-ili9341` needs a few megabytes of GPU memory to function if DMA transfers are enabled. The [gpu_mem](https://www.raspberrypi.org/documentation/configuration/config-txt/memory.md) boot config option dictates how much of the Pi's memory area is allocated to the GPU. By default this is 64, which has been observed to not leave enough memory for `fbcp-ili9341` if HDMI is run at 1080p. If this error happens, try increasing GPU memory to e.g. 128MB by adding a line `gpu_mem=128` in `/boot/config.txt`.

#### Which SPI display should I buy to make sure it works best with fbcp-ili9341?

First, make sure the display is a 4-wire SPI and not a 3-wire one. `fbcp-ili9341` does not currently support 3-wire SPI. A display is 4-wire SPI if it has a Data/Control (DC) GPIO line that needs connecting.

Second is the consideration about display speed. Below is a performance chart of the different displays I have tested. Note that these are sample sizes of one, I don't know how much sample variance there exists. Also I don't know if it is likely that there exists big differences between displays with same controller from different manufacturers. At least the different ILI9341 displays that I have are all quite consistent on performance, whether they are from Adafruit or WaveShare or from BuyDisplay.com.

| Vendor | Size | Resolution | Controller | Rated SPI Bus Speed | Obtained Bus Speed | Frame Rate |
| ------ | ---- | ---------- | ---------- | ------------------- | ------------------ | -----------|
| [Adafruit PiTFT](https://www.adafruit.com/product/1601) | 2.8" | 240x320 | ILI9341 | 10MHz | 294MHz/4=73.50MHz | 59.81 fps |
| [Adafruit PiTFT](https://www.adafruit.com/product/2315) | 2.2" | 240x320 | ILI9340 | 15.15MHz | 338MHz/4=84.50MHz | 68.76 fps |
| [Adafruit PiTFT](https://www.adafruit.com/product/2097) | 3.5" | 320x480 | HX8357D | ? | 314MHz/6=52.33MHz | 21.29 fps |
| [Adafruit OLED](https://www.adafruit.com/product/1673) | 1.27" | 128x96 | SSD1351 |  20 MHz | 360MHz/20=18.00MHz | 91.55 fps |
| [Waveshare RPi LCD (B) IPS](https://www.amazon.co.uk/dp/B01N48NOXI/ref=pe_3187911_185740111_TE_item) | 3.5" | 320x480 | ILI9486 | 15.15MHz | 255MHz/8=31.88MHz | 12.97 fps |
| [BuyDisplay.com SPI TFT](https://www.buydisplay.com/default/serial-spi-3-2-inch-tft-lcd-module-display-ili9341-power-than-sainsmart) | 3.2" | 240x320 | ILI9341 | 10MHz | 310MHz/4=77.50MHz | 63.07 fps |
| [Arduino A000096 LCD](https://store.arduino.cc/arduino-lcd-screen) | 1.77" | 128x160 | ST7735R | 15.15MHz | 355MHz/6=59.16MHz | 180.56 fps |
| [Tontec MZ61581-PI-EXT 2016.1.28](https://www.ebay.com/p/Tontec-3-5-Inches-Touch-Screen-for-Raspberry-Pi-Display-TFT-Monitor-480x320-LCD/1649448059) | 3.5" | 320x480 | MZ61581 | 128MHz | 280MHz/2=140.00MHz | 56.97 fps |

In this list, *Rated SPI Bus Speed* is the maximum clock speed that the display controller is rated to run at. The *Obtained Bus Speed* column lists the fastest SPI bus speed that was achieved in practice, and the `core_freq` BCM Core speed and SPI Clock Divider `CDIV` setting that was used to achieve that rate. Note how most display controllers can generally be driven much faster than what they are officially rated at in their spec sheets.

The *Frame Rate* column shows the worst case frame rate when full screen updates are being performed. This occurs for example when watching fullscreen video (that is not a flat colored cartoon). Because `fbcp-ili341` only sends over the pixels that have changed, displays such as HX8357D and ILI9486 can still be used to play many games at 60fps. Retro games work especially well.

All the ILI9341 displays work nice and super fast at ~70-80MHz. My WaveShare 3.5" 320x480 ILI9486 display runs really slow compared to its pixel resolution, ~32MHz only. See [fbcp-ili9341 ported to ILI9486 WaveShare 3.5" (B) SpotPear 320x480 SPI display](https://www.youtube.com/watch?v=dqOLIHOjLq4) for a video of this display in action. Adafruit's 320x480 3.5" HX8357D PiTFTs is ~64% faster in comparison.

If manufacturing variances turn out not to be high between copies, and you'd like to have a bigger 320x480 display instead of a 240x320 one, then it is recommended to avoid ILI9486, they indeed are slow.

The Tontec MZ61581 controller based 320x480 3.5" display on the other hand can be driven insanely fast at up to 140MHz! These seem to be quite hard to come by though and they are expensive. Tontec seems to have gone out of business and for example the domain itontec.com from which the supplied instructions sheet asks to download original drivers from is no longer registered. I was able to find one from eBay for testing.

Search around, or ask the manufacturer of the display what the maximum SPI bus speed is for the device. This is the most important aspect to getting good frame rates, but unfortunately most web links never state the SPI speed rating, or they state it ridiculously low like in the spec sheets. Try and buy to see, or ask in some community forums from people who already have a particular display to find out what SPI bus speed it can achieve.

One might think that since Pi Zero is slower than a Pi 3, the SPI bus speed might not matter as much when running on a Pi Zero, but the effect is rather the opposite. To get good framerates on a Pi Zero, it should be paired with a display with as high SPI bus speed capability as possible. This is because the higher the SPI bus speed is, the more autonomously a DMA controller can drive it without CPU intervention. For the same reason, the interlacing technique does not (currently at least) perform well on a Pi Zero, so it is disabled there by default. ILI9341s run well on Pi Zero, ILI9486 on the other hand is quite difficult to combine with a Pi Zero.

Ultimately, it should be noted that parallel displays (DPI) are the proper method for getting fast framerates easily. SPI displays should only be preferred if display form factor is important and a desired product might only exist as SPI and not as DPI, or the number of GPIO pins that are available on the Pi is scarce that sacrificing dozens of pins to RGB data is not feasible.

### Resources

The following links proved helpful when writing this:
 - [ARM BCM2835 Peripherals Manual PDF](https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf),
 - [ILI9341 Display Controller Manual PDF](https://cdn-shop.adafruit.com/datasheets/ILI9341.pdf),
 - [notro/fbtft](https://github.com/notro/fbtft): Linux Framebuffer drivers for small TFT LCD display modules,
 - [BCM2835 driver](http://www.airspayce.com/mikem/bcm2835/) for Raspberry Pi,
 - [tasanakorn/rpi-fbcp](https://github.com/tasanakorn/rpi-fbcp), original framebuffer driver,
 - [tasanakorn/rpi-fbcp/#16](https://github.com/tasanakorn/rpi-fbcp/issues/16), discussion about performance,
 - [Tomáš Suk, Cyril Höschl IV, and Jan Flusser, Rectangular Decomposition of Binary Images.](http://library.utia.cas.cz/separaty/2012/ZOI/suk-rectangular%20decomposition%20of%20binary%20images.pdf), a useful research paper about merging monochrome bitmap images to rectangles, which gave good ideas for optimizing SPI span merges across multiple scan lines,
 - [VC DispmanX source code](https://github.com/raspberrypi/userland/blob/master/interface/vmcs_host/vc_vchi_dispmanx.c) (more or less the only official documentation bit on DispmanX I could ever find)

### License

This driver is licensed under the MIT License. See LICENSE.txt. In nonlegal terms, it's yours for both free and commercial projects, DIY packages, kickstarters, Etsys and Ebays, and you don't owe back a dime. Feel free to apply and derive as you wish.

If you found `fbcp-ili9341` useful, it makes me happy to hear back about the projects it found a home in. If you did a build or a project where `fbcp-ili9341` worked out, it'd be great to see a video or some photos or read about your experiences.

I hope you build something you enjoy!
