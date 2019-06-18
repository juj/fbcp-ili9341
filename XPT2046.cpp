/**
 * @file XPT2046.cpp
 * @date 19.02.2016
 * @author Markus Sattler
 *
 * Copyright (c) 2015 Markus Sattler. All rights reserved.
 * This file is originally part of the XPT2046 driver for Arduino.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "XPT2046.h"

#define XPT2046_CFG_START   1<<7

#define XPT2046_CFG_MUX(v)  ((v&0b111) << (4))

#define XPT2046_CFG_8BIT    1<<3
#define XPT2046_CFG_12BIT   (0)

#define XPT2046_CFG_SER     1<<2
#define XPT2046_CFG_DFR     (0)

#define XPT2046_CFG_PWR(v)  ((v&0b11))

#define XPT2046_MUX_Y       0b101
#define XPT2046_MUX_X       0b001

#define XPT2046_MUX_Z1      0b011
#define XPT2046_MUX_Z2      0b100

#define INTERRUPTDEC 10

XPT2046::XPT2046() {
	spi_cs = 0;
	z_average = 0;
	tcfifo = "/tmp/TCfifo";
	
    _maxValue = 0x0fff;
    _width = 480;
    _height = 320;
    _rotation = 0;

    _minX = 0;
    _minY = 0;

    _maxX = _maxValue;
    _maxY = _maxValue;

    _lastX = -1;
    _lastY = -1;

    _minChange = 10;

    interruptpoll = INTERRUPTDEC;
    
	spi_cs = 
		1 << 0 |     //Chip select 1 
		0 << 3 |    //low idle clock polarity
		0 << 6 |    //chip select active low
		0 << 22 |    //chip select low polarity
		0 << 8 |    //DMA disabled
		0 << 11;  //Manual chip select
	
    // FIFO file path
	// Creating the named file(FIFO) 
	// mkfifo(<pathname>, <permission>) 
	mkfifo(tcfifo, 0666);
}


XPT2046::~XPT2046() {
}

int XPT2046::SpiWriteAndRead(unsigned char *data, int length)
{
	for (int i = 0; i < length; i++)
	{		
		spi->cs = spi_cs | 1 << 7;//Set TA to high
		
		while (!(spi->cs & (1 << 18))) ; //Poll TX Fifo until it has space
		
		spi->fifo = data[i];
		
		while(! (spi->cs & (1 << 17))) ; //Wait until RX FIFO contains data
		
		uint32_t spi_fifo = spi->fifo;
		
		spi->cs = (spi_cs & (~(1 << 7))) | 1 << 5 | 1 << 4;   //Set TA to LOW  //Clear Fifo
		
		data[i] = spi_fifo;		
	}
    
    return 0;
}

void XPT2046::read_touchscreen(bool interruptEnable) {
    uint16_t x, y, z;

	uint32_t old_spi_cs = spi->cs;
	uint32_t old_spi_clk = spi->clk;
	//printBits(sizeof(old_spi_cs), (void*)&(old_spi_cs));
	spi->clk = 256;

    // touch on low
	read(&x, &y, &z);
	if (abs(x - _lastX) > _minChange || abs(y - _lastY) > _minChange) {
		_lastX = x;
		_lastY = y;
	}
    
    if(interruptEnable) {
        // SPI requires 32bit alignment
        uint8_t buf[3] = {
            // re-enable interrupt
            (XPT2046_CFG_START | XPT2046_CFG_12BIT | XPT2046_CFG_DFR | XPT2046_CFG_MUX(XPT2046_MUX_Z2)| XPT2046_CFG_PWR(0)), 0x00, 0x00
        };
        SpiWriteAndRead(buf, 3);
    }
    
	if (z > 100) 
	{
		char output[30] = "";
        	fd = open(tcfifo, O_WRONLY | O_NONBLOCK);
		sprintf(output, "x:%d, y:%d, z:%d\n", x, y, z);
		write(fd, output, strlen(output) + 1);
        	close(fd);
		this->lastTouchTick = tick();
	}
    
	//spi->cs = (old_spi_cs | 1 << 4 | 1 << 5) & (~(1 << 7)); //Clear Fifos and TA
	spi->cs  = old_spi_cs;
	spi->clk = old_spi_clk;
}

void XPT2046::setRotation(uint8_t m) {
    _rotation = m % 4;
}

void XPT2046::setCalibration(uint16_t minX, uint16_t minY, uint16_t maxX, uint16_t maxY) {
    _minX = minX;
    _minY = minY;
    _maxX = maxX;
    _maxY = maxY;
}

void XPT2046::read(uint16_t * oX, uint16_t * oY, uint16_t * oZ) {
    uint16_t x, y;
    readRaw(&x, &y, oZ);
	
    uint32_t cX = x;
    uint32_t cY = y;	

    if(cX < _minX) {
        cX = 0;
    } else  if(cX > _maxX) {
        cX = _width;
    } else {
        cX -= _minX;
        cX = ((cX << 8) / (((_maxX - _minX) << 8) / (_width << 8)) )>> 8;
    }

    if(cY < _minY) {
        cY = 0;
    } else if(cY > _maxY) {
        cY = _height;
    } else {
        cY -= _minY;
        cY = ((cY << 8) / (((_maxY - _minY) << 8) / (_height << 8))) >> 8;
    }

    *oX = cX;
    *oY = cY;
	*oZ = std::max(0,4096 - (int)(*oZ));
}

void XPT2046::readRaw(uint16_t * oX, uint16_t * oY, uint16_t * oZ) {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z1 = 0;
    uint32_t z2 = 0;
    uint8_t i = 0;

    for(; i < 4; i++) { // Sampling
        // SPI requires 32bit alignment
        uint8_t buf[12] = {
                (XPT2046_CFG_START | XPT2046_CFG_12BIT | XPT2046_CFG_DFR | XPT2046_CFG_MUX(XPT2046_MUX_Y) | XPT2046_CFG_PWR(3)), 0x00, 0x00,
                (XPT2046_CFG_START | XPT2046_CFG_12BIT | XPT2046_CFG_DFR | XPT2046_CFG_MUX(XPT2046_MUX_X) | XPT2046_CFG_PWR(3)), 0x00, 0x00,
                (XPT2046_CFG_START | XPT2046_CFG_12BIT | XPT2046_CFG_DFR | XPT2046_CFG_MUX(XPT2046_MUX_Z1)| XPT2046_CFG_PWR(3)), 0x00, 0x00,
                (XPT2046_CFG_START | XPT2046_CFG_12BIT | XPT2046_CFG_DFR | XPT2046_CFG_MUX(XPT2046_MUX_Z2)| XPT2046_CFG_PWR(3)), 0x00, 0x00
        };

		SpiWriteAndRead(buf, 12);

        y += (buf[1] << 8 | buf[2])>>3;
        x += (buf[4] << 8 | buf[5])>>3;
        z1 += (buf[7] << 8 | buf[8])>>3;
        z2 += (buf[10] << 8 | buf[11])>>3;
    }

    if(i == 0) {
        *oX = 0;
        *oY = 0;
        *oZ = 0;
        return;
    }

    x /= i;
    y /= i;
    z1 /= i;
    z2 /= i;

    switch(_rotation) {
        case 0:
        default:
            break;
        case 1:
            x = (_maxValue - x);
            y = (_maxValue - y);
            break;
        case 2:
            y = (_maxValue - y);
            break;
        case 3:
            x = (_maxValue - x);
            break;
    }

    int z = z1 + _maxValue - z2;

    *oX = x;
    *oY = y;
    *oZ = z2;
}
