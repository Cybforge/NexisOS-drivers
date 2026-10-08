#include "runtime.h"
#include "pci_resource.h"
#include "display.h"
#include "../framebuffer.h"
#include "../serial.h"
#include "../../include/bootinfo.h"
#include "../../include/string.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../arch/x86_64/pit.h"
#include "../../../tools/gpu-driver/include/nexis_gpu_v2.h"
#define BASE GPU_MODULE_VIRTUAL_BASE
static struct {
    bool resident,active,calling,have_initial;
    uint64_t physical,last_poll_us,bar[6],bar_bytes[6];size_t pages,mapped;
    nexis_gpu_image image;
    nexis_gpu_services services;
    nexis_gpu_instance driver;
    nexis_gpu_resource resources[6];
    nexis_gpu_scanout initial; /* firmware (GOP) mode read before the first set_mode; target of gpu_runtime_revert() */
    pci_device_t device;
    char name[64];
} R;
static uint64_t NEXIS_GPU_CALL time_us(void *context){
    (void)context;uint64_t tsc,hz=pit_tsc_hz();uint32_t lo,hi;
    __asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));tsc=(uint64_t)hi<<32|lo;
    if(!hz)return pit_get_ticks()*1000;
    return tsc/hz*1000000+(tsc%hz)*1000000/hz;
}
static bool NEXIS_GPU_CALL delay_us(void *context,uint32_t us){
    if(us>250000)return false;
    uint64_t start=time_us(context);
    while(time_us(context)-start<us)__asm__ volatile("pause");
    return true;
}
static bool register_address(void *context,unsigned bar,uint32_t offset,uint64_t *out){
    if(context!=&R || bar>=6 || (offset&3) || !R.bar[bar] || R.bar_bytes[bar]<4 || offset>R.bar_bytes[bar]-4)return false;
    *out=R.bar[bar]+offset;return true;
}
static bool NEXIS_GPU_CALL read32(void *context,unsigned bar,uint32_t offset,uint32_t *value){
    uint64_t address;if(!value || !register_address(context,bar,offset,&address))return false;
    *value=*(volatile uint32_t *)(uintptr_t)address;return true;
}
static bool NEXIS_GPU_CALL write32(void *context,unsigned bar,uint32_t offset,uint32_t value){
    uint64_t address;if(!register_address(context,bar,offset,&address))return false;
    *(volatile uint32_t *)(uintptr_t)address=value;__asm__ volatile("mfence":::"memory");return true;
}
static void read_bars(const pci_device_t *d,uint32_t raw[6]){
    for(unsigned n=0;n<6;n++)raw[n]=pci_read_dword(d->bus,d->slot,d->func,0x10+n*4);
}
static void firmware_ranges(const nexis_boot_info_t *boot,uint64_t base[6],uint64_t bytes[6]){
    /* Boot handoff is packed. Copy fields to aligned arrays before decoding. */
    for(unsigned n=0;n<6;n++){base[n]=boot->gpu_bar_address[n];bytes[n]=boot->gpu_bar_bytes[n];}
}
static bool device_ready(const pci_device_t *d){
    return pci_read_dword(d->bus,d->slot,d->func,0)==((uint32_t)d->device_id<<16|d->vendor_id) &&
        (pci_read_dword(d->bus,d->slot,d->func,8)>>24)==3 && (pci_read_word(d->bus,d->slot,d->func,4)&2) &&
        !(pci_read_word(d->bus,d->slot,d->func,0x0e)&0x7f);
}
static bool NEXIS_GPU_CALL resource(void *context,unsigned bar,nexis_gpu_resource *out){
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(context!=&R || bar>=6)return false;
    const nexis_boot_info_t *boot=bootinfo_get();const pci_device_t *d=&R.device;
    if(!boot || !device_ready(d))return false;
    uint32_t raw[6],again[6];uint64_t base[6],bytes[6];read_bars(d,raw);firmware_ranges(boot,base,bytes);
    nexis_gpu_resource live;
    if(!gpu_pci_resource_decode(raw,base,bytes,bar,&live) ||
       live.base!=R.resources[bar].base || live.bytes!=R.resources[bar].bytes ||
       live.flags!=(R.resources[bar].flags&~NEXIS_GPU_RESOURCE_REGISTERS))return false;
    read_bars(d,again);
    if(memcmp(raw,again,sizeof(raw)) || !device_ready(d))return false;
    /* Confirm this aperture last, after the full sweeps. PCI display changes
     * are serialized by the caller; this also detects a late BAR mutation
     * after its slot was read early in a sweep. Never write/probe a BAR. */
    if((live.flags&NEXIS_GPU_RESOURCE_64BIT) && pci_read_dword(d->bus,d->slot,d->func,0x14+bar*4)!=raw[bar+1])return false;
    if(pci_read_dword(d->bus,d->slot,d->func,0x10+bar*4)!=raw[bar])return false;
    *out=R.resources[bar];return true;
}
static void release(bool stop){
    if(stop && R.driver.shutdown)R.driver.shutdown(R.driver.state);
    for(size_t n=0;n<R.mapped;n++)vmm_unmap_page(vmm_get_kernel_pml4(),BASE+n*4096);
    if(R.physical){
        /* Free only after all identity aliases are writable again. */
        if(vmm_protect_identity(R.physical,R.pages*4096,PAGE_WRITABLE|PAGE_NO_EXECUTE))
            for(size_t n=0;n<R.pages;n++)pmm_free_page(R.physical+n*4096);
        else kprintf("[GPU] Module pages quarantined: identity protection restore failed\n");
    }
    memset(&R,0,sizeof(R));display_clear_native_mode();
}
static bool validate_driver(void){
    const nexis_gpu_instance *d=&R.driver;
    if(d->abi!=2 || d->size!=sizeof(*d) || d->reserved || d->max_pixel_khz>4000000 || d->max_tmds_khz>600000 ||
       !nexis_gpu_image_state_pointer(&R.image,BASE,(uintptr_t)d->state,d->state_bytes) ||
       !nexis_gpu_image_code_pointer(&R.image,BASE,(uintptr_t)d->read_mode) ||
       !nexis_gpu_image_code_pointer(&R.image,BASE,(uintptr_t)d->set_mode) ||
       !nexis_gpu_image_code_pointer(&R.image,BASE,(uintptr_t)d->shutdown) ||
       (d->poll && !nexis_gpu_image_code_pointer(&R.image,BASE,(uintptr_t)d->poll)))return false;
    for(unsigned n=0;n<sizeof(d->reserved2);n++)if(d->reserved2[n])return false;
    uintptr_t name=(uintptr_t)d->name;
    if(name<BASE || name-BASE>=R.image.writable_offset)return false;
    size_t available=R.image.writable_offset-(name-BASE);
    for(unsigned n=0;n<sizeof(R.name);n++){
        if(n>=available)return false;
        unsigned char c=d->name[n];if(!c)return n!=0;
        if(c<32 || c>=127 || n==sizeof(R.name)-1)return false;
        R.name[n]=(char)c;
    }
    return false;
}
static bool scanout(nexis_gpu_scanout *mode){
    memset(mode,0,sizeof(*mode));
    if(!R.driver.read_mode(R.driver.state,mode) || mode->reserved || !(mode->flags&NEXIS_GPU_SCANOUT_ACTIVE) ||
       mode->flags&~15u || ((mode->flags&NEXIS_GPU_SCANOUT_AUDIO) && !(mode->flags&NEXIS_GPU_SCANOUT_HDMI)) ||
       mode->framebuffer!=fb_front_base() || mode->pitch!=fb_get_pitch() || mode->format!=(fb_is_rgb()?0u:1u))return false;
    edid_timing t;memcpy(&t,&mode->timing,sizeof(t));
    return t.hactive==fb_get_width() && t.vactive==fb_get_height() && !(t.flags&~3u) &&
        t.hactive<t.hsync_start && t.hsync_start<t.hsync_end && t.hsync_end<=t.htotal && t.htotal<=65536 &&
        t.vactive<t.vsync_start && t.vsync_start<t.vsync_end && t.vsync_end<=t.vtotal && t.vtotal<=65536 &&
        t.clock_khz && t.clock_khz<=R.driver.max_pixel_khz && edid_refresh_millihz(&t)>=20000 && edid_refresh_millihz(&t)<=1000000;
}
bool gpu_runtime_load(const uint8_t *data,size_t bytes,pci_device_t *device){
    /* The caller (packages.c) has already verified the ECDSA signature of the
     * package that carries this module; here only structure and device match. */
    if(R.resident || R.calling || !device || !data || device->class_id!=3)return false;
    nexis_gpu_image image;if(!nexis_gpu_image_parse(data,bytes,device->vendor_id,device->device_id,&image))return false;
    const nexis_boot_info_t *boot=bootinfo_get();
    if(!boot || boot->gpu_vendor!=device->vendor_id || boot->gpu_device!=device->device_id || boot->gpu_bus!=device->bus ||
       boot->gpu_slot!=device->slot || boot->gpu_func!=device->func || !device_ready(device))return false;
    memset(&R,0,sizeof(R));R.image=image;R.device=*device;R.pages=image.memory_bytes/4096;
    R.physical=pmm_alloc_pages(R.pages);
    if(!R.physical)return false;
    if(R.physical>=(64ULL<<30) || R.pages*4096>(64ULL<<30)-R.physical){
        for(size_t n=0;n<R.pages;n++)pmm_free_page(R.physical+n*4096);
        memset(&R,0,sizeof(R));return false;
    }
    if(!vmm_protect_identity(R.physical,R.pages*4096,PAGE_WRITABLE|PAGE_NO_EXECUTE)){release(false);return false;}
    for(size_t n=0;n<R.pages;n++){
        if(!vmm_map_page(vmm_get_kernel_pml4(),BASE+n*4096,R.physical+n*4096,PAGE_WRITABLE|PAGE_NO_EXECUTE)){release(false);return false;}
        R.mapped++;
    }
    memset((void *)(uintptr_t)BASE,0,image.memory_bytes);memcpy((void *)(uintptr_t)BASE,data+64,image.image_bytes);
    for(size_t n=0;n<R.pages;n++){
        size_t offset=n*4096;uint64_t flags=offset<image.text_bytes?0:PAGE_NO_EXECUTE;
        if(offset>=image.writable_offset)flags|=PAGE_WRITABLE;
        if(!vmm_map_page(vmm_get_kernel_pml4(),BASE+offset,R.physical+offset,flags) ||
           !vmm_protect_identity(R.physical+offset,4096,(flags&PAGE_WRITABLE)|PAGE_NO_EXECUTE)){release(false);return false;}
    }
    /* Metadata includes large VRAM apertures. Only independently matched small
     * banks are mapped for register access; no large aperture is mapped here. */
    uint32_t raw[6];uint64_t base_ranges[6],byte_ranges[6];read_bars(device,raw);firmware_ranges(boot,base_ranges,byte_ranges);
    for(unsigned n=0;n<6;n++){
        if(!gpu_pci_resource_decode(raw,base_ranges,byte_ranges,n,&R.resources[n]))continue;
        uint64_t base=R.resources[n].base,size=R.resources[n].bytes;
        if(base<(64ULL<<30) && size<=(64ULL<<30)-base && size<=16*1024*1024 && vmm_map_mmio(base,(size_t)size)){
            R.bar[n]=base;R.bar_bytes[n]=size;
            R.resources[n].flags|=NEXIS_GPU_RESOURCE_REGISTERS;
        }
    }
    R.services=(nexis_gpu_services){2,image.services_bytes,fb_get_width(),fb_get_height(),fb_get_pitch(),fb_is_rgb()?0:1,
        device->vendor_id,device->device_id,device->bus,device->slot,device->func,0,fb_front_base(),fb_front_size(),
        boot->gpu_rom_size && boot->gpu_rom_size<=1024*1024 && boot->gpu_rom_addr && boot->gpu_rom_addr<(1ULL<<32) &&
        boot->gpu_rom_size<=(1ULL<<32)-boot->gpu_rom_addr?(void *)(uintptr_t)boot->gpu_rom_addr:NULL,
        boot->gpu_rom_size,0,&R,read32,write32,time_us,delay_us,resource};
    if(!R.services.rom)R.services.rom_bytes=0;
    R.driver.abi=2;R.driver.size=sizeof(R.driver);R.calling=true;
    nexis_gpu_entry_v2 entry=(void *)(uintptr_t)(BASE+image.entry);
    int result=entry(&R.services,&R.driver);R.calling=false;
    if(result || !validate_driver()){kprintf("[GPU] Retained module initialization rejected (%d)\n",result);release(false);return false;}
    const edid_monitor *monitor=display_monitor_get();nexis_gpu_scanout actual;
    /* Remember the firmware mode so a trial can be reverted. */
    R.have_initial=scanout(&R.initial);
    edid_timing desired={0};edid_link_limits limits={R.driver.max_pixel_khz,R.driver.max_tmds_khz,R.driver.hdmi,R.driver.scdc};
    const edid_timing *chosen=monitor?edid_select_rgb8(monitor,fb_get_width(),fb_get_height(),&limits):NULL;
    if(chosen && !R.have_initial){
        /* Never change a mode that cannot be restored. */
        kprintf("[GPU] Firmware mode could not be read back; native mode change refused\n");release(true);return false;
    }
    if(chosen){
        desired=*chosen;
        desired.flags&=3u;nexis_gpu_timing target;memcpy(&target,&desired,sizeof(target));
        if(!R.driver.set_mode(R.driver.state,&target) || !scanout(&actual) || !nexis_gpu_mode_readback_matches(&actual,&target)){
            kprintf("[GPU] Native mode rejected or hardware readback differs; restoring prior output\n");release(true);return false;
        }
    }else if(!scanout(&actual)){release(true);return false;}
    if(!fb_native_activate(actual.framebuffer,actual.pitch,actual.format,R.name)){release(true);return false;}
    memcpy(&desired,&actual.timing,sizeof(desired));display_set_native_mode(&desired);
    R.resident=R.active=true;R.last_poll_us=time_us(&R);
    kprintf("[GPU] Retained native module: %s, hardware mode %u.%03u Hz; HDMI packets %u\n",R.name,
        edid_refresh_millihz(&desired)/1000,edid_refresh_millihz(&desired)%1000,(actual.flags&NEXIS_GPU_SCANOUT_AUDIO)!=0);
    return true;
}
void gpu_runtime_poll(void){
    if(!R.resident || R.calling || time_us(&R)-R.last_poll_us<100000)return;
    R.calling=true;R.last_poll_us=time_us(&R);if(R.driver.poll)R.driver.poll(R.driver.state);
    nexis_gpu_scanout mode;R.active=scanout(&mode);
    if(R.active){edid_timing timing;memcpy(&timing,&mode.timing,sizeof(timing));display_set_native_mode(&timing);}
    else display_clear_native_mode();
    R.calling=false;
}
bool gpu_runtime_active(void){return R.active;}
bool gpu_runtime_revert(void){
    /* Put the firmware mode back, then unload the module. The framebuffer
     * address never changed, so the desktop keeps drawing into the same memory. */
    if(!R.resident || R.calling)return false;
    bool ok=false;nexis_gpu_scanout now;
    if(R.have_initial){
        R.calling=true;
        ok=R.driver.set_mode(R.driver.state,&R.initial.timing) && scanout(&now) &&
           nexis_gpu_mode_readback_matches(&now,&R.initial.timing);
        R.calling=false;
    }
    release(true);fb_native_deactivate();
    return ok;
}
