/* Link-only fixture proving native support code builds in NDRV2 PIC form.
 * It is deliberately not a card driver: entry always rejects activation.
 * Never put this artifact in the download catalog or embed it in an ISO. */
#include "../../tools/gpu-driver/amd/dcn302_clock.h"
#include "../../tools/gpu-driver/amd/dcn302_ddc.h"
#include "../../tools/gpu-driver/amd/dcn302_hdmi.h"
#include "../../tools/gpu-driver/amd/hdmi_scdc.h"
#include "../../tools/gpu-driver/amd/atom_vm.h"
#include "../../tools/gpu-driver/amd/atom_display_commands.h"
#include "../../tools/gpu-driver/amd/dcn302_route.h"
#include "../../tools/gpu-driver/amd/dcn302_surface.h"
static dcn302_clock_measurement clock;
static dcn302_ddc ddc;
static dcn302_hdmi_transaction hdmi;
static hdmi_scdc_snapshot sink;
static atom_vm vm;
static uint8_t scratch[65536];
static volatile uintptr_t link_symbols[21];
static atom_board board;
static dcn302_route route;
static dcn302_surface surface;
static bool read_reg(void *c,uint32_t o,uint32_t *v){const nexis_gpu_services *s=c;return s->read32(s->service_context,5,o,v);}
static bool write_reg(void *c,uint32_t o,uint32_t v){const nexis_gpu_services *s=c;return s->write32(s->service_context,5,o,v);}
static bool delay(void *c,uint32_t us){const nexis_gpu_services *s=c;return s->delay_us(s->service_context,us);}
static uint64_t now(void *c){const nexis_gpu_services *s=c;return s->time_us(s->service_context);}
static bool scdc_read(void *c,uint8_t a,uint8_t o,uint8_t *v){return dcn302_ddc_read_byte(c,a,o,v);}
static bool scdc_write(void *c,uint8_t a,uint8_t o,uint8_t v){return dcn302_ddc_write_byte(c,a,o,v);}
static bool scdc_delay(void *c,uint32_t us){dcn302_ddc *d=c;return d->io.delay_us(d->io.context,us);}
static bool atom_read(void *c,enum atom_vm_space space,uint32_t o,uint32_t *v){return space==ATOM_VM_MMIO && o<262144 && read_reg(c,o*4,v);}
static bool atom_write(void *c,enum atom_vm_space space,uint32_t o,uint32_t v){return space==ATOM_VM_MMIO && o<262144 && write_reg(c,o*4,v);}
int NEXIS_GPU_CALL driver_init_v2(const nexis_gpu_services *services,nexis_gpu_instance *instance){
    (void)instance;
    /* The linker must retain real bodies, not optimize known-NULL helper calls
     * into constant error returns. ABI2 entry never takes this ABI0 branch. */
    if(services && services->abi==0){
        link_symbols[0]=(uintptr_t)dcn302_clock_measure;link_symbols[1]=(uintptr_t)dcn302_otg_program_disabled;
        link_symbols[2]=(uintptr_t)dcn302_otg_enable;link_symbols[3]=(uintptr_t)dcn302_otg_disable;
        link_symbols[4]=(uintptr_t)dcn302_ddc_transfer;link_symbols[5]=(uintptr_t)dcn302_hdmi_prepare;
        link_symbols[6]=(uintptr_t)dcn302_hdmi_commit;link_symbols[7]=(uintptr_t)dcn302_hdmi_restore;
        link_symbols[8]=(uintptr_t)hdmi_scdc_configure;link_symbols[9]=(uintptr_t)hdmi_scdc_verify_link;
        link_symbols[10]=(uintptr_t)atom_vm_execute;link_symbols[11]=(uintptr_t)atom_vm_init;
        link_symbols[12]=(uintptr_t)atom_board_open;link_symbols[13]=(uintptr_t)atom_display_execute;
        link_symbols[14]=(uintptr_t)dcn302_route_find;link_symbols[15]=(uintptr_t)dcn302_surface_bind;
        link_symbols[16]=(uintptr_t)atom_hdmi_pixel_parameters;link_symbols[17]=(uintptr_t)atom_hdmi_stream_parameters;
        link_symbols[18]=(uintptr_t)atom_hdmi_transmitter_parameters;link_symbols[19]=(uintptr_t)atom_hdmi_transmitter_v7_parameters;
        link_symbols[20]=(uintptr_t)dcn302_route_connected;
        dcn302_io io={(void *)services,read_reg,write_reg,delay};
        (void)dcn302_otg_disable(&io,0);
        nexis_gpu_timing target={558100,1920,1968,2032,2080,1080,1083,1088,1118,3};dcn302_sync sync={10,100,80,40,false};
        (void)dcn302_otg_program_disabled(&io,0,&target,&sync);
        (void)dcn302_otg_enable(&io,0);
        (void)dcn302_clock_measure(&io,0,now,16,&clock);
        (void)dcn302_ddc_init(&ddc,&io,0,27000);
        /* A fully constant aggregate of pointers can become a relocated data
         * template even under -fPIE. Explicit volatile stores use RIP-relative
         * address generation and require no runtime relocation. */
        volatile hdmi_scdc_io sink_io;sink_io.context=&ddc;sink_io.read_byte=scdc_read;sink_io.write_byte=scdc_write;sink_io.delay_us=scdc_delay;
        (void)hdmi_scdc_configure((const hdmi_scdc_io *)&sink_io,558100,false,&sink);
        (void)hdmi_scdc_verify_link((const hdmi_scdc_io *)&sink_io,558100,false);
        (void)hdmi_scdc_restore((const hdmi_scdc_io *)&sink_io,&sink);
        (void)dcn302_hdmi_prepare(&io,0,0,558100,3,true,0,&hdmi);
        (void)dcn302_hdmi_commit(&io,&hdmi);(void)dcn302_hdmi_restore(&io,&hdmi);
        atom_rom rom;
        if(atom_rom_open(services->rom,services->rom_bytes,services->vendor,services->device,&rom)){
            if(atom_board_open(&rom,&board)==ATOM_BOARD_OK && dcn302_route_find(&io,&board,services->width,services->height,&route)==DCN302_ROUTE_OK){
                /* This deliberate ABI0-only link fixture never asserts an
                 * unverified VRAM BAR resource and never activates. */
                (void)dcn302_surface_bind(&io,&route,0,0,services->framebuffer,services->framebuffer_bytes,services->pitch,services->format,&surface);
            }
            atom_vm_io a={(void *)services,atom_read,atom_write,delay,now};
            if(atom_vm_init(&vm,&rom,&a,scratch,sizeof(scratch))){
                uint32_t parameters[4];
                if(board.count && atom_hdmi_pixel_parameters((uint8_t *)parameters,&board.paths[0],0,558100))
                    (void)atom_display_execute(&vm,ATOM_DISPLAY_PIXEL_CLOCK,(uint8_t *)parameters,sizeof(parameters));
            }
        }
    }
    return 99;
}
