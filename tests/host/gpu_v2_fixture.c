/* Loader test fixture only. Never a catalog entry or a physical GPU driver. */
#include "../../tools/gpu-driver/include/nexis_gpu_v2.h"
static struct { const nexis_gpu_services *services;uint32_t polls,last; } state;
static bool mode(void *p,nexis_gpu_scanout *out){(void)p;(void)out;return false;}
static bool set(void *p,const nexis_gpu_timing *t){(void)p;(void)t;return false;}
static void stop(void *p){(void)p;state.services=NULL;}
static void poll(void *p){
    (void)p;uint32_t value=0;
    if(state.services->read32(state.services->service_context,5,4,&value)){state.last=value;state.polls++;}
}
int driver_init_v2(const nexis_gpu_services *services,nexis_gpu_instance *out){
    if(!services || services->abi!=2 || services->size!=sizeof(*services) || !out)return -1;
    state.services=services;out->abi=2;out->size=sizeof(*out);out->state=&state;out->state_bytes=sizeof(state);
    out->name="Retained loader test fixture";out->read_mode=mode;out->set_mode=set;out->poll=poll;out->shutdown=stop;return 0;
}
