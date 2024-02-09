#include "config.h"

#if defined(GC9A01)

#include "spi.h"

#include <memory.h>
#include <stdio.h>

void InitGC9A01()
{
  // If a Reset pin is defined, toggle it briefly high->low->high to enable the device. Some devices do not have a reset pin, in which case compile with GPIO_TFT_RESET_PIN left undefined.
#if defined(GPIO_TFT_RESET_PIN) && GPIO_TFT_RESET_PIN >= 0
  printf("Resetting display at reset GPIO pin %d\n", GPIO_TFT_RESET_PIN);
  SET_GPIO_MODE(GPIO_TFT_RESET_PIN, 1);
  SET_GPIO(GPIO_TFT_RESET_PIN);
  usleep(120 * 1000);
  CLEAR_GPIO(GPIO_TFT_RESET_PIN);
  usleep(120 * 1000);
  SET_GPIO(GPIO_TFT_RESET_PIN);
  usleep(120 * 1000);
#endif

  // Do the initialization with a very low SPI bus speed, so that it will succeed even if the bus speed chosen by the user is too high.
  spi->clk = 34;
  __sync_synchronize();

  BEGIN_SPI_COMMUNICATION();
  {
    // The init sequence reference:
    // https://www.waveshare.com/wiki/File:LCD_Module_code.zip
    // File: RaspberryPi/c/lib/LCD/LCD_1in28.c
    SPI_TRANSFER(0xEF);
    SPI_TRANSFER(0xEB, 0x14);

    SPI_TRANSFER(0xFE);
    SPI_TRANSFER(0xEF);

    SPI_TRANSFER(0xEB, 0x14);

    SPI_TRANSFER(0x84, 0x40);

    SPI_TRANSFER(0x85, 0xFF);

    SPI_TRANSFER(0x86, 0xFF);

    SPI_TRANSFER(0x87, 0xFF);

    SPI_TRANSFER(0x88, 0x0A);

    SPI_TRANSFER(0x89, 0x21);

    SPI_TRANSFER(0x8A, 0x00);

    SPI_TRANSFER(0x8B, 0x80);

    SPI_TRANSFER(0x8C, 0x01);

    SPI_TRANSFER(0x8D, 0x01);

    SPI_TRANSFER(0x8E, 0xFF);

    SPI_TRANSFER(0x8F, 0xFF);


    SPI_TRANSFER(0xB6, 0x00, 0x20);

    SPI_TRANSFER(0x36, 0x08);//Set as vertical screen

    SPI_TRANSFER(0x3A, 0x05);


    SPI_TRANSFER(0x90, 0x08, 0x08, 0x08, 0x08);

    SPI_TRANSFER(0xBD, 0x06);

    SPI_TRANSFER(0xBC, 0x00);

    SPI_TRANSFER(0xFF, 0x60, 0x01, 0x04);

    SPI_TRANSFER(0xC3, 0x13);

    SPI_TRANSFER(0xC4, 0x13);

    SPI_TRANSFER(0xC9, 0x22);

    SPI_TRANSFER(0xBE, 0x11);

    SPI_TRANSFER(0xE1, 0x10, 0x0E);

    SPI_TRANSFER(0xDF, 0x21, 0x0c, 0x02);

    SPI_TRANSFER(0xF0, 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A);

    SPI_TRANSFER(0xF1, 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F);


    SPI_TRANSFER(0xF2, 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A);

    SPI_TRANSFER(0xF3, 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F);

    SPI_TRANSFER(0xED, 0x1B, 0x0B);

    SPI_TRANSFER(0xAE, 0x77);

    SPI_TRANSFER(0xCD, 0x63);


    SPI_TRANSFER(0x70, 0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03);

    SPI_TRANSFER(0xE8, 0x34);

    SPI_TRANSFER(0x62, 0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70);

    SPI_TRANSFER(0x63, 0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70);

    SPI_TRANSFER(0x64, 0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07);

    SPI_TRANSFER(0x66, 0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00);

    SPI_TRANSFER(0x67, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98);

    SPI_TRANSFER(0x74, 0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00);

    SPI_TRANSFER(0x98, 0x3e, 0x07);

    SPI_TRANSFER(0x35);

    SPI_TRANSFER(0x21);

    SPI_TRANSFER(0x11);
    usleep(120 * 1000);
    SPI_TRANSFER(0x29);
    usleep(20 * 1000);

    ClearScreen();
  }
#ifndef USE_DMA_TRANSFERS // For DMA transfers, keep SPI CS & TA active.
  END_SPI_COMMUNICATION();
#endif

  // And speed up to the desired operation speed finally after init is done.
  usleep(10 * 1000); // Delay a bit before restoring CLK, or otherwise this has been observed to cause the display not init if done back to back after the clear operation above.
  spi->clk = SPI_BUS_CLOCK_DIVISOR;
}

void TurnBacklightOn()
{
#if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
  SET_GPIO_MODE(GPIO_TFT_BACKLIGHT, 0x01); // Set backlight pin to digital 0/1 output mode (0x01) in case it had been PWM controlled
  SET_GPIO(GPIO_TFT_BACKLIGHT);            // And turn the backlight on.
#endif
}

void TurnBacklightOff()
{
#if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
  SET_GPIO_MODE(GPIO_TFT_BACKLIGHT, 0x01); // Set backlight pin to digital 0/1 output mode (0x01) in case it had been PWM controlled
  CLEAR_GPIO(GPIO_TFT_BACKLIGHT);          // And turn the backlight off.
#endif
}

void TurnDisplayOff()
{
  TurnBacklightOff();
}

void TurnDisplayOn()
{
  TurnBacklightOn();
}

void DeinitSPIDisplay()
{
  ClearScreen();
  SPI_TRANSFER(/*Display OFF*/ 0x28);
  TurnBacklightOff();
}

#endif
