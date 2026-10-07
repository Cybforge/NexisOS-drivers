#include "../../tools/gpu-driver/include/nexis_gpu_v2.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases;
int main(void){
    nexis_gpu_timing target={558100,1920,1968,2032,2080,1080,1083,1088,1118,3};
    nexis_gpu_scanout a={0};a.timing=target;a.flags=NEXIS_GPU_SCANOUT_ACTIVE;
    CHECK(nexis_gpu_mode_readback_matches(&a,&target));cases++;
    a.timing.pixel_khz=558101;CHECK(!nexis_gpu_mode_readback_matches(&a,&target));cases++;
    a.flags|=NEXIS_GPU_SCANOUT_CLOCK_MEASURED;CHECK(nexis_gpu_mode_readback_matches(&a,&target));cases++;
    for(unsigned pixel=25000;pixel<=4000000;pixel+=24997){
        target.pixel_khz=pixel;a.timing=target;a.flags=9;
        unsigned allowed=pixel/1000;
        a.timing.pixel_khz=pixel-allowed;CHECK(nexis_gpu_mode_readback_matches(&a,&target));cases++;
        a.timing.pixel_khz=pixel-allowed-1;CHECK(!nexis_gpu_mode_readback_matches(&a,&target));cases++;
        a.timing.pixel_khz=pixel+allowed;
        CHECK(nexis_gpu_mode_readback_matches(&a,&target)==(a.timing.pixel_khz<=4000000));cases++;
        a.timing.pixel_khz=pixel+allowed+1;CHECK(!nexis_gpu_mode_readback_matches(&a,&target));cases++;
    }
    target.pixel_khz=558100;
    for(unsigned field=0;field<9;field++){
        a.timing=target;a.flags=9;
        switch(field){case 0:a.timing.hactive--;break;case 1:a.timing.hsync_start++;break;case 2:a.timing.hsync_end++;break;
         case 3:a.timing.htotal++;break;case 4:a.timing.vactive--;break;case 5:a.timing.vsync_start++;break;
         case 6:a.timing.vsync_end++;break;case 7:a.timing.vtotal++;break;case 8:a.timing.flags^=1;break;}
        CHECK(!nexis_gpu_mode_readback_matches(&a,&target));cases++;
    }
    a.timing=target;
    for(unsigned flags=0;flags<64;flags++){
        a.flags=flags;bool allowed=(flags&1) && !(flags&~15u) && (!(flags&4) || (flags&2));
        CHECK(nexis_gpu_mode_readback_matches(&a,&target)==allowed);cases++;
    }
    a.flags=9;a.reserved=1;CHECK(!nexis_gpu_mode_readback_matches(&a,&target));cases++;
    a.reserved=0;a.timing.pixel_khz=0;CHECK(!nexis_gpu_mode_readback_matches(&a,&target));cases++;
    a.timing=target;target.htotal=target.hsync_end;target.vtotal=target.vsync_end;a.timing=target;
    CHECK(nexis_gpu_mode_readback_matches(&a,&target));cases++;
    target.pixel_khz=UINT32_MAX;a.timing=target;CHECK(!nexis_gpu_mode_readback_matches(&a,&target));cases++;
    CHECK(!nexis_gpu_mode_readback_matches(NULL,&target) && !nexis_gpu_mode_readback_matches(&a,NULL));cases++;
    printf("{\"passed\":true,\"cases\":%u,\"actual_measured_clock_policy\":true,\"exact_geometry_required\":true,\"physical_hardware_verified\":false}\n",cases);return 0;
}
