/* Actual retained native RX6600 adapter under construction. Not a catalog
 * package until mode-changing clock/PHY/bandwidth/HDMI transactions are done. */
#include "rx6600.h"
#include <string.h>
static rx6600_state state;
static bool NEXIS_GPU_CALL read_mode(void *s,nexis_gpu_scanout *out){return rx6600_read_mode(s,out);}
static bool NEXIS_GPU_CALL set_mode(void *s,const nexis_gpu_timing *t){return rx6600_set_mode(s,t);}
static void NEXIS_GPU_CALL poll(void *s){rx6600_poll(s);}
static void NEXIS_GPU_CALL shutdown(void *s){rx6600_shutdown(s);}
int NEXIS_GPU_CALL driver_init_v2(const nexis_gpu_services *services,nexis_gpu_instance *instance){
    if(!instance)return RX6600_INPUT;
    memset(instance,0,sizeof(*instance));
    enum rx6600_error error=rx6600_probe(&state,services);
    if(error){rx6600_shutdown(&state);return error;}
    volatile nexis_gpu_instance *out=instance;
    out->abi=2;out->size=sizeof(*instance);out->state=&state;out->state_bytes=sizeof(state);
    out->name="AMD Radeon RX6600 native backend (development)";
    out->max_pixel_khz=state.route.max_tmds_khz;out->max_tmds_khz=state.route.max_tmds_khz;
    out->hdmi=true;out->scdc=true;
    out->read_mode=read_mode;out->set_mode=set_mode;out->poll=poll;out->shutdown=shutdown;
    return 0;
}
