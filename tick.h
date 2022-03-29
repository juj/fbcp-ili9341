#pragma once

#ifndef KERNEL_MODULE
#include <inttypes.h>
#include <unistd.h>

// Initialized in spi.cpp along with the rest of the BCM2835 peripheral:
#ifdef TIMER_32BIT
struct __attribute__((packed, aligned(4))) systemTimer {
    volatile uint32_t cs;
    volatile uint32_t clo;
    volatile uint32_t chi;
    volatile uint32_t c[4];
};
#define TIMER_TYPE systemTimer
extern volatile systemTimer* systemTimerRegister;
#define tick() ((uint64_t)(systemTimerRegister->chi))
#else
#define TIMER_TYPE uint64_t
extern volatile uint64_t *systemTimerRegister;
#define tick() (*systemTimerRegister)
#endif

#endif


#ifdef NO_THROTTLING
#define usleep(x) ((void)0)
#endif
