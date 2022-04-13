#pragma once
#ifdef USE_VCSM_CMA

#include <memory.h>
#include <inttypes.h>
struct CMAInfo {
  size_t size;
  uintptr_t dmaAddr;
  uint32_t fd;
  uint32_t vcHandle;
};

void OpenVCSM(void);
void CloseVCSM(void);
int AllocateCMA(const char* reason, size_t req, CMAInfo* res);
#endif