/* Original MIT licensed register-model checks; no hardware access. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/drivers/audio/hdmi.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } assertions++; } while (0)
static unsigned assertions,cases;
typedef struct {
    bool amd,failed;
    unsigned calls,fail_at,size,eld_invalid,descriptor,slot[8],count,digital1,digital2,allocation,mode,ramp,hbr,dip_enabled,dip_index,packet_size;
    uint32_t sense,transport,sad;
    uint8_t eld[256],packet[32];
} model;
static model state;
static bool transaction(model *m) {
    CHECK(!m->failed);m->calls++;
    if(m->calls==m->fail_at){m->failed=true;return false;}
    return true;
}
static bool read_register(void *context,unsigned nid,unsigned verb,unsigned payload,uint32_t *out) {
    model *m=context;if(!transaction(m))return false;
    CHECK(nid==5);
    switch(verb) {
    case 0xf09: CHECK(payload==0);*out=m->sense;return true;
    case 0xf70: CHECK(m->amd && payload==0);*out=m->transport;return true;
    case 0xf76: CHECK(m->amd && m->descriptor==8 && payload==0);*out=m->sad;return true;
    case 0xf7c: CHECK(m->amd && payload==0);*out=m->hbr;return true;
    case 0xf2e: CHECK(!m->amd && payload==8);*out=m->size;return true;
    case 0xf2f: CHECK(!m->amd && payload<m->size);*out=m->eld[payload]|(payload==m->eld_invalid?0u:0x80000000u);return true;
    default: CHECK(false);return false;
    }
}
static bool write_register(void *context,unsigned nid,unsigned verb,unsigned payload) {
    model *m=context;if(!transaction(m))return false;
    CHECK(payload<=255);
    if(nid==2) {
        switch(verb) {
        case 0x72d:m->count=payload+1;return true;
        case 0x70d:m->digital1=payload;return true;
        case 0x70e:m->digital2=payload;return true;
        case 0x770:CHECK(m->amd);m->ramp=payload;return true;
        default:CHECK(false);return false;
        }
    }
    CHECK(nid==5);
    if(m->amd) {
        switch(verb) {
        case 0x776:m->descriptor=payload;return true;
        case 0x771:m->allocation=payload;return true;
        case 0x772:CHECK(payload==0);return true;
        case 0x789:m->mode=payload;return true;
        case 0x77c:m->hbr=payload;return true;
        default:
            if(verb>=0x777 && verb<=0x77a){m->slot[(verb-0x777)*2]=payload;return true;}
            if(verb>=0x785 && verb<=0x788){m->slot[(verb-0x785)*2+1]=payload;return true;}
            CHECK(false);return false;
        }
    }
    switch(verb) {
    case 0x734:m->slot[payload&7]=payload>>4;return true;
    case 0x732:m->dip_enabled=payload;return true;
    case 0x730:CHECK(payload==0);m->dip_index=0;return true;
    case 0x731:CHECK(m->dip_enabled==0 && m->dip_index<32);m->packet[m->dip_index++]=(uint8_t)payload;m->packet_size=m->dip_index;return true;
    default:CHECK(false);return false;
    }
}
static const hdmi_codec_io io={&state,read_register,write_register};
static void reset(bool amd) {
    memset(&state,0,sizeof(state));state.amd=amd;state.sense=0xc0000000;
    state.transport=0x101;state.sad=0x04010409;state.hbr=0x11;
    state.size=24;state.eld_invalid=256;state.eld[0]=2<<3;state.eld[2]=5;
    state.eld[5]=1<<4;state.eld[20]=9;state.eld[21]=4;state.eld[22]=1;
    for(unsigned i=0;i<8;i++)state.slot[i]=0xff;
}
static bool sink(void){return hdmi_codec_stereo_sink(&io,5,state.amd?0x1002aa01:0x80862801);}
static bool program(unsigned revision){return hdmi_codec_program_stereo(&io,5,2,state.amd?0x1002aa01:0x80862801,revision);}
int main(void) {
    CHECK(hdmi_codec_is_amd(0x1002aa01));CHECK(!hdmi_codec_is_amd(0x1002ffff));CHECK(!hdmi_codec_is_amd(0x80862801));cases++;
    reset(true);CHECK(sink());CHECK(state.calls==4 && state.descriptor==8);cases++;
    /* A valid stereo descriptor does not require an explicit speaker block. */
    reset(true);state.transport=0x100;CHECK(sink());cases++;
    reset(true);state.sad=0x04010008;CHECK(sink());cases++;
    reset(true);state.sad=0x0001040f;CHECK(sink());cases++;
    const uint32_t bad_sense[]={0,0x40000000,0x80000000};
    for(unsigned a=0;a<3;a++){reset(true);state.sense=bad_sense[a];CHECK(!sink() && state.calls==1);cases++;}
    const uint32_t bad_transport[]={0,1,0x201,0x301,0xffffffff};
    for(unsigned a=0;a<5;a++){reset(true);state.transport=bad_transport[a];CHECK(!sink());cases++;}
    const uint32_t bad_sad[]={0,0xffffffff,0x04000409,0x00010209,0x00010408,0x04010411};
    for(unsigned a=0;a<6;a++){reset(true);state.sad=bad_sad[a];CHECK(!sink());cases++;}
    reset(true);CHECK(program(0x100300));CHECK(state.count==2 && state.digital1==1 && state.digital2==0);
    CHECK(state.allocation==0 && state.mode==1 && state.ramp==180 && state.hbr==1);
    CHECK(state.slot[0]==1 && state.slot[1]==0x11);
    for(unsigned s=2;s<8;s++)CHECK(state.slot[s]==0);
    unsigned amd_calls=state.calls;cases++;
    reset(true);CHECK(program(0x100200));CHECK(state.mode==0 && state.ramp==0);
    CHECK(state.slot[0]==1 && state.slot[2]==0 && state.slot[4]==0 && state.slot[6]==0);
    CHECK(state.slot[1]==0xff && state.slot[3]==0xff);cases++;
    reset(true);state.hbr=0;CHECK(program(0x100300));CHECK(state.hbr==0);cases++;
    reset(false);CHECK(sink());cases++;
    reset(false);CHECK(program(0));CHECK(state.count==2 && state.slot[0]==0 && state.slot[1]==1);
    for(unsigned s=2;s<8;s++)CHECK(state.slot[s]==15);
    CHECK(state.packet_size==14 && state.dip_enabled==0xc0);
    CHECK(state.packet[0]==0x84 && state.packet[1]==1 && state.packet[2]==10 && state.packet[4]==1);
    unsigned sum=0;for(unsigned i=0;i<14;i++)sum+=state.packet[i];CHECK((sum&255)==0);
    unsigned generic_calls=state.calls;cases++;
    /* Fail every individual transport transaction: no writes after failure. */
    for(unsigned f=1;f<=amd_calls;f++){reset(true);state.fail_at=f;CHECK(!program(0x100300) && state.failed && state.calls==f);cases++;}
    for(unsigned f=1;f<=generic_calls;f++){reset(false);state.fail_at=f;CHECK(!program(0) && state.failed && state.calls==f);cases++;}
    for(unsigned f=1;f<=4;f++){reset(true);state.fail_at=f;CHECK(!sink() && state.calls==f);cases++;}
    for(unsigned f=1;f<=26;f++){reset(false);state.fail_at=f;CHECK(!sink() && state.calls==f);cases++;}
    for(unsigned i=0;i<24;i++){reset(false);state.eld_invalid=i;CHECK(!sink());cases++;}
    for(unsigned size=0;size<20;size++){reset(false);state.size=size;CHECK(!sink());cases++;}
    reset(false);state.eld[2]=255;CHECK(!sink());cases++;
    reset(false);state.eld[2]=4;CHECK(!sink());cases++;
    reset(false);state.eld[4]=31;CHECK(!sink());cases++;
    reset(false);state.eld[5]=0xf0;CHECK(!sink());cases++;
    reset(false);state.eld[5]=0x14;CHECK(!sink());cases++;
    reset(false);state.eld[21]=2;CHECK(!sink());cases++;
    reset(false);state.eld[22]=0;CHECK(!sink());cases++;
    reset(false);state.eld[20]=8;CHECK(!sink());cases++;
    reset(false);state.eld[0]=31<<3;CHECK(sink());cases++;
    reset(false);state.size=28;state.eld[2]=6;state.eld[4]=4;
    memmove(state.eld+24,state.eld+20,3);memset(state.eld+20,'M',4);CHECK(sink());cases++;
    reset(true);CHECK(!hdmi_codec_stereo_sink(NULL,5,0x1002aa01));
    CHECK(!hdmi_codec_stereo_sink(&io,256,0x1002aa01));
    CHECK(!hdmi_codec_program_stereo(&io,5,256,0x1002aa01,0));
    CHECK(!hdmi_codec_program_stereo(&io,256,2,0x1002aa01,0));CHECK(state.calls==0);cases++;
    printf("{\"passed\":true,\"cases\":%u,\"assertions\":%u,\"physical_hardware_tested\":false}\n",cases,assertions);
    return 0;
}
