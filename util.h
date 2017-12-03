#pragma once

#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define MAX(x, y) ((x) >= (y) ? (x) : (y))

#ifdef KERNEL_MODULE
#define FATAL_ERROR(msg) do { pr_alert(msg "\n"); return -1; } while(0)
#else
#define FATAL_ERROR(msg) do { fprintf(stderr, "%s\n", msg); syslog(LOG_ERR, msg); exit(1); } while(0)
#endif

