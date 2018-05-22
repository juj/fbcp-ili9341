#include "config.h"

#ifdef ILI9486

#include "spi.h"

#include <memory.h>
#include <stdio.h>

static void ILI9486ClearScreen()
{
  // Since we are doing delta updates to only changed pixels, clear display initially to black for known starting state
  for(int y = 0; y < DISPLAY_HEIGHT; ++y)
  {
    SPI_TRANSFER(DISPLAY_SET_CURSOR_X, 0, 0, 0, 0, 0, DISPLAY_WIDTH >> 8, 0, DISPLAY_WIDTH & 0xFF);
    SPI_TRANSFER(DISPLAY_SET_CURSOR_Y, 0, y >> 8, 0, y & 0xFF, 0, DISPLAY_HEIGHT >> 8, 0, DISPLAY_HEIGHT & 0xFF);
    SPITask *clearLine = AllocTask(DISPLAY_WIDTH*2);
    clearLine->cmd = DISPLAY_WRITE_PIXELS;
    memset(clearLine->data, 0, clearLine->size);
    CommitTask(clearLine);
    RunSPITask(clearLine);
    DoneTask(clearLine);
  }
  SPI_TRANSFER(DISPLAY_SET_CURSOR_X, 0, 0, 0, 0, 0, DISPLAY_WIDTH >> 8, 0, DISPLAY_WIDTH & 0xFF);
  SPI_TRANSFER(DISPLAY_SET_CURSOR_Y, 0, 0, 0, 0, 0, DISPLAY_HEIGHT >> 8, 0, DISPLAY_HEIGHT & 0xFF);
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

  // Do the initialization with a very low SPI bus speed, so that it will succeed even if the bus speed chosen by the user is too high.
  spi->clk = 34;
  __sync_synchronize();

  BEGIN_SPI_COMMUNICATION();
  {
    SPI_TRANSFER(0xb0, 0x00, 0x00);
    SPI_TRANSFER(0x11);
    usleep(120*1000);
    SPI_TRANSFER(0x3A, 0x00, 0x55);
    SPI_TRANSFER(0x36, 0x00, 0x28);
    SPI_TRANSFER(0x21);
    SPI_TRANSFER(0xC0, 0x00, 0x9, 0x00, 0x9);
    SPI_TRANSFER(0xc1, 0x00, 0x41, 0x00, 0x00);
    SPI_TRANSFER(0xc5, 0x00, 0x00, 0x00, 0x36);
    SPI_TRANSFER(0xe0, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x2c, 0x00, 0x0b, 0x00, 0x0c, 0x00, 0x04, 0x00, 0x4c, 0x00, 0x64, 0x00, 0x36, 0x00, 0x03, 0x00, 0x0e, 0x00, 0x01, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00);
    SPI_TRANSFER(0xe1, 0x00, 0x0f, 0x00, 0x37, 0x00, 0x37, 0x00, 0x0c, 0x00, 0x0f, 0x00, 0x05, 0x00, 0x50, 0x00, 0x32, 0x00, 0x36, 0x00, 0x04, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x19, 0x00, 0x14, 0x00, 0x0f);
    SPI_TRANSFER(0x11);
    usleep(120*1000);
    SPI_TRANSFER(0x29);
    SPI_TRANSFER(0x36, 0x00, 0x28);

    ILI9486ClearScreen();
  }
#ifndef USE_DMA_TRANSFERS // For DMA transfers, keep SPI CS & TA active.
  END_SPI_COMMUNICATION();
#endif

  // And speed up to the desired operation speed finally after init is done.
  spi->clk = SPI_BUS_CLOCK_DIVISOR;
}

#endif
