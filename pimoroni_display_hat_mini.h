#pragma once

// Data specific to the Pimoroni Display HAT mini
// 320x240 2.0" ST7789V2 HAT
// https://shop.pimoroni.com/products/display-hat-mini

#ifdef PIMORONI_DISPLAY_HAT_MINI

#if !defined(GPIO_TFT_DATA_CONTROL)
#define GPIO_TFT_DATA_CONTROL 9
#endif

#if !defined(GPIO_TFT_BACKLIGHT)
#define GPIO_TFT_BACKLIGHT 13
#endif

#define DISPLAY_USES_CS1

// CS1 line is for the LCD
#define DISPLAY_SPI_DRIVE_SETTINGS (1)

#endif
