#pragma once

#include "spi.h"

//#include <inttypes.h>
//#include <config/sysfs/syscall.h>
#define VIRT_TO_BUS(ptr) ((uintptr_t)(ptr) | 0xC0000000U)
