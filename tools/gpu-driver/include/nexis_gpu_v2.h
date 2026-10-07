#ifndef NEXIS_GPU_RETAINED_ABI_H
#define NEXIS_GPU_RETAINED_ABI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define NEXIS_GPU_ABI_RETAINED 2u
#define NEXIS_GPU_V2_HEADER 64u
#define NEXIS_GPU_V2_MAX_BYTES (1024u*1024u)
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define NEXIS_GPU_CALL __attribute__((sysv_abi))
#else
#define NEXIS_GPU_CALL
#endif
/* NDRV2 payload: PIC text, read-only data, writable data, zero-filled BSS.
 * No relocations/imports. Offsets are relative to the mapped payload.
 * The kernel retains code, services and driver state until shutdown.
 * All returned pointers must belong to this module's appropriate segment. */
typedef struct {
    uint32_t entry,image_bytes,text_bytes,writable_offset,memory_bytes;
    uint16_t vendor,device;
} nexis_gpu_image;
typedef struct {
    uint32_t pixel_khz,hactive,hsync_start,hsync_end,htotal;
    uint32_t vactive,vsync_start,vsync_end,vtotal,flags;
} nexis_gpu_timing;
#define NEXIS_GPU_SCANOUT_ACTIVE 1u
#define NEXIS_GPU_SCANOUT_HDMI 2u
#define NEXIS_GPU_SCANOUT_AUDIO 4u
typedef struct {
    nexis_gpu_timing timing;
    uint64_t framebuffer;
    uint32_t pitch,format,flags,reserved;
} nexis_gpu_scanout;
typedef struct {
    uint32_t abi,size,width,height,pitch,format;
    uint16_t vendor,device;
    uint8_t bus,slot,func,reserved;
    uint64_t framebuffer,framebuffer_bytes;
    const uint8_t *rom;uint32_t rom_bytes,reserved2;
    void *service_context;
    bool (NEXIS_GPU_CALL *read32)(void *,unsigned bar,uint32_t byte_offset,uint32_t *);
    bool (NEXIS_GPU_CALL *write32)(void *,unsigned bar,uint32_t byte_offset,uint32_t);
    uint64_t (NEXIS_GPU_CALL *time_us)(void *);
    bool (NEXIS_GPU_CALL *delay_us)(void *,uint32_t);
} nexis_gpu_services;
typedef struct {
    uint32_t abi,size;
    void *state;uint32_t state_bytes,reserved;
    const char *name;
    /* Values reflect hardware/connector capabilities, not advertised EDID. */
    uint32_t max_pixel_khz,max_tmds_khz;
    bool hdmi,scdc;uint8_t reserved2[6];
    bool (NEXIS_GPU_CALL *read_mode)(void *,nexis_gpu_scanout *);
    bool (NEXIS_GPU_CALL *set_mode)(void *,const nexis_gpu_timing *);
    void (NEXIS_GPU_CALL *poll)(void *);
    void (NEXIS_GPU_CALL *shutdown)(void *);
} nexis_gpu_instance;
typedef int (NEXIS_GPU_CALL *nexis_gpu_entry_v2)(const nexis_gpu_services *,nexis_gpu_instance *);
bool nexis_gpu_image_parse(const uint8_t *,size_t,uint16_t,uint16_t,nexis_gpu_image *);
bool nexis_gpu_image_code_pointer(const nexis_gpu_image *,uintptr_t base,uintptr_t pointer);
bool nexis_gpu_image_state_pointer(const nexis_gpu_image *,uintptr_t base,uintptr_t pointer,size_t bytes);
#endif
