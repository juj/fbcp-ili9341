#pragma once

// Data specific to WaveShare 240x240, 1.28 inch ISP LCD GC9A01, https://www.waveshare.net/w/upload/5/5e/GC9A01A.pdf
#ifdef WAVESHARE_GC9A01

#if !defined(GPIO_TFT_DATA_CONTROL)
#define GPIO_TFT_DATA_CONTROL 25
#endif

#if !defined(GPIO_TFT_BACKLIGHT)
#define GPIO_TFT_BACKLIGHT 18
#endif

#if !defined(GPIO_TFT_RESET_PIN)
#define GPIO_TFT_RESET_PIN 27
#endif

#undef DISPLAY_OUTPUT_LANDSCAPE
#endif
