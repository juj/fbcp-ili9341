#pragma once

// Data specific to Adafruit's PiTFT 1.14" display
#ifdef ADAFRUIT_ST7789_PITFT_114

#if !defined(GPIO_TFT_DATA_CONTROL)
// Adafruit 1.14" 135x240 has display control on pin 25: https://learn.adafruit.com/adafruit-mini-pitft-135x240-color-tft-add-on-for-raspberry-pi/pinouts
#define GPIO_TFT_DATA_CONTROL 25
#endif

#if !defined(GPIO_TFT_BACKLIGHT)
// Adafruit 1.14" 135x240 has backlight on pin 22: https://learn.adafruit.com/adafruit-mini-pitft-135x240-color-tft-add-on-for-raspberry-pi/pinouts
#define GPIO_TFT_BACKLIGHT 22
#endif

#define DISPLAY_SPI_DRIVE_SETTINGS (1 | BCM2835_SPI0_CS_CPOL | BCM2835_SPI0_CS_CPHA)

#endif
