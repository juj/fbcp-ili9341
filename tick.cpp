#include <time.h>

#include "config.h"
#include "tick.h"

uint64_t tick()
{
  struct timespec start;
  clock_gettime(CLOCK_REALTIME, &start);
  return start.tv_sec * 1000000 + start.tv_nsec / 1000;
}
