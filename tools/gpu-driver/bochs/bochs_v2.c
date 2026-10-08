/* QEMU standard VGA / Bochs DISPI (PCI 1234:1111) as a retained v2 module.
 *
 * Purpose: the only GPU model that exists inside QEMU, so the *complete*
 * download -> signature check -> retained load -> activation -> persistence
 * chain can be tested without real hardware.  The DISPI interface has no
 * pixel clock, link or audio.  The reported timing is bookkeeping only; it
 * is never flagged NEXIS_GPU_SCANOUT_CLOCK_MEASURED and must not be read as
 * a real refresh rate.  Physical cards live in tools/gpu-driver/amd/ etc.
 *
 * Register interface: https://www.qemu.org/docs/master/specs/standard-vga.html
 * The services only offer 32-bit register access.  Two adjacent 16-bit DISPI
 * registers share one dword; every write is a read-modify-write of that
 * dword (the neighbour is written back unchanged, which QEMU accepts).
 */
#include "../include/nexis_gpu_v2.h"
#include <string.h>

#define BAR_REGS 2u
#define DISPI(i) (0x500u + (i) * 2u)
enum { ID, XRES, YRES, BPP, ENABLE, BANK, VIRT_W, VIRT_H, X_OFF, Y_OFF };
#define ENABLED 0x01u
#define LFB 0x40u
#define NOCLEAR 0x80u

static struct {
    const nexis_gpu_services *s;
    uint16_t saved[10];
    nexis_gpu_timing timing;
    uint32_t pitch;
    bool ready;
} B;

static bool rd(unsigned i, uint16_t *out) {
    uint32_t v;
    if (!B.s->read32(B.s->service_context, BAR_REGS, DISPI(i) & ~3u, &v)) return false;
    *out = (DISPI(i) & 2u) ? (uint16_t)(v >> 16) : (uint16_t)v;
    return true;
}
static bool wr(unsigned i, uint16_t value) {
    uint32_t v;
    if (!B.s->read32(B.s->service_context, BAR_REGS, DISPI(i) & ~3u, &v)) return false;
    v = (DISPI(i) & 2u) ? (v & 0xffffu) | (uint32_t)value << 16 : (v & 0xffff0000u) | value;
    return B.s->write32(B.s->service_context, BAR_REGS, DISPI(i) & ~3u, v);
}
/* Synthetic reduced-blanking style timing for the *current* geometry. */
static void default_timing(nexis_gpu_timing *t) {
    uint32_t w = B.s->width, h = B.s->height;
    t->hactive = w; t->hsync_start = w + 48; t->hsync_end = w + 80; t->htotal = w + 160;
    t->vactive = h; t->vsync_start = h + 3; t->vsync_end = h + 8; t->vtotal = h + 30;
    t->flags = 1; /* +hsync, -vsync */
    t->pixel_khz = (uint32_t)((uint64_t)t->htotal * t->vtotal * 60u / 1000u);
}
static bool program(uint16_t enable_flags) {
    return wr(ENABLE, 0) && wr(XRES, (uint16_t)B.s->width) && wr(YRES, (uint16_t)B.s->height) && wr(BPP, 32) &&
           wr(BANK, 0) && wr(ENABLE, enable_flags) &&
           /* ENABLE resets virtual geometry; apply pitch/offsets afterwards. */
           wr(VIRT_W, (uint16_t)B.pitch) && wr(X_OFF, 0) && wr(Y_OFF, 0);
}
static void restore(void) {
    wr(ENABLE, 0);
    wr(XRES, B.saved[XRES]); wr(YRES, B.saved[YRES]); wr(BPP, B.saved[BPP]); wr(BANK, B.saved[BANK]);
    wr(ENABLE, (uint16_t)(B.saved[ENABLE] | NOCLEAR));
    wr(VIRT_W, B.saved[VIRT_W]); wr(X_OFF, B.saved[X_OFF]); wr(Y_OFF, B.saved[Y_OFF]);
}
static bool verify(void) {
    uint16_t v[10];
    for (unsigned i = XRES; i <= Y_OFF; i++) if (!rd(i, &v[i])) return false;
    return v[XRES] == B.s->width && v[YRES] == B.s->height && v[BPP] == 32 &&
           (v[ENABLE] & (ENABLED | LFB)) == (ENABLED | LFB) && v[VIRT_W] == B.pitch && !v[X_OFF] && !v[Y_OFF];
}
static bool NEXIS_GPU_CALL read_mode(void *p, nexis_gpu_scanout *out) {
    (void)p;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!B.ready || !verify()) return false;
    out->timing = B.timing;
    out->framebuffer = B.s->framebuffer; out->pitch = B.s->pitch; out->format = B.s->format;
    out->flags = NEXIS_GPU_SCANOUT_ACTIVE; /* virtual: no HDMI, no audio, clock not measured */
    return true;
}
static bool NEXIS_GPU_CALL set_mode(void *p, const nexis_gpu_timing *t) {
    (void)p;
    if (!B.ready || !t || t->hactive != B.s->width || t->vactive != B.s->height || !t->pixel_khz) return false;
    B.timing = *t; /* bookkeeping only; DISPI has no clock to program */
    return verify();
}
static void NEXIS_GPU_CALL poll(void *p) { (void)p; if (B.ready && !verify()) B.ready = false; }
static void NEXIS_GPU_CALL shutdown(void *p) { (void)p; B.ready = false; }

int NEXIS_GPU_CALL driver_init_v2(const nexis_gpu_services *services, nexis_gpu_instance *instance) {
    if (!instance) return -1;
    memset(instance, 0, sizeof(*instance));
    if (!services || services->abi != 2 || services->size != NEXIS_GPU_SERVICES_RESOURCE_BYTES ||
        services->vendor != 0x1234 || services->device != 0x1111 || !services->read32 || !services->write32 ||
        !services->framebuffer || !services->width || !services->height || services->width > 4096 ||
        services->height > 2160 || services->pitch < services->width || services->pitch > 8192 ||
        (uint64_t)services->pitch * services->height * 4 > services->framebuffer_bytes)
        return -2;
    memset(&B, 0, sizeof(B));
    B.s = services; B.pitch = services->pitch;
    uint16_t id;
    if (!rd(ID, &id) || id < 0xb0c2 || id > 0xb0c5) return -3;
    for (unsigned i = 0; i < 10; i++) if (!rd(i, &B.saved[i])) return -4;
    if (!program(ENABLED | LFB | NOCLEAR)) { restore(); return -5; }
    default_timing(&B.timing);
    if (!verify()) { restore(); return -6; }
    B.ready = true;
    volatile nexis_gpu_instance *o = instance;
    o->abi = 2; o->size = sizeof(*instance); o->state = &B; o->state_bytes = sizeof(B);
    o->name = "QEMU/Bochs virtual display (timings are bookkeeping)";
    o->max_pixel_khz = 600000; o->max_tmds_khz = 0; o->hdmi = false; o->scdc = false;
    o->read_mode = read_mode; o->set_mode = set_mode; o->poll = poll; o->shutdown = shutdown;
    return 0;
}
