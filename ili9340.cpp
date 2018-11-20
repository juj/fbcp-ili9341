#include "config.h"

#ifdef ILI9340

#include "spi.h"

#include <memory.h>
#include <stdio.h>

void InitILI9340()
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
    // Ruthlessly copied from fbtft init script
    SPI_TRANSFER(0xEF, 0x03, 0x80, 0x02);
    SPI_TRANSFER(0xCF, 0x00 , 0XC1 , 0X30);
    SPI_TRANSFER(0xED, 0x64 , 0x03 , 0X12 , 0X81);
    SPI_TRANSFER(0xE8, 0x85 , 0x00 , 0x78);
    SPI_TRANSFER(0xCB, 0x39 , 0x2C , 0x00 , 0x34 , 0x02);
    SPI_TRANSFER(0xF7, 0x20);
    SPI_TRANSFER(0xEA, 0x00 , 0x00);

    // Power Control 1
    SPI_TRANSFER(0xC0, 0x23);

    // Power Control 2
    SPI_TRANSFER(0xC1, 0x10);

    // VCOM Control 1
    SPI_TRANSFER(0xC5, 0x3e, 0x28);

    // VCOM Control 2
    SPI_TRANSFER(0xC7, 0x86);

    // COLMOD: Pixel Format Set
    // 16 bits/pixel
    SPI_TRANSFER(0x3A, 0x55);

    // Frame Rate Control
    // Division ratio = fosc, Frame Rate = 79Hz
    SPI_TRANSFER(0xB1, 0x00, 0x18);

    // Display Function Control
    SPI_TRANSFER(0xB6, 0x08, 0x82, 0x27);

    // Gamma Function Disable
    SPI_TRANSFER(0xF2, 0x00);

    // Gamma curve selected 
    SPI_TRANSFER(0x26, 0x01);

    // Positive Gamma Correction
    SPI_TRANSFER(0xE0,
        0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
        0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00);

    // Negative Gamma Correction
    SPI_TRANSFER(0xE1,
        0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
        0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F);

    // Sleep OUT
    SPI_TRANSFER(0x11);

    usleep(120 * 1000);
    

    // Display ON
    SPI_TRANSFER(0x29);

#if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
    printf("Setting TFT backlight on at pin %d\n", GPIO_TFT_BACKLIGHT);
    TurnBacklightOn();
#endif

    // Some wonky effects to try out:
//    SPI_TRANSFER(0x20/*Display Inversion OFF*/);
//    SPI_TRANSFER(0x21/*Display Inversion ON*/);
//    SPI_TRANSFER(0x38/*Idle Mode OFF*/);
//    SPI_TRANSFER(0x39/*Idle Mode ON*/); // Idle mode gives a super-saturated high contrast reduced colors mode

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
  SET_GPIO(GPIO_TFT_BACKLIGHT); // And turn the backlight on.
#endif
}

void TurnBacklightOff()
{
#if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
  SET_GPIO_MODE(GPIO_TFT_BACKLIGHT, 0x01); // Set backlight pin to digital 0/1 output mode (0x01) in case it had been PWM controlled
  CLEAR_GPIO(GPIO_TFT_BACKLIGHT); // And turn the backlight off.
#endif
}

void TurnDisplayOff()
{
  TurnBacklightOff();
#if 0
  QUEUE_SPI_TRANSFER(0x28/*Display OFF*/);
  QUEUE_SPI_TRANSFER(0x10/*Enter Sleep Mode*/);
  usleep(120*1000); // Sleep off can be sent 120msecs after entering sleep mode the earliest, so synchronously sleep here for that duration to be safe.
#endif
//  printf("Turned display OFF\n");
}

void TurnDisplayOn()
{
#if 0
  QUEUE_SPI_TRANSFER(0x11/*Sleep Out*/);
  usleep(120 * 1000);
  QUEUE_SPI_TRANSFER(0x29/*Display ON*/);
#endif
#if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
  SET_GPIO_MODE(GPIO_TFT_BACKLIGHT, 0x01); // Set backlight pin to digital 0/1 output mode (0x01) in case it had been PWM controlled
  SET_GPIO(GPIO_TFT_BACKLIGHT); // And turn the backlight on.
#endif
//  printf("Turned display ON\n");
}

void DeinitSPIDisplay()
{
  ClearScreen();
  SPI_TRANSFER(/*Display OFF*/0x28);
  TurnBacklightOff();
}

#endif
