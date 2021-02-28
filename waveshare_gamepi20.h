#pragma once

// Data specific to WaveShare GamePi20 320x240 2.0inch IPS LCD, https://www.waveshare.com/wiki/GamePi20
#ifdef WAVESHARE_GAMEPI20

#if !defined(GPIO_TFT_DATA_CONTROL)
#define GPIO_TFT_DATA_CONTROL 25
#endif

#if !defined(GPIO_TFT_BACKLIGHT)
#define GPIO_TFT_BACKLIGHT 24
#endif

#if !defined(GPIO_TFT_RESET_PIN)
#define GPIO_TFT_RESET_PIN 27
#endif

#endif