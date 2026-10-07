/* Original MIT licensed bounded decoder of AMD ATOM table layouts.
 * Format reference: Linux v6.12 drivers/gpu/drm/amd/include/atomfirmware.h
 * Hardware programming/interpreter integration is separate and incomplete. */
#include "atom_tables.h"
#include <string.h>
static unsigned u16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static bool extent(size_t size,size_t off,size_t length){return off<=size && length<=size-off;}
static bool master(const uint8_t *image,size_t size,unsigned off){
    return off && extent(size,off,4) && u16(image+off)>=4 && !(u16(image+off)&1) && extent(size,off,u16(image+off));
}
bool atom_rom_open(const uint8_t *bytes,size_t size,uint16_t vendor,uint16_t device,atom_rom *out){
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!bytes || size<512 || size>1024*1024 || vendor!=0x1002)return false;
    for(size_t off=0,guard=0;extent(size,off,0x4a) && guard<256;guard++){
        const uint8_t *image=bytes+off;size_t remaining=size-off;
        if(image[0]!=0x55 || image[1]!=0xaa)return false;
        unsigned pcir=u16(image+0x18);
        if(!extent(remaining,pcir,24) || memcmp(image+pcir,"PCIR",4))return false;
        unsigned pci_length=u16(image+pcir+10),length=u16(image+pcir+16)*512;
        if(pci_length<24 || !length || length>remaining || !extent(length,pcir,pci_length))return false;
        unsigned atom=u16(image+0x48);
        if(image[pcir+20]==0 && u16(image+pcir+4)==vendor && u16(image+pcir+6)==device &&
           extent(length,atom,36) && (!memcmp(image+atom+4,"ATOM",4) || !memcmp(image+atom+4,"MOTA",4))){
            unsigned header_size=u16(image+atom);
            if(header_size<36 || !extent(length,atom,header_size))return false;
            unsigned sum=0;for(unsigned i=0;i<length;i++)sum+=image[i];if(sum&255)return false;
            unsigned commands=u16(image+atom+30),data=u16(image+atom+32);
            if(!master(image,length,commands) || !master(image,length,data))return false;
            out->image=image;out->size=length;out->commands=(uint16_t)commands;out->data=(uint16_t)data;return true;
        }
        if(image[pcir+21]&128)return false;
        off+=length;
    }
    return false;
}
bool atom_rom_table(const atom_rom *rom,bool command,unsigned index,atom_table *out){
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!rom || !rom->image)return false;
    unsigned master_offset=command?rom->commands:rom->data;
    if(!master(rom->image,rom->size,master_offset))return false;
    unsigned n=(u16(rom->image+master_offset)-4)/2;
    if(index>=n)return false;
    unsigned off=u16(rom->image+master_offset+4+index*2),minimum=command?6:4;
    if(!off || !extent(rom->size,off,minimum))return false;
    unsigned size=u16(rom->image+off);
    if(size<minimum || !extent(rom->size,off,size))return false;
    out->bytes=rom->image+off;out->size=(uint16_t)size;out->format=out->bytes[2];out->revision=out->bytes[3];
    if(command){out->workspace=out->bytes[4];out->parameters=out->bytes[5]&127;}
    return true;
}
bool atom_pixel_clock_v7(uint8_t out[16],uint32_t pixel_khz,uint8_t crtc,uint8_t pll,uint8_t encoder,uint8_t mode,uint8_t flags){
    if(!out || !pixel_khz || pixel_khz>4000000 || crtc>5)return false;
    memset(out,0,16);uint32_t units=pixel_khz*10;
    for(unsigned i=0;i<4;i++)out[i]=(uint8_t)(units>>(i*8));
    out[4]=pll;out[5]=encoder;out[6]=mode;out[7]=flags;out[8]=crtc;
    return true;
}
