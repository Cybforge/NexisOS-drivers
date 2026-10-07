#include "../../tools/gpu-driver/amd/dcn302_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static unsigned cases;
typedef struct {uint32_t regs[DCN302_REGISTER_COUNT],frame_bias;uint64_t time,step_us;unsigned reads,writes,fail_at;bool frozen,backwards,jitter,geometry_changes;} model;
static bool rd(void *context,uint32_t address,uint32_t *out){
    model *m=context;if(++m->reads==m->fail_at)return false;
    for(unsigned r=0;r<DCN302_REGISTER_COUNT;r++)if(address==dcn302_register_bytes[0][r]){
        if(r==DCN302_R_FRAME_COUNT){*out=(uint32_t)(m->time/m->step_us+m->frame_bias)&DCN302_FRAME_COUNT_MASK;return true;}
        *out=m->regs[r];
        if(m->geometry_changes && r==DCN302_R_H_TOTAL && m->time>m->step_us*4)(*out)++;
        return true;
    }
    CHECK(false);return false;
}
static bool delay(void *context,uint32_t us){
    model *m=context;CHECK(us==1);
    if(!m->frozen)m->time+=m->jitter && m->time>=m->step_us*4?100:1;
    return true;
}
static uint64_t time_us(void *context){model *m=context;return m->backwards && m->time>10?0:m->time;}
static dcn302_io init(model *m,unsigned hz){
    memset(m,0,sizeof(*m));m->step_us=1000000/hz;
    m->regs[DCN302_R_CONTROL]=DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK;
    m->regs[DCN302_R_CLOCK]=DCN302_CLOCK_ENABLE_MASK|DCN302_CLOCK_ON_MASK;
    m->regs[DCN302_R_H_TOTAL]=2199;m->regs[DCN302_R_V_TOTAL]=m->regs[DCN302_R_V_MIN]=m->regs[DCN302_R_V_MAX]=1124;
    m->regs[DCN302_R_H_BLANK]=2112u|192u<<16;m->regs[DCN302_R_V_BLANK]=1121u|41u<<16;
    m->regs[DCN302_R_H_SYNC]=44u<<16;m->regs[DCN302_R_V_SYNC]=5u<<16;
    m->regs[DCN302_R_SOURCE]=15u<<12;
    return (dcn302_io){m,rd,NULL,delay};
}
int main(void){
    model m;dcn302_clock_measurement measured;
    unsigned hz[]={20,30,60,120,144,240,360,500,1000};
    for(unsigned n=0;n<sizeof(hz)/sizeof(*hz);n++)for(unsigned wrap=0;wrap<2;wrap++){
        dcn302_io io=init(&m,hz[n]);if(wrap)m.frame_bias=DCN302_FRAME_COUNT_MASK-4;
        CHECK(dcn302_clock_measure(&io,0,time_us,16,&measured)==DCN302_OK);
        uint32_t refresh=(uint32_t)((16000000000ULL+measured.elapsed_us/2)/measured.elapsed_us);
        CHECK(measured.frames==16 && measured.elapsed_us==m.step_us*16 && measured.refresh_millihz==refresh);
        uint32_t clock=(uint32_t)((2200ULL*1125*16000+measured.elapsed_us/2)/measured.elapsed_us);
        CHECK(measured.pixel_khz==clock && measured.uncertainty_ppm<=1000 && !m.writes);cases++;
    }
    for(unsigned fault=0;fault<6;fault++){
        dcn302_io io=init(&m,240);m.frozen=fault==0;m.backwards=fault==1;m.jitter=fault==2;m.geometry_changes=fault==3;
        if(fault==4)m.regs[DCN302_R_CONTROL]=0;
        if(fault==5){m.regs[DCN302_R_V_MIN]--;m.regs[DCN302_R_V_CONTROL]=DCN302_V_MIN_SELECT_MASK;}
        enum dcn302_error e=dcn302_clock_measure(&io,0,time_us,16,&measured);
        CHECK(e==(fault==0?DCN302_TIMEOUT:fault==4?DCN302_BUSY:fault==5?DCN302_UNSUPPORTED:DCN302_READBACK));
        CHECK(!measured.pixel_khz && !measured.refresh_millihz && !m.writes);cases++;
    }
    for(unsigned fault=1;fault<53;fault++){
        dcn302_io io=init(&m,240);m.fail_at=fault;CHECK(dcn302_clock_measure(&io,0,time_us,16,&measured)!=DCN302_OK && !measured.pixel_khz);cases++;
    }
    dcn302_io io=init(&m,240);CHECK(dcn302_clock_measure(&io,0,time_us,0,&measured)==DCN302_INPUT && !m.reads);cases++;
    io=init(&m,240);m.regs[DCN302_R_V_MIN]=m.regs[DCN302_R_V_MAX]=0;
    CHECK(dcn302_clock_measure(&io,0,time_us,16,&measured)==DCN302_OK);cases++;
    printf("{\"passed\":true,\"cases\":%u,\"native_frame_counter_clock_measurement\":true,\"requested_clock_used\":false,\"physical_clock_verified\":false}\n",cases);return 0;
}
