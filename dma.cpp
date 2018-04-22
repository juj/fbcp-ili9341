#ifndef KERNEL_MODULE
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <inttypes.h>
#include <syslog.h>
#endif

#include "config.h"
#include "dma.h"
#include "spi.h"
#include "util.h"

#ifdef USE_DMA_TRANSFERS

#define BCM2835_PERI_BASE               0x3F000000

SharedMemory *dmaSourceMemory = 0;
volatile DMAChannelRegisterFile *dma = 0;
volatile DMAChannelRegisterFile *dmaTx = 0;
volatile DMAChannelRegisterFile *dmaRx = 0;
int dmaTxChannel = 0;
int dmaTxIrq = 0;
int dmaRxChannel = 0;
int dmaRxIrq = 0;

static int AllocateDMAChannel(int *dmaChannel, int *irq)
{
  // TODO: Actually reserve the DMA channel to the system using bcm_dma_chan_alloc() and bcm_dma_chan_free()?...
  // Right now, use (lite) channels 12 and 13 which seem to be free.
  const int freeChannels[] = { /*0, 3,*/ 12, 13, 9, 10, 13, 13 };
  static int nextFreeChannel = 0;

  *dmaChannel = freeChannels[nextFreeChannel++];
  LOG("Allocated DMA channel %d", *dmaChannel);
  *irq = 0;
  return 0;
}

int InitDMA()
{
#ifdef KERNEL_MODULE
  dma = (volatile DMAChannelRegisterFile *)ioremap(BCM2835_PERI_BASE+BCM2835_DMA_BASE, 0x1000);
  int ret = AllocateDMAChannel(&dmaTxChannel, &dmaTxIrq);
  if (ret != 0) FATAL_ERROR("Unable to allocate TX DMA channel!");
  ret = AllocateDMAChannel(&dmaRxChannel, &dmaRxIrq);
  if (ret != 0) FATAL_ERROR("Unable to allocate RX DMA channel!");

  // Enable the allocated DMA channels
  volatile uint32_t *dmaEnableRegister = (volatile uint32_t *)((uint32_t)dma + BCM2835_DMAENABLE_REGISTER_OFFSET);
  *dmaEnableRegister |= (1 << dmaTxChannel);
  *dmaEnableRegister |= (1 << dmaRxChannel);
#else
  dma = (volatile DMAChannelRegisterFile*)((uintptr_t)bcm2835 + BCM2835_DMA_BASE);
  dmaTxChannel = spiTaskMemory->dmaTxChannel;
  dmaRxChannel = spiTaskMemory->dmaRxChannel;
#endif
  LOG("DMA hardware register file is at ptr: %p, using DMA TX channel: %d and DMA RX channel: %d", dma, dmaTxChannel, dmaRxChannel);
  if (!dma) FATAL_ERROR("Failed to map DMA!");

  dmaTx = dma + dmaTxChannel;
  dmaRx = dma + dmaRxChannel;
  LOG("DMA hardware TX channel register file is at ptr: %p, DMA RX channel register file is at ptr: %p", dmaTx, dmaRx);

  // Reset the DMA channels
  LOG("Resetting DMA channels for use");
  dmaTx->cs = BCM2835_DMA_CS_RESET;
  dmaTx->cb.debug = BCM2835_DMA_DEBUG_DMA_READ_ERROR | BCM2835_DMA_DEBUG_DMA_FIFO_ERROR | BCM2835_DMA_DEBUG_READ_LAST_NOT_SET_ERROR;
  dmaRx->cs = BCM2835_DMA_CS_RESET;
  dmaRx->cb.debug = BCM2835_DMA_DEBUG_DMA_READ_ERROR | BCM2835_DMA_DEBUG_DMA_FIFO_ERROR | BCM2835_DMA_DEBUG_READ_LAST_NOT_SET_ERROR;
  
  // TODO: Set up IRQ
  LOG("DMA all set up");
  return 0;
}

// Debugging functions to introspect SPI and DMA hardware registers:

