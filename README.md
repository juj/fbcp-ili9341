# Introduction

This repository implements a driver for the SPI-based [Adafruit 2.8" 320x240 TFT w/ Touch screen for Raspberry Pi](https://www.adafruit.com/product/1601).

![PiTFT display](/example.jpg "Adafruit PiTFT 2.8 with ILI9341 controller")

The work was motivated by curiosity after seeing this series of videos on the RetroManCave YouTube channel:
 - [RetroManCave: Waveshare 3.5" Raspberry Pi Screen | Review](https://www.youtube.com/watch?v=SGMC0t33C50)
 - [RetroManCave: Waveshare 3.2" vs 3.5" LCD screen gaming test | Raspberry Pi / RetroPie](https://www.youtube.com/watch?v=8bazEcXemiA)
 - [Elecrow 5 Inch LCD Review | RetroPie & Raspberry Pi](https://www.youtube.com/watch?v=8VgNBDMOssg)

In these videos, the SPI (GPIO) bus is referred to being the bottleneck. SPI based displays generally run at 31.25MHz (or at 62.5MHz if you're lucky) on the Pi, translating to an upper bound of 31.25Mibits/second of bandwidth. On a 320x240x16bpp display this would mean a theoretical update rate ceiling of 25.43 fps.

The repository [rpi-fbcp](https://github.com/tasanakorn/rpi-fbcp) implements the display driver that is used by the popular Retropie projects on these screens. I was taken by how remarkably approachable the code is written. Testing it out on my own Adafruit 2.8" 320x240 display, at the time of writing, it achieves a stable rate of about 16 fps (with SPI bus at 31.25MHz). This is only 62.9% of the theoretical maximum of the bus. Could this be optimized, or is the SPI bus really so hopeless as claimed?

**Update 2017-11-28**: Turns out that this napkin math is not quite accurate due to two features. First, [the BCM2835 chip clocks itself to a turbo speed of 400 MHz on the Pi 3 Model B](https://github.com/raspberrypi/linux/issues/2094), from the base default 250 MHz referred to in the BCM2835 documentation, leading to +60% faster bandwidth. Second, there seems to be [a typo in the BCM2835 documentation](https://elinux.org/BCM2835_datasheet_errata#p156) that misstates that allowed clock dividers for the SPI chip would need to be powers of 2, whereas in practice the possible clock dividers only need to be even numbers. In particular, a CDIV value of 6 is possible to used and works well, over the original slower CDIV value 8 that corresponded to the figure 31.25 MHz referred to above, or the CDIV value 4 that proved too fast for the display hardware. Putting these two together, with CDIV=6 and a 400MHz clock, the theoretical SPI bandwidth comes out to 66.666 MiBits/second, giving a ceiling of 54.25fps.

### Results

The `fbcp-ili9341` software is a drop-in replacement for the stock `fbcp` display driver program. It is named as such since it was written to operate specifically against the ILI9341 display controller that the Adafruit 2.8" PiTFT uses, although nothing in the code is fundamentally specific to the ILI9341 alone, so it should be possible to be ported to run on other display controllers as well.

Whereas the original `fbcp` refreshed at fixed 16fps, this updated driver can achieve a 60fps update rate, depending on the content that is being displayed. Check out these videos for examples of the driver in action:

 - First version: [fbcp-ili9341 driver first demo](https://youtu.be/h1jhuR-oZm0)
 - Second updated version with statistics overlay: [fbcp-ili9341 SPI display driver on Adafruit PiTFT 2.8"](http://youtu.be/rKSH048XRjA)

### How It Works

Given the established theoretical update rate ceiling, how come this seems to be able to update at up to 60fps? The way this is achieved is by what could be called *adaptive display stream updates*. Instead of uploading each pixel at each display refresh cycle, only the actually modified pixels on screen are submitted to the display. This is doable because the ILI9341 controller (as many other popular controllers) has communication interface functions that allow specifying partial screen updates, down to subrectangles or even individual pixel levels. This allows beating the bandwidth limit: for example in Quake, even though it is a fast pacing game, on average only about 46% of all pixels on screen change each rendered frame. Some parts, such as the UI stay practically constant across multiple frames.

Other optimizations are also utilized to squeeze out even more performance:
 - The program directly communicates with the BCM2835 ARM Peripherals controller registers, bypassing any Linux software stack. This speeds up communication on the bus, although at great expense of power consumption, as currently neither DMA or hardware interrupts are used. It is possible that the adaptive display stream update technique would not benefit from DMA transfers much, since the optimized communication protocol requires flipping the Data/Control bus quite frequently, something that would need flushing the DMA queue every time it occurs.
 - Good old **interlacing** is added into the mix: if the amount of pixels that needs updating is detected to be too much that the SPI bus cannot handle it, the driver adaptively resorts to doing an interlaced update, uploading even and odd scanlines at subsequent frames. Once the number of pending pixels to write returns to manageable amounts, progressive updating is resumed. This effectively doubles the display update rate.
 - A dedicated SPI communication thread is used in order to keep the SPI communication queue as warm as possible at all times. This thread was written with the idea that might be refactorable to utilize kernel side SPI interrupts in the future in order to reduce CPU overhead, although the feasibility of this approach is yet unknown. Without SPI interrupts, this thread effectively consumes 100% of a single CPU core when there are a lot of updates to perform, which can make this driver infeasible to use in practice, depending on your use case.
 - A number of micro-optimization techniques are used, such as batch updating spans of pixels, merging disjoint-but-close spans of pixels on the same scanline, and latching Column and Page End Addresses to bottom-right corner of the display to be able to cut CASET and PASET messages in mid-communication.

### Limitations

While the performance of the driver is great and 60fps is just lovable, there are a number of current limitations to the code that can make this unusable in practice:

###### Specific to ILI9341 and BCM2835
 - The codebase has been written with a hardcoded assumption of the ILI9341 controller and the ARM BCM2835 chip. Since it bypasses the generic drivers for SPI and GPIO, it will definitely not work out of the box on any other displays. It might not even work on other Pis than the Pi 3 Model B that I have. The driver also assumes it is the exclusive user of the SPI bus and the pins are hardcoded to follow the AdaFruit 2.8" PiTFT shield.

###### No rendered frame delivery via events from VideoCore IV GPU
 - The codebase reuses the approach from [tasanakorn/rpi-fbcp](https://github.com/tasanakorn/rpi-fbcp) where the framebuffer on the screen is obtained by snapshotting via the VideoCore `vc_dispmanx_snapshot()` API, and the obtained pixels are then routed on to the SPI-based display. This kind of polling is performed, since there does not seem to exist an event-based mechanism to get new frames from the GPU as they are produced. The result is very inefficient and can easily cause stuttering, since different applications produce frames at different paces. For example an emulated PAL NES game would be producing frames at fixed 50Hz, a native GLES2 game at fixed 60Hz, or perhaps at variable times depending on the GPU workload. **Ideally the code would ask the VideoCore API to receive finished frames in callback notifications immediately after they are rendered**, but this kind of functionality might not exist in the current GPU driver stack (if it does, please let me know!). In the absence of such event delivery mechanism, the code has to resort to polling snapshots of the display framebuffer using carefully timed heuristics to balance between keeping latency and stuttering low, while not causing excessive power consumption. These heuristics keep continuously guessing the update rate of the animation on screen, and they have been tuned to ensure that CPU usage goes down to 0% when there is no detected activity on screen, but it is certainly not perfect.

###### A dedicated thread hand holds the SPI FIFO
 - To maximize performance and ensure that the SPI FIFO is efficiently saturated, a dedicated SPI processing pthread is used. This thread spinwaits to observe the SPI FIFO transactions, so the CPU % consumption of this thread linearly correlates to the utilization rate of the SPI bus. If the amount of activity on the screen exceeds the SPI bus bandwidth, this will mean that the SPI thread will sit at 100% utilization. To perform efficient adaptive display stream updates, there are a lot of distinct command+data message pairs that need to be sent, which possibly would prevent the use of the DMA hardware, since a DMA transaction would not be aware to toggle the Data/Control GPIO pin while it transfers data. Depending on how much overhead the DMA hardware has, it might be possible to utilize it for the more longer spans of data, while keeping short spans on the main CPU. In addition, it might be possible to optimize by migrating the display driver to run on the Linux kernel side, utilizing SPI interrupts to process the SPI task queue in a more power efficient manner.

The first performance issue might be addressed in a software update from VideoCore GPU driver (or by use of a smarter Linux API, if such a thing might exist?), while the second performance issue might be fixable by rewriting the driver to run as a kernel module, while accessing the BCM2835 DMA and SPI interrupts hardware. Without these issues resolved, expect overall CPU consumption to be up to 120% - such is the price of 60fps at the moment.

**Update 2017-11-28**: Had a stab at rewriting the application as a kernel module to utilize interrupts. While this worked well to reduce CPU usage close to 0%, it had an adverse effect of losing quite a bit of available bandwidth, due to difficulty keeping the SPI FIFO fully running (possibly because of some latency that processing interrupt callbacks might cause). Also, SPI interrupts firing at a high rate has a chance of starving other interrupts, and it was observed that audio playback began to stutter. Attempting a kernel interrupts rewrite should pay close attention to these aspects.

### Should I Use This?

As a caveat, this was written mostly in one weekend as a hobby programming activity, so it's not a continuously maintained driver.

If your target application doesn't mind high CPU utilization on the background and you have the compatible hardware, then perhaps yes. If your Pi is on battery, this will eat through power pretty quick. To echo RetroManCave's observation as well, you should really use a HDMI display over an SPI-based one, since then the dedicated VideoCore GPU handles all the trouble of presenting frames, plus you will get vsync out of the box. The smallest HDMI displays for Raspberry Pis on the market seem to be [the size of 3.5" 480x320](https://www.raspberrypi.org/forums/viewtopic.php?t=175616), so if that's not too large and your project can manage the HDMI connector hump at the back, there's probably no reason to use SPI.

Perhaps some day if both of the above mentioned performance limitations are optimized away, then high refresh rates on SPI based displays could become power efficient.

### Installation

Check the following topics to set up the driver.

##### Boot configuration

This driver does not utilize the [notro/fbtft](https://github.com/notro/fbtft) framebuffer driver, so that can be disabled if active. That is, if your `/boot/config.txt` file has a line that starts with `dtoverlay=pitft28r, ...`, it can be removed. There is no harm in keeping it though if you plan on e.g. being able to switch back and forth between `fbcp` and `fbcp-ili9341`.

This program neither needs the default SPI driver enabled, so a line such as `dtparam=spi=on` in `/boot/config.txt` can likewise be removed. (In the tested kernel version of this program, that line would conflict since the program would then register the hardware SPI interrupts for itself)

##### Building and running

Run in the console of your Raspberry Pi:

```bash
git clone https://github.com/juj/fbcp-ili9341.git
cd fbcp-ili9341
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
sudo ./fbcp-ili9341
```

If you have been running existing `fbcp` driver, make sure to remove that e.g. via a `sudo pkill fbcp` first (while running in SSH prompt or connected to a HDMI display), these two cannot run at the same time.

##### Configuring build options

Edit the file [config.h](https://github.com/juj/fbcp-ili9341/blob/master/config.h) directly to customize different build options. In particular the option `#define STATISTICS` can be interesting to try to enable.

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

1. The main configuration is the SPI bus `CDIV` (Clock DIVider) setting which controls the MHz rate of the SPI0 controller. By default this is set to value `CDIV=6`. To adjust this value, edit the line `#define SPI_BUS_CLOCK_DIVISOR 6` in the file `config.h`. Possible values are even numbers `2`, `4`, `6`, `8`, `...`. Smaller values result in higher bus speeds.

2. Ensure turbo speed. This is critical for good frame rates. On the Raspberry Pi 3 Model B, the SPI bus runs at 400MHz (divided by `CDIV`) **if** there is enough power provided to the Pi, and if the CPU temperature does not exceed thermal limits. Run the terminal command `vcgencmd measure_clock core` to show the current SPI bus speed, or build `fbcp-ili9341` with `#define STATISTICS` to display the bus speed on the screen (see next section below). If for some reason under-voltage protection is kicking in even when enough power should be fed, you can [force-enable turbo when low voltage is present](https://www.raspberrypi.org/forums/viewtopic.php?f=29&t=82373) by setting the value `avoid_warnings=2` in the file `/boot/config.txt`. The effect of turbo speed on performance is significant, 400MHz vs non-turbo 250MHz, which comes out to +60% of more bandwidth. Getting 60fps in Quake, Sonic or Tyrian requires this turbo frequency, but NES and C64 emulators can often reach 60fps even with the stock 250MHz.

3. Overclock the core. Adjusting the `core_freq=400` option in `/boot/config.txt` affects also the SPI bus speed. There exists numerous Pi overclocking articles ([[1]](https://haydenjames.io/raspberry-pi-3-overclock/), [[2]](https://github.com/retropie/retropie-setup/wiki/Overclocking)), [[3]](https://www.jackenhack.com/raspberry-pi-3-overclocking/) that refer to the Pi being able to reach a core frequency of 500 MHz over the turbo 400 MHz, gaining yet extra +25% of potential SPI bandwidth. However when trying these out in practice, it looks like at least that my copy of the Adafruit ILI9341 PiTFT 2.8" display would not handle a bus speed of 500MHz/6=83.33MHz at all. Smaller bus speeds, such as 450MHz/6=75MHz proved to be too much just by a bit as well, with the display contents showing correctly about 95% of the time, and with small amounts of corrupted pixels showing up here and there every once in a while. Oddly, similar "once or twice per minute" types of artifacts even occured at tiny `core_freq=405` overclock, which led to hypothesizing that perhaps there is some other incompatibility or issue at play when changing the clocks, rather than the actual clock timings. If you have more success with this kind of approach, please let me know!

### Statistics Overlay

![Statistics Overlay](/statistics_overlay.jpg "Performance Statistics Overlay")

If the code is compiled with the `STATISTICS` config option enabled, a performance statistics overlay will be rendered on the top side of the screen. This ovelay displays the following information:

1. Current update frame rate. A suffix `p` or `i` denotes whether progressive (all pixels) or interlaced (every second scanline, alternating between evens and odds) updating is being performed. If a negative number, e.g. `-1` or `-2` appears in **red** after the frame rate, it means that the display driver missed updating a frame altogether (neither even nor odd scanlines of a frame were presented). Whenever such a frame skip number is not present, each frame is getting updated at least in interlaced manner.

2. Specifies the current utilization rate of the SPI communication bus. If this is 0%, the display bus is practically idle, and very few pixels are being updated on to the display. If this is 100% the bus is fully saturated and the display driver is at the communication limits, and performing interlaced screen updates, or skipping frames if needed.

3. Specifies the current utilization rate of the SPI bus in actual megabits/second. The theoretical upper bound of this value on the Raspberry Pi 3 Model B is `400MHz / CDIV * 8 / 9` mbits/second, where `CDIV=6` by default.

4. Tracks the current processor speed of the Pi main CPU, in MHz. On the Pi 3 Model B this typically varies between 600 MHz when idle, and 1200 MHz under load (if not overclocked).

5. Tracks the current processor speed of the auxiliary BCM2835 peripheral CPU, in MHz. On the Pi 3 Model B this is typically 400 MHz, although apparently it may be possible(?) for this to drop down to 250 MHz if power or thermal limits are reached.

6. The current CPU temperature.

7. This field tracks the amount of extra wasted CPU power utilization caused by [this VideoCore driver issue](https://github.com/raspberrypi/userland/issues/440).

### Future Work

There are a couple of interesting ideas that might be useful for tweaking further:

###### CDIV=4 ?

While developing, it was observed that the SPI bus speed capped at parameter CDIV=6 (400MHz/6=66MHz). Using the next faster bus speed, CDIV=4 (400MHz/4=100MHz), would work for about 80% of the time, with some visual artifacts occurring. I was left wondering whether these artifacts would be fixable by more carefully implemented timing in some part of the SPI update code, by tweaking either the SPI offsets or phases of the BCM2835 SPI master or the ILI9341 controller chips. Adding extra synchronization and sleeps in specific places in code seemed to alleviate the issues a little, but this could not be made perfect. There is a `#define SPI_BUS_CLOCK_DIVISOR 6` parameter in the code that can be adjusted to `4` to test how this behaves.

###### VideoCore VSync Callback?

The codebase does implement an option to use the VideoCore GPU vertical sync signal as the method to grab new frames. This was tested and quickly rejected at least for games emulators use, since these generally do not produce frames at strict 60Hz refresh rate. Depending on the use case, it might still be preferrable to use this signal, rather than polling. The option was left in the code, under an optional `#define USE_GPU_VSYNC` that one can enable if desired.

###### LoSSI mode superior to 4-line SPI?

The major source of performance problems in the utilized 4-line SPI protocol is that extra GPIO pin that dictates whether the currently sent byte on the SPI bus is a `Data` byte or a `Command` byte. Since SPI communication and this D/C pin need to be synchronized together for the bytes to be interpreted correctly, it means that the SPI FIFO needs to be flushed empty whenever the state of the D/C pin is to be toggled. The *adaptive display stream update* approach generates a lot of distinct Commands that need to be sent, so flushing the FIFO needs to be done often during a single frame. This prevents the utilization of DMA transfers for the task. In LoSSI SPI mode however, D/C information would be sent on the same line as the communication payload, which means that such kind of FIFO flushing would not need to be done. As result, display updates could theoretically be pushed directly via DMA transfers in one go. Observing the line using a logic analyzer, each flush costs about one byte worth of idle time on the bus, so DMA transfers would have a chance to avoid this altogether.

Using LoSSI mode over 4-line, i.e. sending D/C information as part of the MOSI line, expands the size of each payload byte from 8 bits to 9 bits, which might at a glance seem like a -11.11..% reduction in throughput. Coincidentally, the BCM2835 SPI controller has a bandwidth limiting behavior that in Polled and Interrupt modes (i.e. when not using DMA), there is a [one clock delay after each sent byte on the SPI line](http://www.jumpnowtek.com/rpi/Analyzing-raspberry-pi-spi-performance.html), which effectively means that this cost of the 9th bit is already being paid when using 4-line SPI without DMA (in DMA transfer mode, this 9th bit overhead is not present). Therefore wiring the hardware to run in LoSSI SPI mode and re-writing this driver program to run as a kernel module and to use DMA might be the technically superior solution - close to 0% CPU usage while attaining 100% SPI line saturation.

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
