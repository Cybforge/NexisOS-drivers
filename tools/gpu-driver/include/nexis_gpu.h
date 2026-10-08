#ifndef NEXIS_GPU_MODULE_ABI_H
#define NEXIS_GPU_MODULE_ABI_H
#include <stdint.h>
/* Version 1 modules are init-only, position-independent and have no writable
 * globals, imported symbols, callbacks, interrupt handlers or DMA allocations.
 * The pinned image is called once and unloaded after returning. */
#define NEXIS_GPU_ABI 1u
#define NEXIS_GPU_BGR 1u
typedef struct {
 uint32_t abi,width,height,pitch,format,reserved;
 uint64_t framebuffer,framebuffer_bytes,registers;
 int (*activate)(uint64_t framebuffer,uint32_t pitch,uint32_t format,const char *name);
} nexis_gpu_context;
int driver_init(const nexis_gpu_context *context);
#endif
