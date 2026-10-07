#include "display.h"
#include "../serial.h"
static edid_monitor monitor;
static uint32_t active_refresh=60000;
void display_monitor_init(const nexis_boot_info_t *boot){
    if(boot && boot->gpu_vendor)kprintf("[DISPLAY] Firmware output PCI %04x:%04x at %u:%u.%u; copied ROM %u bytes (data only)\n",
        boot->gpu_vendor,boot->gpu_device,boot->gpu_bus,boot->gpu_slot,boot->gpu_func,boot->gpu_rom_size);
    if(!boot || !boot->edid_size){kprintf("[DISPLAY] No output-associated firmware EDID; native connector probing required\n");return;}
    if(boot->edid_size>sizeof(boot->edid) || !edid_parse(boot->edid,boot->edid_size,&monitor)){
        kprintf("[DISPLAY] Invalid/incomplete EDID data rejected\n");return;
    }
    kprintf("[DISPLAY] %s %s: %u advertised timings; HDMI %u, max TMDS %u kHz, SCDC %u, stereo PCM %u\n",
        monitor.manufacturer,monitor.name,monitor.count,monitor.hdmi,monitor.max_tmds_khz,monitor.scdc,monitor.stereo_48k16);
    uint32_t maximum=0;
    for(unsigned i=0;i<monitor.count;i++){
        const edid_timing *t=&monitor.modes[i];
        if(t->hactive!=boot->screen_width || t->vactive!=boot->screen_height || t->flags&(EDID_INTERLACE|EDID_DOUBLE_CLOCK|EDID_Y420_ONLY))continue;
        uint32_t hz=edid_refresh_millihz(t);if(hz>maximum)maximum=hz;
    }
    kprintf("[DISPLAY] Advertised %ux%u maximum: %u.%03u Hz (not an active/native mode claim)%s\n",
        boot->screen_width,boot->screen_height,maximum/1000,maximum%1000,monitor.incomplete?"; some descriptors unsupported":"");
}
const edid_monitor *display_monitor_get(void){return monitor.valid?&monitor:NULL;}
void display_set_native_mode(const edid_timing *timing){
    uint32_t refresh=edid_refresh_millihz(timing);
    if(refresh>=20000 && refresh<=1000000)active_refresh=refresh;
}
void display_clear_native_mode(void){active_refresh=60000;}
uint32_t display_active_refresh_millihz(void){return active_refresh;}
