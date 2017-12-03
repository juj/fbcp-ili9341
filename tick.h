#pragma once

#ifndef KERNEL_MODULE
#include <inttypes.h>
#include <unistd.h>

uint64_t tick(void);

#endif


#ifdef NO_THROTTLING
#define usleep(x) ((void)0)
#endif
