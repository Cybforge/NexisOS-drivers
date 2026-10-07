#include "../../tools/gpu-driver/amd/dcn302_hdmi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GET(v,f) (((v)&DCN302_HDMI_##f##_MASK)>>DCN302_HDMI_##f##_SHIFT)
#define SET(v,f,n) (((v)&~DCN302_HDMI_##f##_MASK)|(((uint32_t)(n)<<DCN302_HDMI_##f##_SHIFT)&DCN302_HDMI_##f##_MASK))
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s, case %u\n",__LINE__,#c,cases);exit(1);}}while(0)
static unsigned cases;
typedef struct {
    uint32_t regs[5][DCN302_HDMI_REGISTER_COUNT],before[5][DCN302_HDMI_REGISTER_COUNT];
    unsigned ops,reads,writes,fail_at,ignored,cs_updates,info_updates;bool posted,dead,invalid;
} model;
static bool locate(model *m,uint32_t offset,unsigned *b,enum dcn302_hdmi_register *r){
    for(unsigned n=0;n<5;n++)for(unsigned i=0;i<DCN302_HDMI_REGISTER_COUNT;i++)if(offset==dcn302_hdmi_register_bytes[n][i]){*b=n;*r=(enum dcn302_hdmi_register)i;return true;}
    m->invalid=true;return false;
}
static bool rd(void *context,uint32_t offset,uint32_t *out){
    model *m=context;unsigned b;enum dcn302_hdmi_register r;m->reads++;
    if(++m->ops==m->fail_at || m->dead || !locate(m,offset,&b,&r))return false;
    *out=m->regs[b][r];return true;
}
static bool wr(void *context,uint32_t offset,uint32_t value){
    model *m=context;unsigned b;enum dcn302_hdmi_register r;m->writes++;bool fail=++m->ops==m->fail_at;
    if(m->dead || (fail && !m->posted) || !locate(m,offset,&b,&r))return false;
    CHECK(r!=DCN302_HDMI_R_BE && r!=DCN302_HDMI_R_BE_ENABLE && r!=DCN302_HDMI_R_AUDIO_CLOCK && r!=DCN302_HDMI_R_AFMT_POWER);
    if(r==DCN302_HDMI_R_AFMT_PACKET && GET(value,CS_UPDATE)){m->cs_updates++;value&=~DCN302_HDMI_CS_UPDATE_MASK;}
    if(r==DCN302_HDMI_R_AFMT_INFO && GET(value,INFO_UPDATE)){m->info_updates++;value&=~DCN302_HDMI_INFO_UPDATE_MASK;}
    if(!m->ignored || m->ignored!=offset)m->regs[b][r]=value;
    return !fail;
}
static void init(model *m,dcn302_io *io,unsigned inst){
    memset(m,0,sizeof(*m));
    for(unsigned b=0;b<5;b++){
        for(unsigned r=0;r<DCN302_HDMI_REGISTER_COUNT;r++)m->regs[b][r]=0x4a606000u+(r<<1);
        m->regs[b][DCN302_HDMI_R_BE]=SET(SET(m->regs[b][DCN302_HDMI_R_BE],LINK_MODE,3),FE_SOURCE,1u<<b);
        m->regs[b][DCN302_HDMI_R_BE_ENABLE]=0;
        m->regs[b][DCN302_HDMI_R_AUDIO_CLOCK]=DCN302_HDMI_CLOCK_ENABLE_MASK|DCN302_HDMI_CLOCK_ON_MASK;
        m->regs[b][DCN302_HDMI_R_AFMT_POWER]=0;
        m->regs[b][DCN302_HDMI_R_FE]=SET(SET(SET(m->regs[b][DCN302_HDMI_R_FE],PIPE,b),RGB_ENCODING,1),COLOR_FORMAT,1);
        m->regs[b][DCN302_HDMI_R_AFMT_PACKET]&=~DCN302_HDMI_CS_UPDATE_MASK;
        m->regs[b][DCN302_HDMI_R_AFMT_INFO]&=~DCN302_HDMI_INFO_UPDATE_MASK;
    }
    memcpy(m->before,m->regs,sizeof(m->regs));*io=(dcn302_io){m,rd,wr,NULL};(void)inst;
}
static void restored(model *m){
    CHECK(!memcmp(m->before,m->regs,sizeof(m->regs)) && !m->invalid);
}
static void expected(model *m,unsigned inst,uint32_t clock,unsigned scdc,bool audio,unsigned source){
    uint32_t *r=m->regs[inst];
    CHECK(!GET(r[DCN302_HDMI_R_FE],RGB_ENCODING) && !GET(r[DCN302_HDMI_R_FE],COLOR_FORMAT));
    CHECK(GET(r[DCN302_HDMI_R_CONTROL],PACKET_VERSION)==1 && GET(r[DCN302_HDMI_R_CONTROL],KEEPOUT)==1);
    CHECK(!GET(r[DCN302_HDMI_R_CONTROL],DEEP_DEPTH) && !GET(r[DCN302_HDMI_R_CONTROL],DEEP_ENABLE));
    CHECK(GET(r[DCN302_HDMI_R_CONTROL],SCRAMBLE)==(scdc&1) && GET(r[DCN302_HDMI_R_CONTROL],CLOCK_RATIO)==(scdc>>1));
    CHECK(GET(r[DCN302_HDMI_R_GC],AVMUTE)==1 && !GET(r[DCN302_HDMI_R_AFMT_PACKET],SAMPLE_SEND));
    CHECK(GET(r[DCN302_HDMI_R_VBI],GC_CONT) && GET(r[DCN302_HDMI_R_VBI],GC_SEND) && GET(r[DCN302_HDMI_R_VBI],NULL_SEND));
    CHECK(GET(r[DCN302_HDMI_R_INFO0],INFO_SEND)==audio && GET(r[DCN302_HDMI_R_INFO1],INFO_LINE)==2);
    CHECK(GET(r[DCN302_HDMI_R_AFMT_SOURCE],SOURCE)==source && GET(r[DCN302_HDMI_R_AFMT_PACKET2],CHANNELS)==(audio?3:0));
    CHECK(GET(r[DCN302_HDMI_R_CS0],CHANNEL_L)==1 && GET(r[DCN302_HDMI_R_CS1],CHANNEL_R)==2);
    CHECK(GET(r[DCN302_HDMI_R_N48],N48)==6144 && GET(r[DCN302_HDMI_R_CTS48],CTS48)==clock);
    CHECK(GET(r[DCN302_HDMI_R_N32],N32)==4096 && GET(r[DCN302_HDMI_R_CTS32],CTS32)==clock);
    CHECK(GET(r[DCN302_HDMI_R_N44],N44)==6272);
    uint64_t numerator=(uint64_t)clock*1000*GET(r[DCN302_HDMI_R_N44],N44);
    uint64_t denominator=128u*GET(r[DCN302_HDMI_R_CTS44],CTS44),target=44100*denominator;
    uint64_t difference=numerator>target?numerator-target:target-numerator;
    CHECK(difference*1000000<=target*20); /* Arbitrary HDMI clocks stay within 20 ppm. */
    CHECK(m->cs_updates==1 && m->info_updates==1);
    CHECK(!(r[DCN302_HDMI_R_VBI]&dcn302_hdmi_acp_mask[inst]));
    if(inst)CHECK((r[DCN302_HDMI_R_VBI]&0x1000)==(m->before[inst][DCN302_HDMI_R_VBI]&0x1000));
    for(unsigned b=0;b<5;b++)if(b!=inst)CHECK(!memcmp(m->regs[b],m->before[b],sizeof(m->regs[b])));
}
static void normal(void){
    model m;dcn302_io io;dcn302_hdmi_transaction t;
    unsigned clocks[]={25000,25175,74250,148500,340000,340001,558100,594000,600000};
    for(unsigned inst=0;inst<5;inst++)for(unsigned i=0;i<sizeof(clocks)/sizeof(*clocks);i++)for(unsigned audio=0;audio<2;audio++){
        init(&m,&io,inst);unsigned cfg=clocks[i]>340000?3:0;
        CHECK(dcn302_hdmi_prepare(&io,inst,clocks[i],cfg,audio,6,&t)==DCN302_HDMI_OK && t.valid && t.prepared && !t.committed);
        expected(&m,inst,clocks[i],cfg,audio,6);
        CHECK(dcn302_hdmi_commit(&io,&t)==DCN302_HDMI_CLOCK);
        m.regs[inst][DCN302_HDMI_R_BE_ENABLE]=DCN302_HDMI_LINK_ENABLE_MASK|DCN302_HDMI_LINK_CLOCK_MASK;
        CHECK(dcn302_hdmi_commit(&io,&t)==DCN302_HDMI_OK && t.committed);
        CHECK(!GET(m.regs[inst][DCN302_HDMI_R_GC],AVMUTE) && GET(m.regs[inst][DCN302_HDMI_R_AFMT_PACKET],SAMPLE_SEND)==audio);
        CHECK(dcn302_hdmi_restore(&io,&t)==DCN302_HDMI_BUSY);
        m.regs[inst][DCN302_HDMI_R_BE_ENABLE]=0;
        CHECK(dcn302_hdmi_restore(&io,&t)==DCN302_HDMI_OK && !t.prepared && !t.committed);restored(&m);cases++;
    }
}
static void io_faults(void){
    model m;dcn302_io io;dcn302_hdmi_transaction t;
    init(&m,&io,2);CHECK(dcn302_hdmi_prepare(&io,2,558100,3,true,0,&t)==DCN302_HDMI_OK);unsigned ops=m.ops;
    for(unsigned posted=0;posted<2;posted++)for(unsigned op=1;op<=ops;op++){
        init(&m,&io,2);m.fail_at=op;m.posted=posted;
        CHECK(dcn302_hdmi_prepare(&io,2,558100,3,true,0,&t)==DCN302_HDMI_IO && !t.prepared && !t.committed);restored(&m);cases++;
    }
    init(&m,&io,0);m.ignored=dcn302_hdmi_register_bytes[0][DCN302_HDMI_R_CONTROL];
    CHECK(dcn302_hdmi_prepare(&io,0,558100,3,true,0,&t)==DCN302_HDMI_IO);restored(&m);cases++;
    init(&m,&io,0);CHECK(dcn302_hdmi_prepare(&io,0,558100,3,true,0,&t)==DCN302_HDMI_OK);
    m.dead=true;CHECK(dcn302_hdmi_restore(&io,&t)==DCN302_HDMI_BUSY && t.prepared);cases++;
    for(unsigned op=1;op<=8;op++){
        init(&m,&io,0);CHECK(dcn302_hdmi_prepare(&io,0,558100,3,true,0,&t)==DCN302_HDMI_OK);
        m.regs[0][DCN302_HDMI_R_BE_ENABLE]=DCN302_HDMI_LINK_ENABLE_MASK|DCN302_HDMI_LINK_CLOCK_MASK;
        m.fail_at=m.ops+op;m.posted=true;
        CHECK(dcn302_hdmi_commit(&io,&t)==DCN302_HDMI_IO && !t.committed);
        CHECK(!GET(m.regs[0][DCN302_HDMI_R_AFMT_PACKET],SAMPLE_SEND) && GET(m.regs[0][DCN302_HDMI_R_GC],AVMUTE));cases++;
    }
}
static void invalid(void){
    model m;dcn302_io io;dcn302_hdmi_transaction t;
    for(unsigned kind=0;kind<11;kind++){
        init(&m,&io,0);unsigned inst=kind==0?5:0,clock=kind==1?24000:kind==2?600001:558100,cfg=kind==3?0:3,source=kind==4?7:0;
        if(kind==5)m.regs[0][DCN302_HDMI_R_BE_ENABLE]=DCN302_HDMI_LINK_ENABLE_MASK;
        if(kind==6)m.regs[0][DCN302_HDMI_R_BE]=SET(m.regs[0][DCN302_HDMI_R_BE],LINK_MODE,0);
        if(kind==7)m.regs[0][DCN302_HDMI_R_AUDIO_CLOCK]=0;
        if(kind==8)m.regs[0][DCN302_HDMI_R_AFMT_POWER]=DCN302_HDMI_POWER_STATE_MASK;
        if(kind==9)m.regs[0][DCN302_HDMI_R_BE]=SET(m.regs[0][DCN302_HDMI_R_BE],FE_SOURCE,2);
        if(kind==10)m.regs[0][DCN302_HDMI_R_FE]=SET(m.regs[0][DCN302_HDMI_R_FE],PIPE,7);
        CHECK(dcn302_hdmi_prepare(&io,inst,clock,cfg,true,source,&t)!=DCN302_HDMI_OK && !m.writes && !t.valid);cases++;
    }
    init(&m,&io,0);CHECK(dcn302_hdmi_prepare(&io,0,148500,1,true,0,&t)==DCN302_HDMI_OK);expected(&m,0,148500,1,true,0);cases++;
}
int main(void){normal();io_faults();invalid();printf("{\"passed\":true,\"cases\":%u,\"native_dcn302_hdmi_packets\":true,\"physical_hdmi_audio_verified\":false,\"full_rx6600_driver_complete\":false}\n",cases);return 0;}
