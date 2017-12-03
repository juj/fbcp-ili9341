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
#include <linux/timer.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

#include "../config.h"
#include "../display.h"
#include "../spi.h"
#include "../util.h"

// TODO: Super-dirty temp, factor this into kbuild Makefile.
#include "../spi.cpp"

volatile SPITask *currentTask = 0;
volatile uint8_t *taskNextByte = 0;
volatile uint8_t *taskEndByte = 0;

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
  info->data = (void*)spiTaskMemory;
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
  uint32_t cs = spi->cs;
  if (!taskNextByte)
  {
    if (currentTask) DoneTask((SPITask*)currentTask);
    currentTask = GetTask();
    if (!currentTask)
    {
      spi->cs = (cs & ~BCM2835_SPI0_CS_TA) | BCM2835_SPI0_CS_CLEAR;
      return IRQ_HANDLED;
    }

    if ((cs & BCM2835_SPI0_CS_RXF)) (void)spi->fifo;
    while (!(spi->cs & BCM2835_SPI0_CS_DONE)) ;
    CLEAR_GPIO(GPIO_TFT_DATA_CONTROL);
    spi->fifo = currentTask->cmd;
    if (currentTask->size == 0) // Was this a task without data bytes? If so, nothing more to do here, go to sleep to wait for next IRQ event
    {
      DoneTask((SPITask*)currentTask);
      taskNextByte = 0;
      currentTask = 0;
    }
    else
    {
      taskNextByte = currentTask->data;
      taskEndByte = currentTask->data + currentTask->size;
    }
#if 1 // Testing overhead of not returning after command byte, but synchronously polling it out..
    while (!(spi->cs & BCM2835_SPI0_CS_DONE)) ;
    (void)spi->fifo;
#else
    return IRQ_HANDLED;
#endif
  }
  if (taskNextByte == currentTask->data)
  {
    SET_GPIO(GPIO_TFT_DATA_CONTROL);
    __sync_synchronize();
  }

  int maxBytesToSend = (cs & BCM2835_SPI0_CS_DONE) ? 16 : 12;
  if ((cs & BCM2835_SPI0_CS_RXF)) (void)spi->fifo;
  if ((cs & BCM2835_SPI0_CS_RXR)) for(int i = 0; i < MIN(maxBytesToSend, taskEndByte-taskNextByte); ++i) { spi->fifo = *taskNextByte++; (void)spi->fifo; }
  else for(int i = 0; i < MIN(maxBytesToSend, taskEndByte-taskNextByte); ++i) { spi->fifo = *taskNextByte++; }
  if (taskNextByte >= taskEndByte)
  {
    if ((cs & BMC2835_SPI0_CS_INTR)) spi->cs = (cs & ~BMC2835_SPI0_CS_INTR) | BMC2835_SPI0_CS_INTD;
    taskNextByte = 0;
  }
  else
  {
    if (!(cs & BMC2835_SPI0_CS_INTR)) spi->cs = (cs | BMC2835_SPI0_CS_INTR) & ~BMC2835_SPI0_CS_INTR;
  }
  return IRQ_HANDLED;
}

