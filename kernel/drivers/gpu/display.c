#include "display.h"
#include "../serial.h"
#include "../../sys/locale.h"
static edid_monitor monitor;
static uint32_t active_refresh=60000;
void display_monitor_init(const nexis_boot_info_t *boot){
    if(boot && boot->gpu_vendor)kprintf("[DISPLAY] Firmware-Ausgabe PCI %04x:%04x bei %u:%u.%u; ROM kopiert (%u Bytes, nur Daten)\n",
        boot->gpu_vendor,boot->gpu_device,boot->gpu_bus,boot->gpu_slot,boot->gpu_func,boot->gpu_rom_size);
    if(!boot || !boot->edid_size){kprintf("[DISPLAY] Keine Firmware-EDID für die Ausgabe vorhanden; native Anschlusserkennung erforderlich\n");return;}
    if(boot->edid_size>sizeof(boot->edid) || !edid_parse(boot->edid,boot->edid_size,&monitor)){
        kprintf("[DISPLAY] Ungültige oder unvollständige EDID-Daten abgelehnt\n");return;
    }
    kprintf("[DISPLAY] %s %s: %u angekündigte Timings; HDMI %u, max. TMDS %u kHz, SCDC %u, Stereo-PCM %u\n",
        monitor.manufacturer,monitor.name,monitor.count,monitor.hdmi,monitor.max_tmds_khz,monitor.scdc,monitor.stereo_48k16);
    uint32_t maximum=0;
    for(unsigned i=0;i<monitor.count;i++){
        const edid_timing *t=&monitor.modes[i];
        if(t->hactive!=boot->screen_width || t->vactive!=boot->screen_height || t->flags&(EDID_INTERLACE|EDID_DOUBLE_CLOCK|EDID_Y420_ONLY))continue;
        uint32_t hz=edid_refresh_millihz(t);if(hz>maximum)maximum=hz;
    }
    kprintf("[DISPLAY] Angekündigtes Maximum %ux%u: %u.%03u Hz (keine Aussage über einen aktiven oder nativen Modus)%s\n",
        boot->screen_width,boot->screen_height,maximum/1000,maximum%1000,tr(monitor.incomplete?"; einige Deskriptoren nicht unterstützt":""));
}
const edid_monitor *display_monitor_get(void){return monitor.valid?&monitor:NULL;}
void display_set_native_mode(const edid_timing *timing){
    uint32_t refresh=edid_refresh_millihz(timing);
    if(refresh>=20000 && refresh<=1000000)active_refresh=refresh;
}
void display_clear_native_mode(void){active_refresh=60000;}
uint32_t display_active_refresh_millihz(void){return active_refresh;}
