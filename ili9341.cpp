#include "config.h"
#include "spi.h"

#include <memory.h>
#include <stdio.h>

static void ILI9341ClearScreen()
{
  // Since we are doing delta updates to only changed pixels, clear display initially to black for known starting state
  for(int y = 0; y < DISPLAY_HEIGHT; ++y)
  {
    SPI_TRANSFER(DISPLAY_SET_CURSOR_X, 0, 0, DISPLAY_WIDTH >> 8, DISPLAY_WIDTH & 0xFF);
    SPI_TRANSFER(DISPLAY_SET_CURSOR_Y, y >> 8, y & 0xFF, DISPLAY_HEIGHT >> 8, DISPLAY_HEIGHT & 0xFF);
    SPITask *clearLine = AllocTask(DISPLAY_WIDTH*2);
    clearLine->cmd = DISPLAY_WRITE_PIXELS;
    memset(clearLine->data, 0, clearLine->size);
    CommitTask(clearLine);
    RunSPITask(clearLine);
    DoneTask(clearLine);
  }
  SPI_TRANSFER(DISPLAY_SET_CURSOR_X, 0, 0, DISPLAY_WIDTH >> 8, DISPLAY_WIDTH & 0xFF);
  SPI_TRANSFER(DISPLAY_SET_CURSOR_Y, 0, 0, DISPLAY_HEIGHT >> 8, DISPLAY_HEIGHT & 0xFF);
}

#ifdef ADAFRUIT_ILI9341_PITFT
void InitAdafruitILI9341PiTFT()
{
  // Initialize display
  BEGIN_SPI_COMMUNICATION();
  {
    SPI_TRANSFER(0xC0/*Power Control 1*/, 0x23/*VRH=4.60V*/); // Set the GVDD level, which is a reference level for the VCOM level and the grayscale voltage level.
    SPI_TRANSFER(0xC1/*Power Control 2*/, 0x10/*AVCC=VCIx2,VGH=VCIx7,VGL=-VCIx4*/); // Sets the factor used in the step-up circuits. To reduce power consumption, set a smaller factor.
    SPI_TRANSFER(0xC5/*VCOM Control 1*/, 0x3e/*VCOMH=4.250V*/, 0x28/*VCOML=-1.500V*/); // Adjusting VCOM 1 and 2 can control display brightness
    SPI_TRANSFER(0xC7/*VCOM Control 2*/, 0x86/*VCOMH=VMH-58,VCOML=VML-58*/);

#define MADCTL_ROW_COLUMN_EXCHANGE (1<<5)
#define MADCTL_BGR_PIXEL_ORDER (1<<3)
#define MADCTL_ROTATE_180_DEGREES 0xC0
    uint8_t madctl = MADCTL_BGR_PIXEL_ORDER;
#ifdef DISPLAY_ROTATE_180_DEGREES
    madctl |= MADCTL_ROTATE_180_DEGREES;
#endif
#ifdef DISPLAY_OUTPUT_LANDSCAPE
    madctl |= MADCTL_ROW_COLUMN_EXCHANGE;
#endif
    SPI_TRANSFER(0x36/*MADCTL: Memory Access Control*/, madctl);

    SPI_TRANSFER(0x3A/*COLMOD: Pixel Format Set*/, 0x55/*DPI=16bits/pixel,DBI=16bits/pixel*/);
    SPI_TRANSFER(0xB1/*Frame Rate Control (In Normal Mode/Full Colors)*/, 0x00/*DIVA=fosc*/, 0x18/*RTNA(Frame Rate)=79Hz*/);
    SPI_TRANSFER(0xB6/*Display Function Control*/, 0x08/*PTG=Interval Scan,PT=V63/V0/VCOML/VCOMH*/, 0x82/*REV=1(Normally white),ISC(Scan Cycle)=5 frames*/, 0x27/*LCD Driver Lines=320*/);
    SPI_TRANSFER(0x26/*Gamma Set*/, 0x01/*Gamma curve 1 (G2.2)*/);
    SPI_TRANSFER(0xE0/*Positive Gamma Correction*/, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00);
    SPI_TRANSFER(0xE1/*Negative Gamma Correction*/, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F);
    SPI_TRANSFER(0x11/*Sleep Out*/);
    usleep(120 * 1000);
    SPI_TRANSFER(/*Display ON*/0x29);

    // Some wonky effects to try out:
//    SPI_TRANSFER(0x20/*Display Inversion OFF*/);
//    SPI_TRANSFER(0x21/*Display Inversion ON*/);
//    SPI_TRANSFER(0x38/*Idle Mode OFF*/);
//    SPI_TRANSFER(0x39/*Idle Mode ON*/); // Idle mode gives a super-saturated high contrast reduced colors mode

    ILI9341ClearScreen();
  }

  END_SPI_COMMUNICATION();
}
#endif

