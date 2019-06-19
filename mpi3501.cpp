#include "config.h"

#ifdef MPI3501

#include "spi_user.h"
#include "XPT2046.h"

#include <memory.h>
#include <stdio.h>

#include <fcntl.h>

XPT2046 touch;
static int counter = 0;
char buffer[20];
short bufLen = 0;
#define LOOP_INTERVAL 500
static int loop = 0;

bool activeTouchscreen() {
   return (touch.ticksSinceLastTouch() < TURN_DISPLAY_OFF_AFTER_USECS_OF_INACTIVITY); 
}

void ChipSelectHigh()
{
  WAIT_SPI_FINISHED();
  SET_GPIO(GPIO_SPI0_CE1); // Disable Display
  CLEAR_GPIO(GPIO_SPI0_CE0); // Enable Touch
  __sync_synchronize();
    if(loop++ % LOOP_INTERVAL == 0) {
        touch.read_touchscreen(true);
        __sync_synchronize();
    }
  if(hasInterrupt()) {
	touch.read_touchscreen(false);
      __sync_synchronize();
  }
  SET_GPIO(GPIO_SPI0_CE0); // Disable Touch
  CLEAR_GPIO(GPIO_SPI0_CE1); // Enable Display
  __sync_synchronize();
}

void InitKeDeiV63()
{
  // output device
  touch = XPT2046();
 
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
    CLEAR_GPIO(GPIO_SPI0_CE0); // Enable Touch
    CLEAR_GPIO(GPIO_SPI0_CE1); // Enable Display

    BEGIN_SPI_COMMUNICATION();

    usleep(25*1000);

    SET_GPIO(GPIO_SPI0_CE0); // Disable Touch
    usleep(25*1000);

    SPI_TRANSFER(DISPLAY_NO_OPERATION); // Reset
    usleep(10*1000);
    SPI_TRANSFER(0xff001100);
    SPI_TRANSFER(0xff001100);
    usleep(10*1000);
    SPI_TRANSFER(0xff001100);
    SPI_TRANSFER(0xff001100);
    SPI_TRANSFER(0xff001100);
    SPI_TRANSFER(0xff001100);
    usleep(15*1000);
    SPI_TRANSFER(0x11001100/*Sleep Out*/);
    usleep(150*1000);

    SPI_TRANSFER(0xB0001100, 0x00, 0x00); // CLK 30Hz?
    SPI_TRANSFER(0xB3001100, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00); // set RGB?
    SPI_TRANSFER(0xB9001100, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x0f); // ext cmd?
    SPI_TRANSFER(0xC0001100, 0x00, 0x13, 0x00, 0x3B, 0x00, 0x00, 0x00, 0x02
	, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x43);                    // Timing?
    SPI_TRANSFER(0xC1001100, 0x00, 0x08, 0x00, 0x0F, 0x00, 0x08, 0x00, 0x08); // gamma?
    SPI_TRANSFER(0xC4001100, 0x00, 0x11, 0x00, 0x07, 0x00, 0x03, 0x00, 0x04); // sleep modes?
    SPI_TRANSFER(0xC6001100, 0x00, 0x00); // ?
    SPI_TRANSFER(0xC8001100, 0x00, 0x03, 0x00, 0x03, 0x00, 0x13, 0x00, 0x5C
	, 0x00, 0x03, 0x00, 0x07, 0x00, 0x14, 0x00, 0x08
	, 0x00, 0x00, 0x00, 0x21, 0x00, 0x08, 0x00, 0x14
	, 0x00, 0x07, 0x00, 0x53, 0x00, 0x0C, 0x00, 0x13
	, 0x00, 0x03, 0x00, 0x03, 0x00, 0x21, 0x00, 0x00); // ?
    SPI_TRANSFER(0x35001100, 0x00, 0x00);

#define MADCTL_BGR_PIXEL_ORDER (1<<3)
#define MADCTL_LINE_ADDRESS_ORDER_SWAP (1<<4)
#define MADCTL_ROW_COLUMN_EXCHANGE (1<<5)
#define MADCTL_COLUMN_ADDRESS_ORDER_SWAP (1<<6)
#define MADCTL_ROW_ADDRESS_ORDER_SWAP (1<<7)
#define MADCTL_ROTATE_180_DEGREES ( MADCTL_ROW_ADDRESS_ORDER_SWAP )

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
    SPI_TRANSFER(0x36001100/*MADCTL: Memory Access Control*/, 0x00, madctl);

    SPI_TRANSFER(0x3A001100/*Interface Pixel Format*/, 0x00, 0x55);
    SPI_TRANSFER(0x44001100, 0x00, 0x00, 0x00, 0x01);
    SPI_TRANSFER(0xD0001100, 0x00, 0x07, 0x00, 0x07, 0x00, 0x1D, 0x00, 0x03); // ? 
    SPI_TRANSFER(0xD1001100, 0x00, 0x03, 0x00, 0x30, 0x00, 0x10); // ? 
    SPI_TRANSFER(0xD2001100, 0x00, 0x03, 0x00, 0x14, 0x00, 0x04); // ? 
    SPI_TRANSFER(0x29001100/*Display ON*/);

    usleep(30*1000);

    SPI_TRANSFER(0xB4001100, 0x00, 0x00); // ?
    
    usleep(10*1000);
    
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
  SPI_TRANSFER(DISPLAY_BACKLIT_BRIT,0xFF);
  SPI_TRANSFER(DISPLAY_BACKLIT_CTRL,0x00); // 5-Backlight contro, 3-Display dimming, 2-BacklihtOff
}

void TurnBacklightOn()
{
  SPI_TRANSFER(DISPLAY_BACKLIT_BRIT,0x00);
  SPI_TRANSFER(DISPLAY_BACKLIT_CTRL,0x2C); // 5-Backlight contro, 3-Display dimming, 2-BacklihtON
}

void TurnDisplayOff()
{
  SPI_TRANSFER(DISPLAY_OFF); 
}

void TurnDisplayOn()
{
  SPI_TRANSFER(DISPLAY_ON); 
}

void DeinitSPIDisplay()
{
  ClearScreen();
  TurnDisplayOff();
}


#endif
