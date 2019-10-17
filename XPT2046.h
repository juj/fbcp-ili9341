/**
 * @file XPT2046.h
 * @date 19.02.2016
 * @author Markus Sattler
 *
 * Copyright (c) 2015 Markus Sattler. All rights reserved.
 * This file is part of the XPT2046 driver for Arduino.
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

#ifndef XPT2046_H_
#define XPT2046_H_

#include <stdint.h>
#include <fcntl.h>				//Needed for SPI port
#include <sys/ioctl.h>			//Needed for SPI port
#include <unistd.h>			//Needed for SPI port
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>

#include "spi_user.h"
#include "calibrate.h"


class XPT2046 {
    public:

        XPT2046();
	
        ~XPT2046();

	int SpiWriteAndRead(unsigned char *data, int length);
	
        void read_touchscreen(bool interruptEnable);

        void setRotation(uint8_t m);
        void initCalibration();

        void read(uint16_t * oX, uint16_t * oY, uint16_t * oZ);
        void readRaw(uint16_t * oX, uint16_t * oY, uint16_t * oZ);

	uint64_t ticksSinceLastTouch() {
        	uint64_t now = tick();
		return now - this->lastTouchTick;
        }

	static void printBits(size_t const size, void const * const ptr)
	{
	    	unsigned char *b = (unsigned char*) ptr;
	    	unsigned char byte;
	    	int i, j;
	    
	    	for (i = size - 1; i >= 0; i--)
	    	{
	    		for (j = 7; j >= 0; j--)
	    		{
	    			byte = (b[i] >> j) & 1;
	    			printf("%u", byte);
	    		}				
	    		printf(" ");
	    	}
	    	puts("");
	}
	
    protected:

        uint16_t _width;
        uint16_t _height;

        uint16_t _rotation;

        uint16_t _minX;
        uint16_t _minY;

        uint16_t _maxX;
        uint16_t _maxY;

        uint16_t _maxValue;

        int _lastX;
        int _lastY;
        int fd;
        uint64_t lastTouchTick;
 
        uint16_t _minChange;
			
        uint32_t spi_cs ;
	
	uint32_t z_average ;
    
    MATRIX calib;
    
	const char * tcfifo ;
    const char *calibFile ;
        int interruptpoll ;
};



#endif /* XPT2046_H_ */
