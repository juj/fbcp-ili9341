#pragma once

// Data specific to the PiTFT 2.8 display
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
#define GPIO_TFT_DATA_CONTROL 25  /*!< Version 1, Pin P1-22, PiTFT 2.8 resistive Data/Control pin */

// Data specific to the ILI9341 controller
#define DISPLAY_BYTESPERPIXEL 2
#define DISPLAY_SET_CURSOR_X 0x2A
#define DISPLAY_SET_CURSOR_Y 0x2B
#define DISPLAY_WRITE_PIXELS 0x2C

void InitILI9341(void);
#define InitSPIDisplay InitILI9341
