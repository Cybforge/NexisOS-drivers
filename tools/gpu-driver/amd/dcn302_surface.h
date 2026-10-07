#ifndef NEXIS_DCN302_SURFACE_H
#define NEXIS_DCN302_SURFACE_H
#include "dcn302_route.h"
#include "dcn302_surface_regs.h"
enum dcn302_surface_error {DCN302_SURFACE_OK,DCN302_SURFACE_INPUT,DCN302_SURFACE_IO,DCN302_SURFACE_UNSUPPORTED,DCN302_SURFACE_BOUNDS,DCN302_SURFACE_CHANGED};
typedef struct {unsigned mpcc,hubp;uint32_t pitch,format;uint64_t gpu_address,cpu_address,bytes;} dcn302_surface;
/* Read-only native MPC -> paired DPP/HUBP -> actual INUSE VRAM surface proof.
 * Supports one uncompressed, unrotated linear RGB8888 VM0 plane, with no flip
 * pending. bar_base/bar_bytes must be the independently verified firmware PCI
 * VRAM aperture. GOP parameters come from boot handoff, not a driver assertion.
 * GPU physical VRAM offset and CPU BAR address are translated separately.
 * No surface writes, page tables or GOP timing are reused as hardware proof. */
enum dcn302_surface_error dcn302_surface_bind(const dcn302_io *,const dcn302_route *,uint64_t bar_base,uint64_t bar_bytes,
    uint64_t gop_base,uint64_t gop_bytes,uint32_t pitch,uint32_t format,dcn302_surface *);
#endif