// Also ILI9341 like Adafruit's PiTFT, but for some reason, has a different sequence of initialization commands, even though the ILI9341 spec should
// already define what the initialization commands are(?)
#ifdef FREEPLAYTECH_WAVESHARE32B
void InitWaveshare32BILI9341()
{
  // Run reset sequence to enable the device.
  SET_GPIO_MODE(GPIO_TFT_RESET_PIN, 1);
  CLEAR_GPIO(GPIO_TFT_RESET_PIN);
  usleep(120 * 1000);
  SET_GPIO(GPIO_TFT_RESET_PIN);
  usleep(120 * 1000);

  BEGIN_SPI_COMMUNICATION();
  {
    // NOTE: If you find out a spec describing these initialization commands, please send a message!
    SPI_TRANSFER(0xCB,0x39,0x2C,0x00,0x34,0x02);
    SPI_TRANSFER(0xCF,0x00,0xC1,0x30);
    SPI_TRANSFER(0xE8,0x85,0x00,0x78);
    SPI_TRANSFER(0xEA,0x00,0x00);
    SPI_TRANSFER(0xED,0x64,0x03,0x12,0x81);
    SPI_TRANSFER(0xF7,0x20);
    SPI_TRANSFER(0xC0/*Power Control 1*/, 0x23/*VRH=4.60V*/); // Set the GVDD level, which is a reference level for the VCOM level and the grayscale voltage level.
    SPI_TRANSFER(0xC1/*Power Control 2*/, 0x10/*AVCC=VCIx2,VGH=VCIx7,VGL=-VCIx4*/); // Sets the factor used in the step-up circuits. To reduce power consumption, set a smaller factor.
    SPI_TRANSFER(0xC5/*VCOM Control 1*/, 0x3e/*VCOMH=4.250V*/, 0x28/*VCOML=-1.500V*/); // Adjusting VCOM 1 and 2 can control display brightness
    SPI_TRANSFER(0xC7/*VCOM Control 2*/, 0x86/*VCOMH=VMH-58,VCOML=VML-58*/);
    SPI_TRANSFER(0x36/*MADCTL: Memory Access Control*/, 0x28);
    SPI_TRANSFER(0x3A/*COLMOD: Pixel Format Set*/, 0x55/*DPI=16bits/pixel,DBI=16bits/pixel*/);
    SPI_TRANSFER(0xB1/*Frame Rate Control (In Normal Mode/Full Colors)*/, 0x00/*DIVA=fosc*/, 0x18/*RTNA(Frame Rate)=79Hz*/);
    SPI_TRANSFER(0xB6/*Display Function Control*/, 0x08/*PTG=Interval Scan,PT=V63/V0/VCOML/VCOMH*/, 0x82/*REV=1(Normally white),ISC(Scan Cycle)=5 frames*/, 0x27/*LCD Driver Lines=320*/);
    SPI_TRANSFER(0xF2,0x00);
    SPI_TRANSFER(0x26/*Gamma Set*/, 0x01/*Gamma curve 1 (G2.2)*/);
    SPI_TRANSFER(0xE0/*Positive Gamma Correction*/, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00);
    SPI_TRANSFER(0xE1/*Negative Gamma Correction*/, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F);
    SPI_TRANSFER(0x11/*Sleep Out*/);
    usleep(120 * 1000);
    SPI_TRANSFER(/*Display ON*/0x29);

    ILI9341ClearScreen();
  }
  END_SPI_COMMUNICATION();
}
#endif
