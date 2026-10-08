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
    uint32_t services_bytes;
} nexis_gpu_image;
typedef struct {
    uint32_t pixel_khz,hactive,hsync_start,hsync_end,htotal;
    uint32_t vactive,vsync_start,vsync_end,vtotal,flags;
} nexis_gpu_timing;
#define NEXIS_GPU_SCANOUT_ACTIVE 1u
#define NEXIS_GPU_SCANOUT_HDMI 2u
#define NEXIS_GPU_SCANOUT_AUDIO 4u
/* Clock was measured from the running native hardware against calibrated time.
 * Readback may differ by <=1,000 ppm from the requested nominal pixel clock.
 * All geometry/polarity fields must still match exactly. A GOP/EDID clock is
 * not a measured clock. The kernel publishes the actual measured refresh. */
#define NEXIS_GPU_SCANOUT_CLOCK_MEASURED 8u
typedef struct {
    nexis_gpu_timing timing;
    uint64_t framebuffer;
    uint32_t pitch,format,flags,reserved;
} nexis_gpu_scanout;
bool nexis_gpu_mode_readback_matches(const nexis_gpu_scanout *,const nexis_gpu_timing *);
/* V2 has two defined service prefixes. Older 104-byte modules remain loadable;
 * modules requiring resource() declare 112 bytes in their NDRV header. The
 * kernel sets size to that declared prefix, never silently changes an old ABI. */
#define NEXIS_GPU_SERVICES_BASE_BYTES 104u
#define NEXIS_GPU_SERVICES_RESOURCE_BYTES 112u
/* Third prefix: adds log() and the monitor's EDID. A module that declares 136 bytes in its NDRV
 * header gets size==136 and may use them; older modules keep their smaller prefix. */
#define NEXIS_GPU_SERVICES_LOG_BYTES 136u
#define NEXIS_GPU_RESOURCE_MEMORY 1u
#define NEXIS_GPU_RESOURCE_64BIT 2u
#define NEXIS_GPU_RESOURCE_PREFETCH 4u
#define NEXIS_GPU_RESOURCE_REGISTERS 8u
typedef struct {uint64_t base,bytes;uint32_t flags,reserved;} nexis_gpu_resource;
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
    /* Read-only PCI extent, independently matched to UEFI and current PCI
     * config. Does not map/access VRAM. REGISTERS alone permits read32/write32.
     * Failure clears the output. The upper slot of a 64-bit BAR is not a BAR. */
    bool (NEXIS_GPU_CALL *resource)(void *,unsigned bar,nexis_gpu_resource *);
    /* Diagnostics: one line of printable ASCII (<=199 chars) into the kernel log
     * ("dmesg" in the Terminal). Hardware bring-up depends on these lines, so
     * every driver logs its probe result, each modeset step and register values
     * on failure. Only valid when size>=NEXIS_GPU_SERVICES_LOG_BYTES. */
    void (NEXIS_GPU_CALL *log)(void *,const char *);
    /* EDID of the monitor on the firmware's output (copied by the loader from the same output handle as the
     * framebuffer); NULL/0 when firmware could not provide one. Read-only, valid for the module's lifetime.
     * Used for the audio capabilities (CTA short audio descriptors, speaker allocation). */
    const uint8_t *edid;
    uint32_t edid_bytes,reserved3;
} nexis_gpu_services;
_Static_assert(offsetof(nexis_gpu_services,resource)==NEXIS_GPU_SERVICES_BASE_BYTES,"GPU service prefix");
_Static_assert(offsetof(nexis_gpu_services,log)==NEXIS_GPU_SERVICES_RESOURCE_BYTES,"GPU service resource prefix");
_Static_assert(sizeof(nexis_gpu_services)==NEXIS_GPU_SERVICES_LOG_BYTES,"GPU service layout");
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
