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

### Limitations

While the performance of the driver is great and 60fps is just lovable, there are a number of current limitations to the code:

###### Specific to BCM2835
 - The codebase has been written with a hardcoded assumption of the ARM BCM2835 chip. Since it bypasses the generic drivers for SPI and GPIO, it will definitely not work out of the box on any other display controllers than the ones mentioned earlier. It might not work on other Pis than the ones mentioned earlier either. The driver also assumes it is the exclusive user of the SPI bus, which means that at the moment, touch controllers are not supported and should be disabled.

###### No rendered frame delivery via events from VideoCore IV GPU
 - The codebase captures screen framebuffers by snapshotting via the VideoCore `vc_dispmanx_snapshot()` API, and the obtained pixels are then routed on to the SPI-based display. This kind of polling is performed, since there does not exist an event-based mechanism to get new frames from the GPU as they are produced. The result is inefficient and can easily cause stuttering, since different applications produce frames at different paces. For example an emulated PAL NES game would be producing frames at fixed 50Hz, a native GLES2 game at fixed 60Hz, or perhaps at variable times depending on the GPU workload. **Ideally the code would ask the VideoCore API to receive finished frames in callback notifications immediately after they are rendered**, but this kind of functionality does not exist in the current GPU driver stack. In the absence of such event delivery mechanism, the code has to resort to polling snapshots of the display framebuffer using carefully timed heuristics to balance between keeping latency and stuttering low, while not causing excessive power consumption. These heuristics keep continuously guessing the update rate of the animation on screen, and they have been tuned to ensure that CPU usage goes down to 0% when there is no detected activity on screen, but it is certainly not perfect. This GPU limitation is discussed at https://github.com/raspberrypi/userland/issues/440. If you'd like to see fbcp-ili9341 operation reduce latency, stuttering and power consumption, please throw a (kind!) comment or a thumbs up emoji in that bug thread to share that you care about this, and perhaps Raspberry Pi engineers might pick the improvement up on the development roadmap.

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
- `-DWAVESHARE35B_ILI9486=ON`: If specified, targets a Waveshare 3.5" 480x320 display, or possibly any other generic ILI9486 controller. This support is experimental.
- `-DILI9486=ON`: If you have any other generic ILI9486 display, pass this directive.
- `-DUSE_DMA_TRANSFERS=OFF`: If specified, disables using DMA transfers. Pass this if DMA is giving some issues.
- `-DDMA_TX_CHANNEL=<num>`: Specifies the DMA channel number to use for SPI send commands. Change this if you find a DMA channel conflict.
- `-DDMA_RX_CHANNEL=<num>`: Specifies the DMA channel number to use for SPI receive commands. Change this if you find a DMA channel conflict.
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

If the size of the default HDMI output `/dev/fb0` framebuffer differs from the 320x240 resolution of the display, the source size will need to be rescaled to fit to 320x240 pixels. `fbcp-ili9341` will manage setting up this rescaling if needed, and it will be done by the GPU, so performance should not be impacted too much. However if the resolutions do not match, small text will probably appear illegible. The resizing will be done in aspect ratio preserving manner, so if the aspect ratios do not match, either horizontal or vertical black borders will appear on the display. If you do not use the HDMI output at all, it is probably best to configure the HDMI output to match the 320x240 size so that rescaling will not be needed. This can be done by setting the following lines in `/boot/config.txt`:

```
hdmi_group=2
hdmi_mode=87
hdmi_cvt=320 240 60 1 0 0 0
hdmi_force_hotplug=1
```

These lines hint native applications about the default display mode, and let them render to the native resolution of the TFT display. This can however prevent the use of the HDMI connector, if the HDMI connected display does not support such a small resolution. As a compromise, if both HDMI and SPI displays want to be used at the same time, some other compatible resolution such as 640x480 can be used. See [Raspberry Pi HDMI documentation](https://www.raspberrypi.org/documentation/configuration/config-txt/video.md) for the available options to do this.

##### Tuning Performance

There are three ways to configure the throughput performance of the display driver.

1. The main configuration is the SPI bus `CDIV` (Clock DIVider) setting which controls the MHz rate of the SPI0 controller. By default this is set to value `CDIV=6`. To adjust this value, pass the directive `-DSPI_BUS_CLOCK_DIVISOR=even_number` in CMake command line. Possible values are even numbers `2`, `4`, `6`, `8`, `...`. Smaller values result in higher bus speeds. Usually for ILI9341, `CDIV=6` seems good, but if one underclocks `core_freq` to around 300MHz, it looks like `CDIV=4` can also be used. On ILI9486, it seems that `CDIV=14` or `CDIV=16` are good starting values.

2. Ensure turbo speed. This is critical for good frame rates. On the Raspberry Pi 3 Model B, the SPI bus runs at 400MHz (divided by `CDIV`) **if** there is enough power provided to the Pi, and if the CPU temperature does not exceed thermal limits. If for some reason under-voltage protection is kicking in even when enough power should be fed, you can [force-enable turbo when low voltage is present](https://www.raspberrypi.org/forums/viewtopic.php?f=29&t=82373) by setting the value `avoid_warnings=2` in the file `/boot/config.txt`. The effect of turbo speed on performance is significant, 400MHz vs non-turbo 250MHz, which comes out to +60% of more bandwidth. Getting 60fps in Quake, Sonic or Tyrian requires this turbo frequency, but NES and C64 emulators can often reach 60fps even with the stock 250MHz.

3. **Underclock** the core. The `core_freq=` option in `/boot/config.txt` affects also the SPI bus speed. Setting a **smaller** core frequency than the default turbo 400MHz can enable using a smaller clock divider to get a better SPI bus speed. For example, if with default `core_freq=400` SPI `CDIV=8` works (resulting in SPI bus speed `400MHz/8=50MHz`), but `CDIV=6` does not (`400MHz/6=66.67MHz` was too much), you can try lowering `core_freq=360` and set `CDIV=6` to get an effective SPI bus speed of `360MHz/6=60MHz`, a middle ground between the two that might perhaps work. Balancing `core_freq=` and `CDIV` options allows one to find the maximum SPI bus speed up to the last few kHz that the display controller can tolerate.

### Statistics Overlay

By default `fbcp-ili9341` builds with a statistics overlay enabled. See the video [fbcp-ili9341 ported to ILI9486 WaveShare 3.5" (B) SpotPear 320x480 SPI display](https://www.youtube.com/watch?v=dqOLIHOjLq4) to find details on what each field means. Remove the `#define STATISTICS` option in `config.h` to disable displaying the statistics.

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
