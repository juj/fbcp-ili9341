//#include <linux/buffer_head.h>
//#include <linux/debugfs.h>
#include <linux/delay.h>
//#include <linux/fb.h>
#include <linux/fs.h>
//#include <linux/futex.h>
//#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
//#include <linux/kthread.h>
//#include <linux/math64.h>
//#include <linux/mm.h>
#include <linux/module.h>
//#include <linux/proc_fs.h>
#include <linux/slab.h>
//#include <linux/spi/spidev.h>
//#include <linux/time.h>
//#include <asm/io.h>
//#include <asm/segment.h>
#include <asm/uaccess.h>

#include "../spi.h"

#define SPI_BUS_PROC_ENTRY_FILENAME "bcm2835_spi_display_bus"
#define req(cnd) if (!(cnd)) { LOG("!!!%s!!!\n", #cnd);}

volatile int shuttingDown = 0;
static uint32_t irqHandlerCookie = 0;
static uint32_t irqRegistered = 0;

static irqreturn_t irq_handler(int irq, void* dev_id)
{
  if(shuttingDown) return IRQ_HANDLED;
    
  return IRQ_HANDLED;
}

int bcm2835_spi_display_init(void)
{
  int ret = request_irq(84, irq_handler, IRQF_SHARED, "spi_handler", &irqHandlerCookie);
  if (ret != 0) printk("request_irq failed!");
  irqRegistered = 1;

  return 0;
}

void bcm2835_spi_display_exit(void)
{
  shuttingDown = 1;
  msleep(2000);

  if (irqRegistered)
  {
    free_irq(84, &irqHandlerCookie);
    irqRegistered = 0;
  }
}

module_init(bcm2835_spi_display_init);
module_exit(bcm2835_spi_display_exit);
