#pragma once

#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#define MAX(x, y) ((x) >= (y) ? (x) : (y))

#ifdef KERNEL_MODULE
#define LOG(...) do { printk(KERN_INFO __VA_ARGS__); } while(0)
#define FATAL_ERROR(msg) do { pr_alert(msg "\n"); return -1; } while(0)
#else
#define LOG(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define FATAL_ERROR(msg) do { fprintf(stderr, "%s\n", msg); syslog(LOG_ERR, msg); exit(1); } while(0)
#endif

#ifdef KERNEL_MODULE
#define PRINT_FLAG_2(flag_str, flag, shift) printk(KERN_INFO flag_str ": %x", (reg & flag) >> shift)
#else
#define PRINT_FLAG_2(flag_str, flag, shift) printf(flag_str ": %x\n", (reg & flag) >> shift)
#endif

#define PRINT_FLAG(flag) PRINT_FLAG_2(#flag, flag, flag##_SHIFT)

#ifndef KERNEL_MODULE
#define cpu_relax() asm volatile("yield" ::: "memory")
#endif
