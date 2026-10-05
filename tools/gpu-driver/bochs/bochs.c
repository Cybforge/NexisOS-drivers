/* Native QEMU standard-VGA (1234:1111) DISPI modesetting.
 * Register interface: https://www.qemu.org/docs/master/specs/standard-vga.html
 * The existing desktop dimensions/pitch are retained during live takeover.
 * This adapter has no HDMI audio, hardware rendering or refresh-clock control. */
#include "../include/nexis_gpu.h"
static void restore(volatile uint16_t *r,const uint16_t *old){
 r[4]=0;r[1]=old[1];r[2]=old[2];r[3]=old[3];r[5]=old[5];
 /* Preserve visible pixels and restore virtual geometry after ENABLE. */
 r[4]=old[4]|0x80;r[6]=old[6];r[8]=old[8];r[9]=old[9];
}
int driver_init(const nexis_gpu_context *c){
 if(!c || c->abi!=NEXIS_GPU_ABI || !c->registers || !c->activate || !c->framebuffer ||
    !c->width || !c->height || c->width>4096 || c->height>2160 || c->pitch<c->width || c->pitch>8192 ||
    (uint64_t)c->pitch*c->height*4>c->framebuffer_bytes)return -1;
 volatile uint16_t *r=(volatile uint16_t *)(uintptr_t)(c->registers+0x500);
 if(r[0]<0xb0c2 || r[0]>0xb0c5)return -2;
 uint16_t old[10];for(unsigned i=0;i<10;i++)old[i]=r[i];
 r[4]=0;r[1]=c->width;r[2]=c->height;r[3]=32;r[5]=0;r[4]=0xc1;
 /* ENABLE resets virtual geometry; apply pitch/offsets afterwards. */
 r[6]=c->pitch;r[8]=0;r[9]=0;
 int error=r[1]!=c->width?-31:r[2]!=c->height?-32:r[3]!=32?-33:r[6]!=c->pitch?-34:(r[4]&0x41)!=0x41?-35:0;
 if(!error && c->activate(c->framebuffer,c->pitch,NEXIS_GPU_BGR,"Bochs native framebuffer"))error=-36;
 if(error){restore(r,old);return error;}
 return 0;
}
