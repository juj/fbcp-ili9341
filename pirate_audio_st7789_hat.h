#pragma once

// Data specific to Pirate Audio series 240x240, 1.3inch IPS LCD ST7789 hat, https://shop.pimoroni.com/collections/pirate-audio
#ifdef PIRATE_AUDIO_ST7789_HAT

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
