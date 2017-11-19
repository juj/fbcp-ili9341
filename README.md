# fbcp-ili9341

This repository implements a driver for the SPI-based [Adafruit 2.8" 320x240 TFT w/ Touch screen for Raspberry Pi](https://www.adafruit.com/product/1601).

![PiTFT display](/example.jpg "Adafruit PiTFT 2.8 with ILI9341 controller")

The work was motivated by curiosity after seeing this series of videos on the RetroManCave YouTube channel:
 - [RetroManCave: Waveshare 3.5" Raspberry Pi Screen | Review](https://www.youtube.com/watch?v=SGMC0t33C50)
 - [RetroManCave: Waveshare 3.2" vs 3.5" LCD screen gaming test | Raspberry Pi / RetroPie](https://www.youtube.com/watch?v=8bazEcXemiA)
 - [Elecrow 5 Inch LCD Review | RetroPie & Raspberry Pi](https://www.youtube.com/watch?v=8VgNBDMOssg)

In these videos, the SPI (GPIO) bus is referred to being the bottleneck. SPI based displays generally run at 32MHz (or at 64MHz if you're lucky) on the Pi, translating to an upper bound of 32Mibits/second of bandwidth. On a 320x240x16bpp display this would mean a theoretical update rate ceiling of 26.0417 Hz.

The repository [rpi-fbcp](https://github.com/tasanakorn/rpi-fbcp) implements the display driver that is used by the popular Retropie projects on these screens. I was taken by how remarkably approachable the code is written. Testing it out on my own Adafruit 2.8" 320x240 display, at the time of writing, it achieves a stable rate of about 16fps (with SPI bus at 32MHz). This is only 60% of the theoretical maximum of the bus. Could this be optimized, or is the SPI bus really so hopeless as claimed?

### Results

The `fbcp-ili9341` software is a drop-in replacement for the stock `fbcp` display driver program. It is named as such since it was written to operate specifically against the ILI9341 display controller that the Adafruit 2.8" PiTFT uses, although nothing in the code is fundamentally specific to the ILI9341, so it should be possible to be ported to run on other display controllers as well.

Whereas the original `fbcp` refreshed at fixed 16fps, this updated driver can achieve a 60fps update rate, depending on the content that is being displayed. Check out this video for examples of the driver in action:

 - [YouTube: fbcp-ili9341 driver demo](https://youtu.be/h1jhuR-oZm0)

Some specific data points (SPI at 32MHz):

| Test                | Update Rate |
| ------------------- | ---------------- |
| Prince of Persia    | 60fps |
| Outrun              | 60fps |
| OpenTyrian          | 55-60fps |
| Sonic the Hedgehog  | 48-60fps |
| Quake               | 45-60fps |

### How It Works

Hey, hold on! If the maximum update rate of the bus is 26.0417 Hz, how come this seems to be able to update at up to 60fps? The way this is achieved is by what could be called *adaptive display stream updates*. Instead of uploading each pixel at each display refresh cycle, only the actually modified pixels on screen are submitted to the display. This is doable because the ILI9341 controller (as many other popular controllers) has communication interface functions that allow specifying partial screen updates, down to subrectangles or even individual pixel levels. This allows beating the bandwidth limit: for example in Quake, even though it is a fast pacing game, on average only about 46% of all pixels on screen change each rendered frame. Some parts, such as the UI stay practically constant across multiple frames.

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

The first performance issue might be addressed in a software update from VideoCore GPU driver (or by use of a smarter Linux API, if such a thing might exist?), while the second performance issue might be fixable by rewriting the driver to run as a kernel module, while accessing the BCM2835 DMA and SPI interrupts hardware. Without these issues resolved, expect overall CPU consumption to be at around 120% - such is the price of 60fps at the moment.

### Should I Use This?

As a caveat, this was written in one weekend as a hobby programming activity, so it's not a continuously maintained driver.

If your target application doesn't mind high CPU utilization on the background and you have the compatible hardware, then perhaps yes. If your Pi is on battery, this will eat through power pretty quick. To echo RetroManCave's observation as well, you should really use a HDMI display over an SPI-based one, since then the dedicated VideoCore GPU handles all the trouble of presenting frames, plus you will get vsync out of the box. The smallest HDMI displays for Raspberry Pis on the market seem to be [the size of 3.5" 480x320](https://www.raspberrypi.org/forums/viewtopic.php?t=175616), so if that's not too large and your project can manage the HDMI connector hump at the back, there's probably no reason to use SPI.

Perhaps some day if both of the above mentioned performance limitations are optimized away, then high refresh rates on SPI based displays could become power efficient.

### Installation

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

To set up the driver to launch at startup, edit the file `/etc/rc.local` in `sudo` mode, and add a line `sudo /path/to/fbcp-ili9341/build/fbcp-ili9341 &` to the end. Make note of the needed ampersand `&` at the end of that line.

### Future Work

There are a couple of interesting ideas that might be useful for tweaking further:

###### 64 MHz SPI Bus?

While developing, it was observed that using a twice as fast bus speed, 64MHz, over the safer 32MHz, would work for about 80% of the time, with some visual artifacts occurring. I was left wondering whether these artifacts would be fixable by more carefully implemented timing in some part of the SPI update code, or whether the hardware is just plain incompatible. Adding extra synchronization and sleeps in specific places in code seemed to alleviate the issues a little, but this could not be made perfect. There is a `const int busDivisor` parameter in the code that may be worth setting to `4` instead of `8` when testing and see if that works, since that could double the bus speed for double the performance.

###### VideoCore VSync Callback?

The codebase does implement an option to use the VideoCore GPU vertical sync signal as the method to grab new frames. This was tested and quickly rejected at least for games emulators use, since these generally do not produce frames at strict 60Hz refresh rate. Depending on the use case, it might still be preferrable to use this signal, rather than polling. The option was left in the code, under an optional `#define USE_GPU_VSYNC` that one can enable if desired.

###### Vertical Merging of Scanlines?

There exists a very promising research paper, [Tomáš Suk, Cyril Höschl IV, and Jan Flusser, Rectangular Decomposition of Binary Images.](http://library.utia.cas.cz/separaty/2012/ZOI/suk-rectangular%20decomposition%20of%20binary%20images.pdf) which points out that a simple vertical scanline merging technique, called ***Generalized delta-method (GDM)*** in the paper, would be efficient in decomposing monochrome raster images to a minimum number of rectangles. This technique might be useful in reducing the number of overall span restarts needed to communicate with the ILI9341 display controller, resulting in fewer SPI flushes needed. This could be combined with the fact that such submitted rectangles would not need to be disjoint, but they only would need to overlap all dirty pixels on the screen. There is a change that this could help create considerably longer spans, which in turn could make the DMA hardware more suitable to be used.

### Resources

The following links proved helpful when writing this:
 - [ARM BCM2835 Peripherals Manual PDF](https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf),
 - [ILI9341 Display Controller Manual PDF](https://cdn-shop.adafruit.com/datasheets/ILI9341.pdf),
 - [notro/fbtft](https://github.com/notro/fbtft): Linux Framebuffer drivers for small TFT LCD display modules,
 - [BCM2835 driver](http://www.airspayce.com/mikem/bcm2835/) for Raspberry Pi,
 - [tasanakorn/rpi-fbcp](https://github.com/tasanakorn/rpi-fbcp), original framebuffer driver,
 - [tasanakorn/rpi-fbcp/#16](https://github.com/tasanakorn/rpi-fbcp/issues/16), discussion about performance,
 - [VC DispmanX source code](https://github.com/raspberrypi/userland/blob/master/interface/vmcs_host/vc_vchi_dispmanx.c) (more or less the only official documentation bit on DispmanX I could ever find)
