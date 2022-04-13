#ifdef USE_VCSM_CMA

#include "config.h"
#include "cma.h"
#include "util.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int cma_fd = -1;
#define PAGE_SIZE 4096

void OpenVCSM(void) {
  cma_fd = open("/dev/vcsm-cma", O_RDWR|O_SYNC);
  if (cma_fd < 0) FATAL_ERROR("can't open /dev/vcsm-cma");
}

void CloseVCSM(void) {
  if (cma_fd >= 0) {
    close(cma_fd);
  }
}

const int NAME_LENGTH = 32;

struct Allocate {
  /* user -> kernel */
  uint32_t size;
  uint32_t num;
  uint32_t flags;
  uint32_t pad;
  char name[NAME_LENGTH];

  /* kernel -> user */
  int32_t fd;
  uint32_t vcHandle;
  uint64_t dmaAddr;
};

int AllocateCMA(const char* reason, size_t req, CMAInfo* res) {
  if (res == NULL) {
    return -1;
  }
  Allocate ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.size = ALIGN_UP(req, PAGE_SIZE);
  ctx.flags = 0; // NO cache
  strncpy((char*)ctx.name, reason, NAME_LENGTH -1);
  ctx.num = 1;
  if (ioctl(cma_fd, _IOR('J', 0x5A, struct Allocate), &ctx) < 0 || ctx.fd < 0) { // allocate cmd
    return -1;
  }
  res->size = ctx.size;
  res->vcHandle = ctx.vcHandle;
  res->dmaAddr = ctx.dmaAddr;
  res->fd = ctx.fd;
  return 0;
}

#endif
