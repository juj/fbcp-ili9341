#pragma once

#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define MAX(x, y) ((x) >= (y) ? (x) : (y))

#define FATAL_ERROR(msg) do { fprintf(stderr, "%s\n", msg); syslog(LOG_ERR, msg); exit(1); } while(0)
