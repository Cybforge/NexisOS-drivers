/* Host test of the EDID audio-capability parser: valid blocks, every kind of damage, and a mutation fuzz that
 * must neither crash nor read outside the buffer (run under the undefined-behavior sanitizer). */
#include "../../tools/gpu-driver/common/cta_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases;
static void fix(uint8_t *b){unsigned sum=0;for(unsigned i=0;i<127;i++)sum+=b[i];b[127]=(uint8_t)(0-sum);}
static void edid(uint8_t *e,unsigned channels_minus_1,unsigned rates,unsigned sizes,int speakers){
    static const uint8_t h[8]={0,255,255,255,255,255,255,0};
    memset(e,0,256);memcpy(e,h,8);e[8]=0x04;e[9]=0x72;e[10]=0x58;e[11]=0x10;e[54+3]=0xfc;memcpy(e+54+5,"ACER VG270\n",11);e[126]=1;fix(e);
    uint8_t *x=e+128;x[0]=2;x[1]=3;unsigned p=4;
    x[p++]=(1<<5)|6;x[p++]=(unsigned)((2<<3)|1);x[p++]=0x1f;x[p++]=0x01; /* AC-3 descriptor first: must be ignored */
    x[p++]=(unsigned)((1<<3)|channels_minus_1);x[p++]=(uint8_t)rates;x[p++]=(uint8_t)sizes;
    if(speakers>=0){x[p++]=(4<<5)|3;x[p++]=(uint8_t)speakers;x[p++]=0;x[p++]=0;}
    x[2]=(uint8_t)p;fix(x);
}
int main(void){
    uint8_t e[256];nx_audio_caps c;
    edid(e,1,0x07,0x07,0x01);
    CHECK(nx_audio_caps_from_edid(e,256,&c) && c.valid && c.lpcm_channels==2 && c.lpcm_rates==7 && c.lpcm_sizes==7 && c.speakers==1);
    CHECK(c.manufacturer==0x0472 && c.product==0x1058 && c.name_length==10 && !memcmp(c.name,"ACER VG270",10) && nx_audio_caps_stereo48(&c));cases++;
    edid(e,1,0x03,0x07,-1);CHECK(nx_audio_caps_from_edid(e,256,&c) && c.speakers==1 && !nx_audio_caps_stereo48(&c));cases++; /* no 48 kHz */
    edid(e,1,0x07,0x06,-1);CHECK(nx_audio_caps_from_edid(e,256,&c) && !nx_audio_caps_stereo48(&c));cases++;                  /* no 16 bit */
    edid(e,7,0x7f,0x07,0x4f);CHECK(nx_audio_caps_from_edid(e,256,&c) && c.lpcm_channels==8 && c.speakers==0x4f && nx_audio_caps_stereo48(&c));cases++;
    edid(e,1,0x07,0x07,0x01);e[128+10]^=0xff;CHECK(!nx_audio_caps_from_edid(e,256,&c) && !c.valid);cases++; /* extension checksum */
    edid(e,1,0x07,0x07,0x01);e[0]=1;CHECK(!nx_audio_caps_from_edid(e,256,&c));cases++;                       /* header */
    edid(e,1,0x07,0x07,0x01);CHECK(!nx_audio_caps_from_edid(e,255,&c) && !nx_audio_caps_from_edid(e,100,&c) && !nx_audio_caps_from_edid(NULL,256,&c) && !nx_audio_caps_from_edid(e,256,NULL));cases++;
    edid(e,1,0x07,0x07,0x01);e[126]=0;CHECK(!nx_audio_caps_from_edid(e,256,&c));cases++;                     /* no extension announced */
    /* mutation fuzz */
    srand(12345);
    for(unsigned n=0;n<200000;n++){
        edid(e,1,0x07,0x07,0x01);
        unsigned flips=1+(unsigned)rand()%8;
        for(unsigned f=0;f<flips;f++)e[(unsigned)rand()%256]=(uint8_t)rand();
        if(rand()%2){fix(e);fix(e+128);}
        (void)nx_audio_caps_from_edid(e,(size_t)(rand()%2?256:(rand()%257)),&c);
        CHECK(c.name_length<sizeof(c.name) && c.name[c.name_length]==0);
        cases++;
    }
    printf("{\"passed\":true,\"cases\":%u}\n",cases);
    return 0;
}
