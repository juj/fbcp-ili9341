#pragma once

#include <inttypes.h>
#include <unistd.h>

uint64_t tick();

#ifdef NO_THROTTLING
#define usleep(x) ((void)0)
#endif
