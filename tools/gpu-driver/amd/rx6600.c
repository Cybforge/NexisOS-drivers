#include "rx6600.h"
#include <string.h>
static bool read_reg(void *context,uint32_t offset,uint32_t *value){
    rx6600_state *s=context;return s->services->read32(s->services->service_context,5,offset,value);
}
static bool delay(void *context,uint32_t us){rx6600_state *s=context;return s->services->delay_us(s->services->service_context,us);}
static uint64_t now(void *context){rx6600_state *s=context;return s->services->time_us(s->services->service_context);}
static enum rx6600_error fail(rx6600_state *s,enum rx6600_error error){s->ready=false;s->error=error;return error;}
static bool resources(rx6600_state *s){
    nexis_gpu_resource v,r;const nexis_gpu_services *k=s->services;
    return k->resource(k->service_context,0,&v) && k->resource(k->service_context,5,&r) &&
        !memcmp(&s->vram,&v,sizeof(v)) && !memcmp(&s->registers,&r,sizeof(r));
}
static bool fixed_rate(rx6600_state *s){
    const enum dcn302_register registers[]={DCN302_R_V_CONTROL,DCN302_R_V_MIN,DCN302_R_V_MAX};
    for(unsigned n=0;n<3;n++){
        uint32_t value;
        if(!s->io.read(s->io.context,dcn302_register_bytes[s->route.otg][registers[n]],&value) || value!=s->fixed_rate[n])return false;
    }
    return true;
}
static enum rx6600_error prove(rx6600_state *s){
    const nexis_gpu_services *k=s->services;dcn302_route route;dcn302_surface surface;
    if(!resources(s))return RX6600_RESOURCE;
    if(!fixed_rate(s))return RX6600_CLOCK;
    if(dcn302_route_find(&s->io,&s->board,k->width,k->height,&route)!=DCN302_ROUTE_OK)return RX6600_ROUTE;
    /* Identity and complete geometry, including OPP routing, remain stable. */
    if(memcmp(&s->route,&route,sizeof(route)))return RX6600_CHANGED;
    if(dcn302_surface_bind(&s->io,&route,s->vram.base,s->vram.bytes,k->framebuffer,k->framebuffer_bytes,k->pitch,k->format,&surface)!=DCN302_SURFACE_OK)return RX6600_SURFACE;
    if(memcmp(&s->surface,&surface,sizeof(surface)))return RX6600_CHANGED;
    return RX6600_OK;
}
enum rx6600_error rx6600_probe(rx6600_state *s,const nexis_gpu_services *k){
    if(!s)return RX6600_INPUT;
    memset(s,0,sizeof(*s));
    if(!k || k->abi!=2 || k->size!=NEXIS_GPU_SERVICES_RESOURCE_BYTES || k->vendor!=0x1002 || k->device!=0x73ff ||
       !k->width || !k->height || k->pitch<k->width || k->format>1 || k->reserved || k->reserved2 ||
       !k->framebuffer || !k->framebuffer_bytes || !k->rom || !k->rom_bytes ||
       !k->read32 || !k->time_us || !k->delay_us || !k->resource)return fail(s,RX6600_INPUT);
    s->services=k;
    /* Volatile individual pointer stores avoid absolute pointer templates in
     * freestanding PIE; no runtime relocations/imports are available. */
    volatile dcn302_io *io=&s->io;io->context=s;io->read=read_reg;io->write=NULL;io->delay_us=delay;
    if(!k->resource(k->service_context,0,&s->vram) || !k->resource(k->service_context,5,&s->registers) ||
       s->vram.reserved || s->registers.reserved || s->vram.flags!=(NEXIS_GPU_RESOURCE_MEMORY|NEXIS_GPU_RESOURCE_64BIT|NEXIS_GPU_RESOURCE_PREFETCH) ||
       !(s->registers.flags&NEXIS_GPU_RESOURCE_MEMORY) || !(s->registers.flags&NEXIS_GPU_RESOURCE_REGISTERS) ||
       (s->registers.flags&~15u) || s->registers.bytes<1024*1024)return fail(s,RX6600_RESOURCE);
    atom_rom rom;
    if(!atom_rom_open(k->rom,k->rom_bytes,k->vendor,k->device,&rom))return fail(s,RX6600_ROM);
    if(atom_board_open(&rom,&s->board)!=ATOM_BOARD_OK)return fail(s,RX6600_BOARD);
    if(dcn302_route_find(&s->io,&s->board,k->width,k->height,&s->route)!=DCN302_ROUTE_OK)return fail(s,RX6600_ROUTE);
    if(dcn302_surface_bind(&s->io,&s->route,s->vram.base,s->vram.bytes,k->framebuffer,k->framebuffer_bytes,k->pitch,k->format,&s->surface)!=DCN302_SURFACE_OK)return fail(s,RX6600_SURFACE);
    dcn302_snapshot fixed;
    if(!dcn302_otg_snapshot(&s->io,s->route.otg,&fixed))return fail(s,RX6600_CLOCK);
    s->fixed_rate[0]=fixed.registers[DCN302_R_V_CONTROL];s->fixed_rate[1]=fixed.registers[DCN302_R_V_MIN];s->fixed_rate[2]=fixed.registers[DCN302_R_V_MAX];
    if(dcn302_clock_measure(&s->io,s->route.otg,now,16,&s->clock)!=DCN302_OK ||
       s->clock.pixel_khz>s->route.max_tmds_khz)return fail(s,RX6600_CLOCK);
    enum rx6600_error error=prove(s);if(error)return fail(s,error);
    s->sampled_us=now(s);
    if(!dcn302_otg_frame_count(&s->io,s->route.otg,&s->sampled_frame))return fail(s,RX6600_CLOCK);
    s->ready=true;s->error=RX6600_OK;return RX6600_OK;
}
static bool alive(rx6600_state *s){
    uint64_t time=now(s);uint32_t frame;
    if(time<s->sampled_us || time-s->sampled_us>30000000 || !dcn302_otg_frame_count(&s->io,s->route.otg,&frame))return false;
    uint64_t elapsed=time-s->sampled_us;
    if(elapsed<100000)return true;
    uint64_t expected=(elapsed*s->clock.refresh_millihz+500000000)/1000000000;
    uint32_t delta=(frame-s->sampled_frame)&DCN302_FRAME_COUNT_MASK;
    uint64_t tolerance=2+(expected+999)/1000;
    if(!delta || delta+tolerance<expected || (uint64_t)delta>expected+tolerance)return false;
    if(elapsed>=500000){s->sampled_us=time;s->sampled_frame=frame;}
    return true;
}
bool rx6600_read_mode(rx6600_state *s,nexis_gpu_scanout *out){
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!s || !s->ready || s->busy)return false;
    s->busy=true;enum rx6600_error error=prove(s);
    if(!error && !alive(s))error=RX6600_CLOCK;
    if(error){fail(s,error);s->busy=false;return false;}
    out->timing=s->route.shape;out->timing.pixel_khz=s->clock.pixel_khz;
    out->framebuffer=s->surface.cpu_address;out->pitch=s->surface.pitch;out->format=s->surface.format;
    out->flags=NEXIS_GPU_SCANOUT_ACTIVE|NEXIS_GPU_SCANOUT_HDMI|NEXIS_GPU_SCANOUT_CLOCK_MEASURED;
    s->busy=false;return true;
}
bool rx6600_set_mode(rx6600_state *s,const nexis_gpu_timing *t){
    nexis_gpu_scanout current;
    if(!t || !rx6600_read_mode(s,&current))return false;
    nexis_gpu_timing shape=*t;shape.pixel_khz=0;
    uint32_t clock=current.timing.pixel_khz;
    uint32_t difference=clock>t->pixel_khz?clock-t->pixel_khz:t->pixel_khz-clock;
    if(!t->pixel_khz || memcmp(&shape,&s->route.shape,sizeof(shape)) || (uint64_t)difference*1000000>(uint64_t)t->pixel_khz*1000){
        s->error=RX6600_MODESET_PENDING;return false;
    }
    /* Genuine no-op for the already running, measured mode only. */
    s->error=RX6600_OK;return true;
}
void rx6600_poll(rx6600_state *s){
    if(!s || !s->ready || s->busy)return;
    bool connected=false;s->busy=true;
    if(dcn302_route_connected(&s->io,&s->board.paths[s->route.path],s->route.hpd,&connected)!=DCN302_ROUTE_OK || !connected)
        fail(s,RX6600_ROUTE);
    else if(!fixed_rate(s) || !alive(s))fail(s,RX6600_CLOCK);
    s->busy=false;
}
void rx6600_shutdown(rx6600_state *s){if(s)memset(s,0,sizeof(*s));}
