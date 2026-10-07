#include "../../../tools/gpu-driver/include/nexis_gpu_v2.h"
static bool valid(const nexis_gpu_timing *t){
    return t && t->pixel_khz && t->pixel_khz<=4000000 && !(t->flags&~3u) &&
        t->hactive && t->hactive<t->hsync_start && t->hsync_start<t->hsync_end && t->hsync_end<=t->htotal && t->htotal<=65536 &&
        t->vactive && t->vactive<t->vsync_start && t->vsync_start<t->vsync_end && t->vsync_end<=t->vtotal && t->vtotal<=65536;
}
bool nexis_gpu_mode_readback_matches(const nexis_gpu_scanout *actual,const nexis_gpu_timing *target){
    if(!actual || actual->reserved || !(actual->flags&NEXIS_GPU_SCANOUT_ACTIVE) || actual->flags&~15u ||
       ((actual->flags&NEXIS_GPU_SCANOUT_AUDIO) && !(actual->flags&NEXIS_GPU_SCANOUT_HDMI)) ||
       !valid(target) || !valid(&actual->timing))return false;
    const nexis_gpu_timing *t=&actual->timing;
    if(t->hactive!=target->hactive || t->hsync_start!=target->hsync_start || t->hsync_end!=target->hsync_end || t->htotal!=target->htotal ||
       t->vactive!=target->vactive || t->vsync_start!=target->vsync_start || t->vsync_end!=target->vsync_end || t->vtotal!=target->vtotal || t->flags!=target->flags)return false;
    if(!(actual->flags&NEXIS_GPU_SCANOUT_CLOCK_MEASURED))return t->pixel_khz==target->pixel_khz;
    uint64_t delta=t->pixel_khz>target->pixel_khz?t->pixel_khz-target->pixel_khz:target->pixel_khz-t->pixel_khz;
    return delta*1000000<=(uint64_t)target->pixel_khz*1000;
}
