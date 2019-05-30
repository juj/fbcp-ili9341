/**
 * @file XPT2046.h
 * @date 19.02.2016
 * @author Markus Sattler
 *
 * Copyright (c) 2015 Markus Sattler. All rights reserved.
 * This file is part of the XPT2046 driver for Arduino.
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

class XPT2046 {
    public:

        XPT2046();
	
        ~XPT2046();

	    int SpiWriteAndRead(unsigned char *data, int length);
	
        void read_touchscreen();
        bool armInterrupt();

        void setRotation(uint8_t m);
        void setCalibration(uint16_t minX, uint16_t minY, uint16_t maxX, uint16_t maxY);

        void read(uint16_t * oX, uint16_t * oY, uint16_t * oZ);
        void readRaw(uint16_t * oX, uint16_t * oY, uint16_t * oZ);
	
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
    
        uint16_t _minChange;
			
		uint32_t spi_cs ;
	
	    uint32_t z_average ;

		const char * tcfifo ;
        bool interruptEnabled ;
};



#endif /* XPT2046_H_ */
