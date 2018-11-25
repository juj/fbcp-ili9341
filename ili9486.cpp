#include "config.h"

#if defined(ILI9486) || defined(ILI9486L)

#include "spi.h"

#include <memory.h>
#include <stdio.h>

void ChipSelectHigh()
{
  WAIT_SPI_FINISHED();
  CLEAR_GPIO(GPIO_SPI0_CE0);
//    for(int i = 0; i < 10; ++i) __sync_synchronize();
  SET_GPIO(GPIO_SPI0_CE0);
  SET_GPIO(GPIO_SPI0_CE1); // Disable Display
//    for(int i = 0; i < 10; ++i) __sync_synchronize();
  CLEAR_GPIO(GPIO_SPI0_CE1); // Enable Display
}

void InitILI9486()
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

  // For sanity, start with both Chip selects high to ensure that the display will see a high->low enable transition when we start.
  SET_GPIO(GPIO_SPI0_CE0); // Disable Touch
  SET_GPIO(GPIO_SPI0_CE1); // Disable Display
  usleep(1000);

  // Do the initialization with a very low SPI bus speed, so that it will succeed even if the bus speed chosen by the user is too high.
  spi->clk = 34;
  __sync_synchronize();

  BEGIN_SPI_COMMUNICATION();
  {
/*
    CLEAR_GPIO(GPIO_SPI0_CE0); // Enable Touch
    SET_GPIO(GPIO_SPI0_CE1); // Disable Display

    // Original driver sends the command 0xE7 42 times with odd timings and Chip Select combinations, does not seem to be necessary.
    for(int i = 0; i < 21; ++i)
    {
      WAIT_SPI_FINISHED();
      SET_GPIO(GPIO_SPI0_CE0); // Enable Touch
      CLEAR_GPIO(GPIO_SPI0_CE0); // Enable Touch
      SPI_TRANSFER(0x0000E700);
    }
    usleep(50*1000);
    for(int i = 0; i < 21; ++i)
    {
      WAIT_SPI_FINISHED();
      SET_GPIO(GPIO_SPI0_CE0); // Enable Touch
      CLEAR_GPIO(GPIO_SPI0_CE0); // Enable Touch
      SPI_TRANSFER(0x0000E700);
    }
    SPI_TRANSFER(0x00008000);

    END_SPI_COMMUNICATION();
    BEGIN_SPI_COMMUNICATION();

    usleep(10*1000);
*/
    CLEAR_GPIO(GPIO_SPI0_CE0); // Enable Touch
    CLEAR_GPIO(GPIO_SPI0_CE1); // Enable Display

    BEGIN_SPI_COMMUNICATION();

//    SPI_TRANSFER_TO_PREV_CS(0x00000100);
///////////////////////    SPI_TRANSFER(0x00000100);

//    END_SPI_COMMUNICATION();
//    BEGIN_SPI_COMMUNICATION();

    usleep(25*1000);

    SET_GPIO(GPIO_SPI0_CE0); // Disable Touch
/*
    SET_GPIO(GPIO_SPI0_CE0); // Disable Touch
    CLEAR_GPIO(GPIO_SPI0_CE1); // Enable Display

    END_SPI_COMMUNICATION();
    BEGIN_SPI_COMMUNICATION();

    SET_GPIO(GPIO_SPI0_CE1); // Enable Display
    usleep(30);
    CLEAR_GPIO(GPIO_SPI0_CE1); // Enable Display
*/
    usleep(25*1000);

//    SPI_TRANSFER_TO_PREV_CS(0x00000000);
    SPI_TRANSFER(0x00000000); // This command seems to be Reset
    usleep(120*1000);
    /*
    SET_GPIO(GPIO_SPI0_CE1); // Enable Display
    usleep(30);
    CLEAR_GPIO(GPIO_SPI0_CE1); // Enable Display
    */

//    SPI_TRANSFER_TO_PREV_CS(0x00000100);
    SPI_TRANSFER(0x00000100);
    usleep(50*1000);
//    SPI_TRANSFER_TO_PREV_CS(0x00001100);
    SPI_TRANSFER(0x00001100);
    usleep(60*1000);
/*
    SET_GPIO(GPIO_SPI0_CE1); // Disable Display
    usleep(30);
    CLEAR_GPIO(GPIO_SPI0_CE1); // Enable Display
*/
//    END_SPI_COMMUNICATION();
//    BEGIN_SPI_COMMUNICATION();

    SPI_TRANSFER(0xB9001100, 0x00, 0xFF, 0x00, 0x83, 0x00, 0x57);
    usleep(5*1000);
/*
    SET_GPIO(GPIO_SPI0_CE1); // Enable Display
    usleep(30);
    CLEAR_GPIO(GPIO_SPI0_CE1); // Enable Display
*/
    SPI_TRANSFER(0xB6001100, 0x00, 0x2C);
    SPI_TRANSFER(0x11001100);
    usleep(150*1000);
/*
    SET_GPIO(GPIO_SPI0_CE1); // Enable Display
    usleep(30);
    CLEAR_GPIO(GPIO_SPI0_CE1); // Enable Display
*/
    SPI_TRANSFER(0x3A001100, 0x00, 0x55);
    SPI_TRANSFER(0xB0001100, 0x00, 0x68);
    SPI_TRANSFER(0xCC001100, 0x00, 0x09);

    SPI_TRANSFER(0xB3001100, 0x00, 0x43, 0x00, 0x00, 0x00, 0x06, 0x00, 0x06);

    SPI_TRANSFER(0xB1001100, 0x00, 0x00, 0x00, 0x15, 0x00, 0x1C, 0x00, 0x1C, 0x00, 0x83, 0x00, 0x44);

    SPI_TRANSFER(0xC0001100, 0x00, 0x24, 0x00, 0x24, 0x00, 0x01, 0x00, 0x3C, 0x00, 0x1E, 0x00, 0x08);

    SPI_TRANSFER(0xB4001100, 0x00, 0x02, 0x00, 0x40, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x2A, 0x00, 0x0D, 0x00, 0x4F);

    SPI_TRANSFER(0xE0001100, 0x00, 0x02, 0x00, 0x08, 0x00, 0x11, 0x00, 0x23, 0x00, 0x2C, 0x00, 0x40, 0x00, 0x4A, 0x00, 0x52, 0x00, 0x48, 0x00, 0x41, 0x00, 0x3C, 0x00, 0x33, 0x00, 0x2E, 0x00, 0x28, 0x00, 0x27, 0x00, 0x1B, 0x00, 0x02, 0x00, 0x08, 0x00, 0x11, 0x00, 0x23, 0x00, 0x2C, 0x00, 0x40, 0x00, 0x4A, 0x00, 0x52, 0x00, 0x48, 0x00, 0x41, 0x00, 0x3C, 0x00, 0x33, 0x00, 0x2E, 0x00, 0x28, 0x00, 0x27, 0x00, 0x1B, 0x00, 0x00, 0x00, 0x01);

    SPI_TRANSFER(0x36001100, 0x00, 0x3A);

    SPI_TRANSFER(0x29001100);

    usleep(200*1000);
#if 0
#ifdef DISPLAY_SPI_BUS_IS_16BITS_WIDE
    SPI_TRANSFER(0xB0/*Interface Mode Control*/, 0x00, 0x00/*DE polarity=High enable, PCKL polarity=data fetched at rising time, HSYNC polarity=Low level sync clock, VSYNC polarity=Low level sync clock*/);
#else
    SPI_TRANSFER(0xB0/*Interface Mode Control*/, 0x00/*DE polarity=High enable, PCKL polarity=data fetched at rising time, HSYNC polarity=Low level sync clock, VSYNC polarity=Low level sync clock*/);
#endif
    SPI_TRANSFER(0x11/*Sleep OUT*/);
    usleep(120*1000);

#ifdef DISPLAY_COLOR_FORMAT_R6X2G6X2B6X2
    const uint8_t pixelFormat = 0x66; /*DPI(RGB Interface)=18bits/pixel, DBI(CPU Interface)=18bits/pixel*/
#else
    const uint8_t pixelFormat = 0x55; /*DPI(RGB Interface)=16bits/pixel, DBI(CPU Interface)=16bits/pixel*/
#endif

#ifdef DISPLAY_SPI_BUS_IS_16BITS_WIDE
    SPI_TRANSFER(0x3A/*Interface Pixel Format*/, 0x00, pixelFormat);
#else
    SPI_TRANSFER(0x3A/*Interface Pixel Format*/, pixelFormat);
#endif

    // Oddly, WaveShare 3.5" (B) seems to need Display Inversion ON, whereas WaveShare 3.5" (A) seems to need Display Inversion OFF for proper image. See https://github.com/juj/fbcp-ili9341/issues/8
#ifdef DISPLAY_INVERT_COLORS
    SPI_TRANSFER(0x21/*Display Inversion ON*/);
#else
    SPI_TRANSFER(0x20/*Display Inversion OFF*/);
#endif

#ifdef DISPLAY_SPI_BUS_IS_16BITS_WIDE
    SPI_TRANSFER(0xC0/*Power Control 1*/, 0x00, 0x09, 0x00, 0x09);
    SPI_TRANSFER(0xC1/*Power Control 2*/, 0x00, 0x41, 0x00, 0x00);
    SPI_TRANSFER(0xC2/*Power Control 3*/, 0x00, 0x33);
    SPI_TRANSFER(0xC5/*VCOM Control*/, 0x00, 0x00, 0x00, 0x36);
#else
    SPI_TRANSFER(0xC0/*Power Control 1*/, 0x09, 0x09);
    SPI_TRANSFER(0xC1/*Power Control 2*/, 0x41, 0x00);
    SPI_TRANSFER(0xC2/*Power Control 3*/, 0x33);
    SPI_TRANSFER(0xC5/*VCOM Control*/, 0x00, 0x36);
#endif

#define MADCTL_BGR_PIXEL_ORDER (1<<3)
#define MADCTL_ROW_COLUMN_EXCHANGE (1<<5)
#define MADCTL_COLUMN_ADDRESS_ORDER_SWAP (1<<6)
#define MADCTL_ROW_ADDRESS_ORDER_SWAP (1<<7)
#define MADCTL_ROTATE_180_DEGREES (MADCTL_COLUMN_ADDRESS_ORDER_SWAP | MADCTL_ROW_ADDRESS_ORDER_SWAP)

    uint8_t madctl = 0;
#ifndef DISPLAY_SWAP_BGR
    madctl |= MADCTL_BGR_PIXEL_ORDER;
#endif
#if defined(DISPLAY_FLIP_ORIENTATION_IN_HARDWARE)
    madctl |= MADCTL_ROW_COLUMN_EXCHANGE;
#endif
#ifdef DISPLAY_ROTATE_180_DEGREES
    madctl ^= MADCTL_ROTATE_180_DEGREES;
#endif

#ifdef DISPLAY_SPI_BUS_IS_16BITS_WIDE
    SPI_TRANSFER(0x36/*MADCTL: Memory Access Control*/, 0x00, madctl);
    SPI_TRANSFER(0xE0/*Positive Gamma Control*/, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x2C, 0x00, 0x0B, 0x00, 0x0C, 0x00, 0x04, 0x00, 0x4C, 0x00, 0x64, 0x00, 0x36, 0x00, 0x03, 0x00, 0x0E, 0x00, 0x01, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00);
    SPI_TRANSFER(0xE1/*Negative Gamma Control*/, 0x00, 0x0F, 0x00, 0x37, 0x00, 0x37, 0x00, 0x0C, 0x00, 0x0F, 0x00, 0x05, 0x00, 0x50, 0x00, 0x32, 0x00, 0x36, 0x00, 0x04, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x19, 0x00, 0x14, 0x00, 0x0F);
    SPI_TRANSFER(0xB6/*Display Function Control*/, 0, 0, 0, /*ISC=2*/2, 0, /*Display Height h=*/59); // Actual display height = (h+1)*8 so (59+1)*8=480
#else
    SPI_TRANSFER(0x36/*MADCTL: Memory Access Control*/, madctl);
    SPI_TRANSFER(0xE0/*Positive Gamma Control*/, 0x00, 0x2C, 0x2C, 0x0B, 0x0C, 0x04, 0x4C, 0x64, 0x36, 0x03, 0x0E, 0x01, 0x10, 0x01, 0x00);
    SPI_TRANSFER(0xE1/*Negative Gamma Control*/, 0x0F, 0x37, 0x37, 0x0C, 0x0F, 0x05, 0x50, 0x32, 0x36, 0x04, 0x0B, 0x00, 0x19, 0x14, 0x0F);
    SPI_TRANSFER(0xB6/*Display Function Control*/, 0, /*ISC=2*/2, /*Display Height h=*/59); // Actual display height = (h+1)*8 so (59+1)*8=480
#endif
    SPI_TRANSFER(0x11/*Sleep OUT*/);
    usleep(120*1000);
    SPI_TRANSFER(0x29/*Display ON*/);
    SPI_TRANSFER(0x38/*Idle Mode OFF*/);
    SPI_TRANSFER(0x13/*Normal Display Mode ON*/);

#if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
    printf("Setting TFT backlight on at pin %d\n", GPIO_TFT_BACKLIGHT);
    TurnBacklightOn();
#endif
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
