#include <linux/buffer_head.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/futex.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/spi/spidev.h>
#include <linux/time.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

//#include "../config.h"
#include "../spi.h"

#define SPI_BUS_PROC_ENTRY_FILENAME "bcm2835_spi_display_bus"

typedef struct mmap_info
{
  char *data;
} mmap_info;

static void p_vm_open(struct vm_area_struct *vma)
{
}

static void p_vm_close(struct vm_area_struct *vma)
{
}

static int p_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
  mmap_info *info = (mmap_info *)vma->vm_private_data;
  if (info->data)
  {
    struct page *page = virt_to_page(info->data + vmf->pgoff*PAGE_SIZE);
    get_page(page);
    vmf->page = page;
  }
  return 0;
}

static struct vm_operations_struct vm_ops =
{
  .open = p_vm_open,
  .close = p_vm_close,
  .fault = p_vm_fault,
};

static int p_mmap(struct file *filp, struct vm_area_struct *vma)
{
  vma->vm_ops = &vm_ops;
  vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
  vma->vm_private_data = filp->private_data;
  p_vm_open(vma);
  return 0;
}

static int p_open(struct inode *inode, struct file *filp)
{
  mmap_info *info = kmalloc(sizeof(mmap_info), GFP_KERNEL);
  info->data = (void*)spiFlagMemory;
  filp->private_data = info;
  return 0;
}

static int p_release(struct inode *inode, struct file *filp)
{
  mmap_info *info;
  info = filp->private_data;
  kfree(info);
  filp->private_data = NULL;
  return 0;
}

static const struct file_operations fops =
{
  .mmap = p_mmap,
  .open = p_open,
  .release = p_release,
};

static irqreturn_t irq_handler(int irq, void* dev_id)
{
  return IRQ_HANDLED;
}

#define req(cnd) if (!(cnd)) { LOG("!!!%s!!!\n", #cnd);}

uint32_t virt_to_bus_address(volatile void *virtAddress)
{
  return (uint32_t)virt_to_phys((void*)virtAddress) | 0x40000000U;
}

volatile int shuttingDown = 0;
dma_addr_t spiFlagMemoryPhysical = 0;

static uint32_t irqHandlerCookie = 0;
static uint32_t irqRegistered = 0;

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

  remove_proc_entry(SPI_BUS_PROC_ENTRY_FILENAME, NULL);
}

module_init(bcm2835_spi_display_init);
module_exit(bcm2835_spi_display_exit);
