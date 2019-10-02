/**
 * @file XPT2046.cpp
 * @date 19.02.2016
 * @author Markus Sattler
 *
 * Copyright (c) 2015 Markus Sattler. All rights reserved.
 * This file is originally part of the XPT2046 driver for Arduino.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the “Software”), to deal in the Software without restriction,
 * including without limitation the rights to 
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in 
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "XPT2046.h"

#include <sys/types.h>
#include <stdio.h>

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
#define MAXLINE 128

XPT2046::XPT2046() {
	spi_cs = 0;
	z_average = 0;
	tcfifo = "/tmp/TCfifo";
    calibFile = "/etc/xpt2046.conf";
	
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
    initCalibration();
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
        uint16_t words16Write[4] = { x, y, z, 0x0000FFFF};
        fd = open(tcfifo, O_WRONLY | O_NONBLOCK);
        
		write(fd, words16Write, 4 * sizeof(uint16_t));
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

void XPT2046::initCalibration() {
    FILE *stream;
    char *line = NULL;
    int reti;
    size_t len = 0;
    ssize_t nread;

    stream = fopen(calibFile, "r");
    if(stream != NULL)
    {
        while ((nread = getline(&line, &len, stream)) != -1) {
            //fprintf(stdout,"scanline: '%s'", line);
            reti = sscanf((const char*)line,"%d,%d,%d,%d,%d,%d,%d",
                   &calib.An, &calib.Bn, &calib.Cn, &calib.Dn, &calib.En, &calib.Fn,&calib.Divider);
        }
        free(line);
        fclose(stream);
    } else { // Make standard identity calibration -- no translation
        // Target on-screen points
        POINT pointTargets[3] = {
            {15, 15},
            {-15, 15},
            {15, -15},
        };        
        // Set curser values initially
        // Update for screen dimensions
        for(int i = 0; i<3;i++ ) {
            pointTargets[i].x = (pointTargets[i].x < 0 ? _width + pointTargets[i].x : pointTargets[i].x);
            pointTargets[i].y = (pointTargets[i].y < 0 ? _height + pointTargets[i].y : pointTargets[i].y);
            fprintf(stdout, "%d,%d\n", pointTargets[i].x, pointTargets[i].y);
        }
        setCalibrationMatrix( (POINT*)pointTargets, (POINT*)pointTargets, &calib); // initialized 1:1 matrix
    }
    fprintf(stdout,"M: %d,%d,%d,%d,%d,%d,%d\r\n"
            ,calib.An,calib.Bn,calib.Cn,calib.Dn,calib.En,calib.Fn,calib.Divider );
}


void XPT2046::read(uint16_t * oX, uint16_t * oY, uint16_t * oZ) {
    uint16_t x, y;
    POINT pointCorrected, pointRaw;
    
    readRaw(&x, &y, oZ);

    pointRaw.x = x;
    pointRaw.y = y;

    if(pointRaw.x < _minX) {
        pointRaw.x = 0;
    } else  if(pointRaw.x > _maxX) {
        pointRaw.x = _width;
    } else {
        pointRaw.x -= _minX;
        pointRaw.x = ((pointRaw.x << 8) / (((_maxX - _minX) << 8) / (_width << 8)) )>> 8;
    }
    
    if(pointRaw.y < _minY) {
        pointRaw.y = 0;
    } else if(pointRaw.y > _maxY) {
        pointRaw.y = _height;
    } else {
        pointRaw.y -= _minY;
        pointRaw.y = ((pointRaw.y << 8) / (((_maxY - _minY) << 8) / (_height << 8))) >> 8;
    }

    getDisplayPoint((POINT *)&pointCorrected,(POINT *)&pointRaw,&calib);

    *oX = pointCorrected.x;
    *oY = pointCorrected.y;
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
