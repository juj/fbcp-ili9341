#include "config.h"

#if defined(ILI9488)

#include "spi.h"

#include <memory.h>
#include <stdio.h>

void InitILI9488()
{
  // If a Reset pin is defined, toggle it briefly high->low->high to enable the device. Some devices do not have a reset pin, in which case compile with GPIO_TFT_RESET_PIN left undefined.
#if defined(GPIO_TFT_RESET_PIN) && GPIO_TFT_RESET_PIN >= 0
  printf("Resetting ili9488 display at reset GPIO pin %d\n", GPIO_TFT_RESET_PIN);
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
      //0xE0 - PGAMCTRL Positive Gamma Control
      SPI_TRANSFER(0xE0, 0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F);
      //0xE1 - NGAMCTRL Negative Gamma Control
      SPI_TRANSFER(0xE1, 0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F);
      // 0xC0 Power Control 1
      SPI_TRANSFER(0xC0, 0x17, 0x15);
      // 0xC1 Power Control 2
      SPI_TRANSFER(0xC1, 0x41);
      // 0xC5 VCOM Control
      SPI_TRANSFER(0xC5, 0x00, 0x12, 0x80);
      // 0x36 Memory Access Control - sets display rotation.
      SPI_TRANSFER(0x36, 0xE8);// 0x88); //0x28);//0x48);
      // 0x3A Interface Pixel Format (bit depth color space)
      SPI_TRANSFER(0x3A, 0x66);
      // 0xB0 Interface Mode Control
      SPI_TRANSFER(0xB0, 0x80);
      // 0xB1 Frame Rate Control (in Normal Mode/Full Colors)
      SPI_TRANSFER(0xB1, 0xA0);
      // 0xB4 Display Inversion Control.
      SPI_TRANSFER(0xB4, 0x02);
      // 0xB6 Display Function Control.
      SPI_TRANSFER(0xB6, 0x02, 0x02);
      // 0xE9 Set Image Function.
      SPI_TRANSFER(0xE9, 0x00);
      // 0xF7 Adjuist Control 3
      SPI_TRANSFER(0xF7, 0xA9, 0x51, 0x2C, 0x82);
      // 0x11 Exit Sleep Mode. (Sleep OUT)
      SPI_TRANSFER(0x11);
      usleep(120*1000);
      // 0x29 Display ON.
      SPI_TRANSFER(0x29);
      // 0x38 Idle Mode OFF.
      SPI_TRANSFER(0x38);
      // 0x13 Normal Display Mode ON.
      SPI_TRANSFER(0x13);

#if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
    printf("Setting TFT backlight on at pin %d\n", GPIO_TFT_BACKLIGHT);
    TurnBacklightOn();
#endif

    ClearScreen();
  }
#ifndef USE_DMA_TRANSFERS // For DMA transfers, keep SPI CS & TA active.
  END_SPI_COMMUNICATION();
#endif

  // And speed up to the desired operation speed finally after init is done.
  usleep(10 * 1000); // Delay a bit before restoring CLK, or otherwise this has been observed to cause the display not init if done back to back after the clear operation above.
  spi->clk = SPI_BUS_CLOCK_DIVISOR;
}

void TurnBacklightOff()
{
#if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
  SET_GPIO_MODE(GPIO_TFT_BACKLIGHT, 0x01); // Set backlight pin to digital 0/1 output mode (0x01) in case it had been PWM controlled
  CLEAR_GPIO(GPIO_TFT_BACKLIGHT); // And turn the backlight off.
#endif
}

void TurnBacklightOn()
{
#if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
  SET_GPIO_MODE(GPIO_TFT_BACKLIGHT, 0x01); // Set backlight pin to digital 0/1 output mode (0x01) in case it had been PWM controlled
  SET_GPIO(GPIO_TFT_BACKLIGHT); // And turn the backlight on.
#endif
}

void TurnDisplayOff()
{
  TurnBacklightOff();
  QUEUE_SPI_TRANSFER(0x28/*Display OFF*/);
  QUEUE_SPI_TRANSFER(0x10/*Enter Sleep Mode*/);
  usleep(120*1000); // Sleep off can be sent 120msecs after entering sleep mode the earliest, so synchronously sleep here for that duration to be safe.
}

void TurnDisplayOn()
{
  TurnBacklightOff();
  QUEUE_SPI_TRANSFER(0x11/*Sleep Out*/);
  usleep(120 * 1000);
  QUEUE_SPI_TRANSFER(0x29/*Display ON*/);
  usleep(120 * 1000);
  TurnBacklightOn();
}

void DeinitSPIDisplay()
{
  ClearScreen();
  TurnDisplayOff();
}

#endif
