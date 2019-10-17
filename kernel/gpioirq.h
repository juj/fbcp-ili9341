#ifndef __GPIO_IRQ_H__
#define __GPIO_IRQ_H__

#include <linux/types.h>
#include <linux/ioctl.h>

#define GPIOIRQ_MAGIC	('x')

#define GPIOIRQ_FALLING_EDGE	(0x08)
#define GPIOIRQ_RISING_EDGE 	(0x04)
#define GPIOIRQ_HIGH			(0x02)
#define GPIOIRQ_LOW				(0x01)
#define GPIOIRQ_NONE			(0x00)

#define GPIOIRQ_PULLUP			2
#define GPIOIRQ_PULLDOWN		1
#define GPIOIRQ_NOPULL			0

#define GPIOIRQ_IOC_SETTYPE		_IOW(GPIOIRQ_MAGIC, 1, __u8)
#define GPIOIRQ_IOC_SETPULL		_IOW(GPIOIRQ_MAGIC, 2, __u8)


#endif
