#include "../../tools/gpu-driver/amd/atom_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases,checks;
static uint8_t image[4096];
static void p16(uint8_t *p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void p32(uint8_t *p,uint32_t n){for(unsigned b=0;b<4;b++)p[b]=(uint8_t)(n>>(b*8));}
static atom_rom fixture(unsigned revision,unsigned modules){
    memset(image,0,sizeof(image));p16(image+100,64);image[102]=2;image[103]=1;p16(image+104+28*2,200);
    unsigned size=revision==3?52:revision==4?60:84;p16(image+200,24+size*modules);image[202]=2;image[203]=(uint8_t)revision;image[220]=(uint8_t)modules;
    for(unsigned n=0;n<modules;n++){
        uint8_t *p=image+224+n*size;p32(p,8192);p32(p+4,255);p16(p+(revision==5?10:20),size);
        unsigned off=revision==5?13:23;p[off]=0x70;p[off+1]=8;p[off+2]=4;
    }
    return (atom_rom){.image=image,.size=sizeof(image),.data=100};
}
static bool zero(const atom_memory *out){atom_memory empty={0};return !memcmp(out,&empty,sizeof(empty));}
int main(void){
    for(unsigned rev=3;rev<=5;rev++)for(unsigned modules=1;modules<=16;modules++){
        atom_rom rom=fixture(rev,modules);atom_memory out;
        CHECK(atom_memory_open(&rom,&out)==ATOM_MEMORY_OK && out.memory_mb==8192 && out.channel_enable==255 && out.type==0x70 && out.channels==8 && out.channel_bytes==2 && out.modules==modules);cases++;
    }
    for(unsigned rev=3;rev<=5;rev++){
        atom_rom rom=fixture(rev,2);unsigned size=rev==3?52:rev==4?60:84;
        /* Every truncation of header and the two variable-size modules. */
        for(unsigned length=0;length<24+size*2;length++){
            fixture(rev,2);p16(image+200,length);atom_memory out;memset(&out,0xff,sizeof(out));
            CHECK(atom_memory_open(&rom,&out)!=ATOM_MEMORY_OK && zero(&out));cases++;
        }
        unsigned geometry[]={0,4,(rev==5?13u:23u),(rev==5?14u:24u),(rev==5?15u:25u)};
        for(unsigned f=0;f<5;f++){
            fixture(rev,2);uint8_t *p=image+224+size;p[geometry[f]]^=1;atom_memory out;
            CHECK(atom_memory_open(&rom,&out)!=ATOM_MEMORY_OK && zero(&out));cases++;
        }
        for(unsigned field=0;field<2;field++)for(unsigned value=0;value<256;value++){
            fixture(rev,1);image[224+(rev==5?14:24)+field]=(uint8_t)value;atom_memory out;
            bool valid=field?(value>=4 && value<=6):(value>=1 && value<=16);
            CHECK((atom_memory_open(&rom,&out)==ATOM_MEMORY_OK)==valid);
            CHECK(valid || zero(&out));cases++;
        }
        fixture(rev,1);p16(image+224+(rev==5?10:20),size-1);atom_memory out;
        CHECK(atom_memory_open(&rom,&out)==ATOM_MEMORY_TABLE && zero(&out));cases++;
        fixture(rev,1);p16(image+224+(rev==5?10:20),4096);
        CHECK(atom_memory_open(&rom,&out)==ATOM_MEMORY_TABLE && zero(&out));cases++;
    }
    atom_rom rom=fixture(5,1);atom_memory out;
    image[102]=1;CHECK(atom_memory_open(&rom,&out)==ATOM_MEMORY_VERSION && zero(&out));cases++;
    fixture(5,1);image[203]=6;CHECK(atom_memory_open(&rom,&out)==ATOM_MEMORY_VERSION && zero(&out));cases++;
    fixture(5,1);image[220]=17;CHECK(atom_memory_open(&rom,&out)==ATOM_MEMORY_TABLE && zero(&out));cases++;
    CHECK(atom_memory_open(NULL,&out)==ATOM_MEMORY_INPUT && zero(&out));cases++;
    CHECK(atom_memory_open(&rom,NULL)==ATOM_MEMORY_INPUT);cases++;
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"gddr6_vram_info_2_3_to_2_5\":true,\"physical_hardware_verified\":false}\n",cases,checks);return 0;
}