static int display_initialization_thread(void *unused)
{
  printk(KERN_INFO "BCM2835 SPI Display driver thread started");

  // Initialize display. TODO: Move to be shared with ili9341.cpp.
  QUEUE_SPI_TRANSFER(0xC0/*Power Control 1*/, 0x23/*VRH=4.60V*/); // Set the GVDD level, which is a reference level for the VCOM level and the grayscale voltage level.
  QUEUE_SPI_TRANSFER(0xC1/*Power Control 2*/, 0x10/*AVCC=VCIx2,VGH=VCIx7,VGL=-VCIx4*/); // Sets the factor used in the step-up circuits. To reduce power consumption, set a smaller factor.
  QUEUE_SPI_TRANSFER(0xC5/*VCOM Control 1*/, 0x3e/*VCOMH=4.250V*/, 0x28/*VCOML=-1.500V*/); // Adjusting VCOM 1 and 2 can control display brightness
  QUEUE_SPI_TRANSFER(0xC7/*VCOM Control 2*/, 0x86/*VCOMH=VMH-58,VCOML=VML-58*/);

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
  QUEUE_SPI_TRANSFER(0x36/*MADCTL: Memory Access Control*/, madctl);
  QUEUE_SPI_TRANSFER(0x3A/*COLMOD: Pixel Format Set*/, 0x55/*DPI=16bits/pixel,DBI=16bits/pixel*/);
  QUEUE_SPI_TRANSFER(0xB1/*Frame Rate Control (In Normal Mode/Full Colors)*/, 0x00/*DIVA=fosc*/, 0x18/*RTNA(Frame Rate)=79Hz*/);
  QUEUE_SPI_TRANSFER(0xB6/*Display Function Control*/, 0x08/*PTG=Interval Scan,PT=V63/V0/VCOML/VCOMH*/, 0x82/*REV=1(Normally white),ISC(Scan Cycle)=5 frames*/, 0x27/*LCD Driver Lines=320*/);
  QUEUE_SPI_TRANSFER(0x26/*Gamma Set*/, 0x01/*Gamma curve 1 (G2.2)*/);
  QUEUE_SPI_TRANSFER(0xE0/*Positive Gamma Correction*/, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00);
  QUEUE_SPI_TRANSFER(0xE1/*Negative Gamma Correction*/, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F);
  QUEUE_SPI_TRANSFER(0x11/*Sleep Out*/);

  spi->cs = BCM2835_SPI0_CS_CLEAR | BCM2835_SPI0_CS_TA | BMC2835_SPI0_CS_INTR | BMC2835_SPI0_CS_INTD; // Initialize the Control and Status register to defaults: CS=0 (Chip Select), CPHA=0 (Clock Phase), CPOL=0 (Clock Polarity), CSPOL=0 (Chip Select Polarity), TA=0 (Transfer not active), and reset TX and RX queues.
  msleep(1000);
  QUEUE_SPI_TRANSFER(/*Display ON*/0x29);

  // Initial screen clear
  for(int y = 0; y < DISPLAY_HEIGHT; ++y)
  {
    QUEUE_SPI_TRANSFER(DISPLAY_SET_CURSOR_X, 0, 0, DISPLAY_WIDTH >> 8, DISPLAY_WIDTH & 0xFF);
    QUEUE_SPI_TRANSFER(DISPLAY_SET_CURSOR_Y, y >> 8, y & 0xFF, DISPLAY_HEIGHT >> 8, DISPLAY_HEIGHT & 0xFF);
    SPITask *clearLine = AllocTask(SCANLINE_SIZE);
    clearLine->cmd = DISPLAY_WRITE_PIXELS;
    clearLine->size = SCANLINE_SIZE;
    memset((void*)clearLine->data, 0, SCANLINE_SIZE);
    CommitTask(clearLine);
  }
  QUEUE_SPI_TRANSFER(DISPLAY_SET_CURSOR_X, 0, 0, DISPLAY_WIDTH >> 8, DISPLAY_WIDTH & 0xFF);
  QUEUE_SPI_TRANSFER(DISPLAY_SET_CURSOR_Y, 0, 0, DISPLAY_HEIGHT >> 8, DISPLAY_HEIGHT & 0xFF);

  spi->cs = BCM2835_SPI0_CS_CLEAR | BCM2835_SPI0_CS_TA | BMC2835_SPI0_CS_INTR | BMC2835_SPI0_CS_INTD; // Initialize the Control and Status register to defaults: CS=0 (Chip Select), CPHA=0 (Clock Phase), CPOL=0 (Clock Polarity), CSPOL=0 (Chip Select Polarity), TA=0 (Transfer not active), and reset TX and RX queues.

  // Expose SPI worker ring bus to user space driver application.
  proc_create(SPI_BUS_PROC_ENTRY_FILENAME, 0, NULL, &fops);

  return 0;
}

static struct task_struct *displayThread = 0;
static uint32_t irqHandlerCookie = 0;
static uint32_t irqRegistered = 0;

int bcm2385_spi_display_init(void)
{
  InitSPI();
  int ret = request_irq(84, irq_handler, IRQF_SHARED, "spi_handler", &irqHandlerCookie);
  if (ret != 0) FATAL_ERROR("request_irq failed!");
  irqRegistered = 1;

  displayThread = kthread_create(display_initialization_thread, NULL, "display_thread");
  if (displayThread) wake_up_process(displayThread);
  return 0;
}

void bcm2385_spi_display_exit(void)
{
  spi->cs = BCM2835_SPI0_CS_CLEAR;
  msleep(200);
  DeinitSPI();

  if (irqRegistered)
  {
    free_irq(84, &irqHandlerCookie);
    irqRegistered = 0;
  }

  remove_proc_entry(SPI_BUS_PROC_ENTRY_FILENAME, NULL);
}

module_init(bcm2385_spi_display_init);
module_exit(bcm2385_spi_display_exit);
