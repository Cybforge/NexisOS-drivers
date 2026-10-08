#ifndef NEXIS_GPU_NXLOG_H
#define NEXIS_GPU_NXLOG_H
#include "../include/nexis_gpu_v2.h"
/* Tiny freestanding logger for driver modules (no libc, no imports).
 * Output goes to the kernel log through services->log (needs a module header
 * that declares NEXIS_GPU_SERVICES_LOG_BYTES). Supported: %u %d %x %X %s %c %%
 * with optional 0, width and ll / z length modifiers, e.g. "%08x", "%llx".
 * Lines are cut at 199 characters. Safe to call before nx_log_attach(): ignored. */
void nx_log_attach(const nexis_gpu_services *services);
void nx_logf(const char *format, ...) __attribute__((format(printf, 1, 2)));
#endif