void DumpCS(uint32_t reg)
{
  PRINT_FLAG(BCM2835_DMA_CS_RESET);
  PRINT_FLAG(BCM2835_DMA_CS_ABORT);
  PRINT_FLAG(BCM2835_DMA_CS_DISDEBUG);
  PRINT_FLAG(BCM2835_DMA_CS_WAIT_FOR_OUTSTANDING_WRITES);
  PRINT_FLAG(BCM2835_DMA_CS_PANIC_PRIORITY);
  PRINT_FLAG(BCM2835_DMA_CS_PRIORITY);
  PRINT_FLAG(BCM2835_DMA_CS_ERROR);
  PRINT_FLAG(BCM2835_DMA_CS_WAITING_FOR_OUTSTANDING_WRITES);
  PRINT_FLAG(BCM2835_DMA_CS_DREQ_STOPS_DMA);
  PRINT_FLAG(BCM2835_DMA_CS_PAUSED);
  PRINT_FLAG(BCM2835_DMA_CS_DREQ);
  PRINT_FLAG(BCM2835_DMA_CS_INT);
  PRINT_FLAG(BCM2835_DMA_CS_END);
  PRINT_FLAG(BCM2835_DMA_CS_ACTIVE);
}

void DumpDebug(uint32_t reg)
{
  PRINT_FLAG(BCM2835_DMA_DEBUG_LITE);
  PRINT_FLAG(BCM2835_DMA_DEBUG_VERSION);
  PRINT_FLAG(BCM2835_DMA_DEBUG_DMA_STATE);
  PRINT_FLAG(BCM2835_DMA_DEBUG_DMA_ID);
  PRINT_FLAG(BCM2835_DMA_DEBUG_DMA_OUTSTANDING_WRITES);
  PRINT_FLAG(BCM2835_DMA_DEBUG_DMA_READ_ERROR);
  PRINT_FLAG(BCM2835_DMA_DEBUG_DMA_FIFO_ERROR);
  PRINT_FLAG(BCM2835_DMA_DEBUG_READ_LAST_NOT_SET_ERROR);
}

void DumpTI(uint32_t reg)
{
  PRINT_FLAG(BCM2835_DMA_TI_NO_WIDE_BURSTS);
  PRINT_FLAG(BCM2835_DMA_TI_WAITS);
  PRINT_FLAG(BCM2835_DMA_TI_PERMAP);
  PRINT_FLAG(BCM2835_DMA_TI_BURST_LENGTH);
  PRINT_FLAG(BCM2835_DMA_TI_SRC_IGNORE);
  PRINT_FLAG(BCM2835_DMA_TI_SRC_DREQ);
  PRINT_FLAG(BCM2835_DMA_TI_SRC_WIDTH);
  PRINT_FLAG(BCM2835_DMA_TI_SRC_INC);
  PRINT_FLAG(BCM2835_DMA_TI_DEST_IGNORE);
  PRINT_FLAG(BCM2835_DMA_TI_DEST_DREQ);
  PRINT_FLAG(BCM2835_DMA_TI_DEST_WIDTH);
  PRINT_FLAG(BCM2835_DMA_TI_DEST_INC);
  PRINT_FLAG(BCM2835_DMA_TI_WAIT_RESP);
  PRINT_FLAG(BCM2835_DMA_TI_TDMODE);
  PRINT_FLAG(BCM2835_DMA_TI_INTEN);
}

#define DMA_SPI_FIFO_PHYS_ADDRESS 0x7E204004

#define req(cnd) if (!(cnd)) { LOG("!!!%s!!!\n", #cnd);}

