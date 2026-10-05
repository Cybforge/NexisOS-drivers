#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../../tools/gpu-driver/include/nexis_gpu.h"
static unsigned calls;static int reject;
static int attach(uint64_t fb,uint32_t pitch,uint32_t format,const char *name){assert(fb==0xc0000000 && pitch==1280 && format==1);assert(!strcmp(name,"Bochs native framebuffer"));calls++;return reject;}
int main(void){
 uint16_t mmio[2048]={0},old[10];volatile uint16_t *r=mmio+0x500/2;
 nexis_gpu_context c={1,1280,720,1280,1,0,0xc0000000,16*1024*1024,(uintptr_t)mmio,attach};
 assert(driver_init(NULL)==-1);c.abi=2;assert(driver_init(&c)==-1);c.abi=1;
 assert(driver_init(&c)==-2);assert(!calls);r[0]=0xb0c5;r[1]=1280;r[2]=720;r[3]=32;r[4]=0x41;r[6]=1280;r[7]=4096;
 memcpy(old,(void*)r,sizeof(old));reject=1;assert(driver_init(&c)==-36);old[4]|=0x80;assert(!memcmp(old,(void*)r,sizeof(old)));
 reject=0;assert(!driver_init(&c));assert(r[1]==1280 && r[2]==720 && r[3]==32 && r[4]==0xc1 && r[6]==1280 && calls==2);
 c.framebuffer_bytes=1024;assert(driver_init(&c)==-1);assert(calls==2);
 puts("PASS: native Bochs register takeover, rejection and rollback model");return 0;
}
