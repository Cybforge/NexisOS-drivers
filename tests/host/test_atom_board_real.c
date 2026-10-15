/* The video BIOS tables of the first real RX 6600 (hex dump of the 2026-10-15 run), fed through the board parser. */
#include "../../tools/gpu-driver/amd/atom_board.h"
#include "../../tools/gpu-driver/amd/atom_memory.h"
#include "real_rx6600_vbios.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t bytes[8192];
static void p16(unsigned at,unsigned value){bytes[at]=(uint8_t)value;bytes[at+1]=(uint8_t)(value>>8);}
int main(void){
    atom_rom rom={bytes,sizeof(bytes),80,100,true};atom_board b;
    p16(80,18);p16(100,64);bytes[102]=2;bytes[103]=1;
    p16(104+12*2,200);p16(104+22*2,400);p16(104+27*2,1600);p16(104+28*2,2000);
    memcpy(bytes+200,gpio_pin_lut,sizeof(gpio_pin_lut));
    memcpy(bytes+400,object_info,sizeof(object_info));
    memcpy(bytes+1600,dce_info_head,sizeof(dce_info_head));   /* the remaining 148 bytes of dce_info were not captured */
    memcpy(bytes+2000,vram_info_head,sizeof(vram_info_head));   /* table size field 4492; only its first 96 bytes were captured */
    atom_table t;
    CHECK(atom_rom_table(&rom,false,12,&t) && t.size==108 && t.format==2 && t.revision==1);
    CHECK(atom_rom_table(&rom,false,22,&t) && t.size==136 && t.format==1 && t.revision==4);
    CHECK(atom_rom_table(&rom,false,27,&t) && t.size==212 && t.format==4 && t.revision==4);
    enum atom_board_error e=atom_board_open(&rom,&b);
    printf("atom_board_open -> %d, paths %u, reference %u kHz\n",(int)e,(unsigned)b.count,(unsigned)b.reference_khz);
    for(unsigned i=0;i<b.count;i++){const atom_board_path *p=&b.paths[i];
        printf(" path %u: connector %04x kind %02x encoder %04x phy %u internal %d ddc %d (gpio %x shift %u line %u hw %d) hpd %d (gpio %x shift %u state %u) caps %x\n",
            i,p->connector,p->kind,p->encoder,p->phy,p->internal_phy,p->has_ddc,(unsigned)p->ddc_gpio.index,p->ddc_gpio.shift,p->ddc_line,p->ddc_hardware,p->has_hpd,(unsigned)p->hpd_gpio.index,p->hpd_gpio.shift,p->hpd_state,(unsigned)p->encoder_caps);}
    atom_memory m;enum atom_memory_error me=atom_memory_open(&rom,&m);
    printf("atom_memory_open -> %d: %u MB, %u channels x %u bytes, type %02x, modules %u\n",(int)me,(unsigned)m.memory_mb,m.channels,m.channel_bytes,m.type,m.modules);
    return e==ATOM_BOARD_OK && me==ATOM_MEMORY_OK?0:1;
}