void SPIDMATransfer(SPITask *task)
{
#ifdef KERNEL_MODULE
  // TODO: SPI should be in DONE state at this point, this is rather a paranoia check for debugging time. It should be possible to just
  // remove this block altogether!
  while (!(spi->cs & BCM2835_SPI0_CS_DONE))
  {
    if ((spi->cs & BCM2835_SPI0_CS_DMAEN))
      spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;

    if ((spi->cs & BCM2835_SPI0_CS_CS))
      spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;

    if ((spi->cs & (BCM2835_SPI0_CS_RXR | BCM2835_SPI0_CS_RXF)))
      spi->cs = BCM2835_SPI0_CS_CLEAR_RX | BCM2835_SPI0_CS_TA;
  }

  // Transition the SPI peripheral to enable the use of DMA
  spi->cs = BCM2835_SPI0_CS_DMAEN | BCM2835_SPI0_CS_CLEAR;
  task->dmaSpiHeader = BCM2835_SPI0_CS_TA | (task->size << 16); // The first four bytes written to the SPI data register control the DLEN and CS,CPOL,CPHA settings.

  // TODO: DMA should be in DONE state at this point, it should be possible to remove the following lines! (or if not, we'll want to refactor so that
  // the wait for previous DMA done is done somewhere earlier!)
  while (dmaTx->cbAddr && (dmaTx->cs & BCM2835_DMA_CS_ACTIVE));
  while (dmaRx->cbAddr && (dmaRx->cs & BCM2835_DMA_CS_ACTIVE));

  // TODO: Ideally we would be able to directly perform the DMA from the SPI ring buffer from 'task' pointer. However
  // that pointer is shared to userland, and it is proving troublesome to make it both userland-writable as well as cache-bypassing DMA coherent.
  // Therefore these two memory areas are separate for now, and we memcpy() from SPI ring buffer to an intermediate 'dmaSourceMemory' memory area to perform
  // the DMA transfer. Is there a way to avoid this intermediate buffer? That would improve performance a bit.
  volatile DMAControlBlock *txcb = &dmaSourceMemory->cb[0].cb;
  txcb->ti = BCM2835_DMA_TI_PERMAP_SPI_TX | BCM2835_DMA_TI_DEST_DREQ | BCM2835_DMA_TI_SRC_INC | BCM2835_DMA_TI_WAIT_RESP;
  memcpy((void*)dmaSourceMemory->buffer, (void*)&task->dmaSpiHeader, task->size + 4); // task->size is the actual payload, +4 bytes comes from the SPI DLEN header that DMA writes

  // Source pixel data flows from memory -> SPI 'data' register
  txcb->src = VIRT_TO_BUS(dmaSourceMemory->buffer);
  // Ideally would want the above line to just directly do the following (but it's not cache coherent!):
  //  txcb->src = VIRT_TO_BUS(&task->dmaSpiHeader);

  txcb->dst = DMA_SPI_FIFO_PHYS_ADDRESS; // Write out to the SPI peripheral 
  txcb->len = task->size + 4;
  txcb->stride = 0;
  txcb->next = 0;
  txcb->debug = 0;
  txcb->reserved = 0;
  __sync_synchronize();
  dmaTx->cbAddr = VIRT_TO_BUS(txcb);
  __sync_synchronize();
  req(dmaTx->cbAddr % 256 == 0);

  // SPI DMA transfer needs a second DMA channel to receive. We don't actually use this, so data is not written anywhere, but must just pump the reads to empty the SPI read FIFO.
  volatile DMAControlBlock *rxcb = &dmaSourceMemory->cb[1].cb;
  rxcb->ti = BCM2835_DMA_TI_PERMAP_SPI_RX | BCM2835_DMA_TI_SRC_DREQ | BCM2835_DMA_TI_DEST_IGNORE | BCM2835_DMA_TI_WAIT_RESP;

  rxcb->src = DMA_SPI_FIFO_PHYS_ADDRESS;
  rxcb->dst = 0;
  rxcb->len = task->size;
  rxcb->stride = 0;
  rxcb->next = 0;
  rxcb->debug = 0;
  rxcb->reserved = 0;
  __sync_synchronize();
  dmaRx->cbAddr = VIRT_TO_BUS(rxcb);
  __sync_synchronize();
  req(dmaRx->cbAddr % 256 == 0);

  // TODO: Review the need for any of the flags BCM2835_DMA_CS_INT, BCM2835_DMA_CS_WAIT_FOR_OUTSTANDING_WRITES, BCM2835_DMA_CS_SET_PRIORITY(0x??) or BCM2835_DMA_CS_SET_PANIC_PRIORITY(0x??)
  dmaTx->cs = BCM2835_DMA_CS_ACTIVE | BCM2835_DMA_CS_END;
  __sync_synchronize();
  dmaRx->cs = BCM2835_DMA_CS_ACTIVE | BCM2835_DMA_CS_END;
  __sync_synchronize();

  // XXX DEBUGGING: Synchronously wait for the DMA transfers to finish:
  // Currently there is a bug that occassionally the DMA TX (or RX) transfers will hang waiting for DREQ, seemingly for no reason.
#if 1
  uint64_t now = tick();
  while(!(dmaTx->cs & BCM2835_DMA_CS_END) && dmaTx->cb.src - txcb->src < task->size)
  {
    if (tick() - now > 100000)
    {
      DumpCS(dmaTx->cs);
      DumpDebug(dmaTx->cb.debug);
      DumpTI(dmaTx->cb.ti);
      DumpSPICS(spi->cs);
      LOG("Waiting for TX, CB physAddr: %p, src physAddr: %p->%p, dst physAddr: %p", 
        (void*)VIRT_TO_BUS(txcb), (void*)txcb->src, (void*)(txcb->src+txcb->len), (void*)txcb->dst);
      LOG("CS: %x, cbAddr: %p, ti: %x, src: %p, dst: %p, len: %u, stride: %u, nextConBk: %p, debug: %x",
        dmaTx->cs, (void*)dmaTx->cbAddr, dmaTx->cb.ti, (void*)dmaTx->cb.src, (void*)dmaTx->cb.dst, dmaTx->cb.len, dmaTx->cb.stride, (void*)dmaTx->cb.next, dmaTx->cb.debug);
      LOG("Header %x, dlen: %u, bytesWritten: %u", task->dmaSpiHeader, spi->dlen, dmaTx->cb.src - txcb->src);

      // XXX HACK: When we detect we've stalled, just move on
      dmaTx->cs = BCM2835_DMA_CS_ABORT | BCM2835_DMA_CS_RESET;
      break;
    }
  }

  now = tick();
  while(!(dmaRx->cs & BCM2835_DMA_CS_END))
  {
    if (tick() - now > 100000)
    {
      LOG("TX:");
      DumpCS(dmaTx->cs);
      DumpDebug(dmaTx->cb.debug);
      DumpTI(dmaTx->cb.ti);
      DumpSPICS(spi->cs);
      LOG("Waiting for TX, CB physAddr: %p, src physAddr: %p->%p, dst physAddr: %p", (void*)VIRT_TO_BUS(txcb), (void*)txcb->src, (void*)(txcb->src+txcb->len), (void*)txcb->dst);
      LOG("CS: %x, cbAddr: %p, ti: %x, src: %p, dst: %p, len: %u, stride: %u, nextConBk: %p, debug: %x",
        dmaTx->cs, (void*)dmaTx->cbAddr, dmaTx->cb.ti, (void*)dmaTx->cb.src, (void*)dmaTx->cb.dst, dmaTx->cb.len, dmaTx->cb.stride, (void*)dmaTx->cb.next, dmaTx->cb.debug);
      LOG("Header %x, dlen: %u, bytesWritten: %u", task->dmaSpiHeader, spi->dlen, dmaTx->cb.src - txcb->src);

      LOG("RX:");
      DumpCS(dmaRx->cs);
      DumpDebug(dmaRx->cb.debug);
      DumpTI(dmaRx->cb.ti);
      DumpSPICS(spi->cs);
      LOG("Waiting for RX, CB physAddr: %p, src physAddr: %p, dst physAddr: %p->%p", (void*)VIRT_TO_BUS(rxcb), (void*)rxcb->src, (void*)rxcb->dst, (void*)(rxcb->dst+rxcb->len));
      LOG("CS: %x, cbAddr: %p, ti: %x, src: %p, dst: %p, len: %u, stride: %u, nextConBk: %p, debug: %x",
        dmaRx->cs, (void*)dmaRx->cbAddr, dmaRx->cb.ti, (void*)dmaRx->cb.src, (void*)dmaRx->cb.dst, dmaRx->cb.len, dmaRx->cb.stride, (void*)dmaRx->cb.next, dmaRx->cb.debug);
      LOG("Header %x, dlen: %u, bytesRead: %d", task->dmaSpiHeader, spi->dlen, dmaRx->cb.dst - rxcb->dst);

      dmaRx->cs = BCM2835_DMA_CS_ABORT | BCM2835_DMA_CS_RESET;
      break;
    }
  }
#endif

  // Remove DMAEN from SPI to disable DMA transfers, we might be doing some polled SPI transfers next.
  __sync_synchronize();
  spi->cs = BCM2835_SPI0_CS_TA | BCM2835_SPI0_CS_CLEAR;
  __sync_synchronize();
#endif
}

#endif
