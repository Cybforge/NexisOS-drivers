#include "../../tools/gpu-driver/amd/dcn302_dpp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases,checks;
typedef struct {
    uint32_t regs[5][DCN302_DPP_REGISTER_COUNT],otg[5][3];unsigned hubp,reads,writes,fail_read,fail_write,ignore_write;
    unsigned activate_write,clock_loss_write,power_loss_write,guard_loss_write,unstable_read,log[64],log_count;
    bool posted,guard_ok,invalid;
} model;
static bool guard(void *context){return ((model *)context)->guard_ok;}
static bool rd(void *context,uint32_t address,uint32_t *out){
    model *m=context;unsigned n=++m->reads;if(n==m->fail_read)return false;
    for(unsigned p=0;p<5;p++){
        for(unsigned r=0;r<DCN302_DPP_REGISTER_COUNT;r++)if(address==dcn302_dpp_register_bytes[p][r]){
            *out=m->regs[p][r];if(n==m->unstable_read)*out^=1;return true;
        }
        const unsigned regs[]={DCN302_R_CONTROL,DCN302_R_CLOCK,DCN302_R_VTG};
        for(unsigned r=0;r<3;r++)if(address==dcn302_register_bytes[p][regs[r]]){*out=m->otg[p][r];return true;}
    }
    m->invalid=true;return false;
}
static bool wr(void *context,uint32_t address,uint32_t value){
    model *m=context;unsigned n=++m->writes,p=m->hubp;
    for(unsigned r=0;r<DCN302_DPP_REGISTER_COUNT;r++)if(address==dcn302_dpp_register_bytes[p][r]){
        CHECK(r<DCN302_DPP_PROGRAM_COUNT && dcn302_dpp_owned[r] && !(value&dcn302_dpp_readonly[r]) && m->guard_ok);
        CHECK(!((value^m->regs[p][r])&~(dcn302_dpp_owned[r]|dcn302_dpp_readonly[r])));
        for(unsigned i=0;i<5;i++)CHECK(!(m->otg[i][0]&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) &&
            !(m->otg[i][1]&DCN302_BUSY_MASK) && !(m->otg[i][2]&DCN302_VTG_ENABLE_MASK));
        CHECK(m->log_count<64);m->log[m->log_count++]=r;
        if(n==m->ignore_write)return true;
        if(n==m->fail_write && !m->posted)return false;
        m->regs[p][r]=value|(m->regs[p][r]&dcn302_dpp_readonly[r]);
        if(n==m->activate_write)m->otg[4][0]|=DCN302_MASTER_ACTIVE_MASK;
        if(n==m->clock_loss_write)m->regs[p][DCN302_DPP_R_CLOCK]&=~0x200000u;
        if(n==m->power_loss_write)m->regs[p][DCN302_DPP_R_STATUS]|=4;
        if(n==m->guard_loss_write)m->guard_ok=false;
        return n!=m->fail_write;
    }
    m->invalid=true;return false;
}
static nexis_gpu_timing mode(void){return (nexis_gpu_timing){.hactive=1920,.vactive=1080};}
static void init(model *m,dcn302_io *io,unsigned hubp){
    memset(m,0,sizeof(*m));m->hubp=hubp;m->guard_ok=true;
    for(unsigned p=0;p<5;p++){
        uint32_t *r=m->regs[p];for(unsigned n=0;n<DCN302_DPP_REGISTER_COUNT;n++)r[n]=0xa5c655a5&~(dcn302_dpp_owned[n]|dcn302_dpp_readonly[n]);
        for(unsigned n=0;n<DCN302_DPP_PROGRAM_COUNT;n++)r[n]|=dcn302_dpp_owned[n];
        r[DCN302_DPP_R_CM]&=~1u;
        r[DCN302_DPP_R_FORMAT]|=0x100000;r[DCN302_DPP_R_CM]|=0x100;r[DCN302_DPP_R_CSC]|=0xc;
        r[DCN302_DPP_R_CNVC_CURSOR]|=0x10000;r[DCN302_DPP_R_MODE]|=0x1000;r[DCN302_DPP_R_LB_MEMORY]|=0x03030000;
        r[DCN302_DPP_R_VIEW_START]=0;r[DCN302_DPP_R_VIEW_SIZE]=1080u<<16|1920;
        r[DCN302_DPP_R_CLOCK]=0xf00001;r[DCN302_DPP_R_STATUS]=3;r[DCN302_DPP_R_POWER]=0;
        r[DCN302_DPP_R_LOCAL_CLOCK]=0x10;
    }
    *io=(dcn302_io){m,rd,wr,NULL};
}
static void prepare(model *m,dcn302_io *io,dcn302_dpp_transaction *t){
    nexis_gpu_timing request=mode();CHECK(!dcn302_dpp_prepare(io,m->hubp,&request,guard,t) && t->prepared && !t->dirty && !m->writes);
}
static bool same(const model *m,const model *old){return !memcmp(m->regs,old->regs,sizeof(m->regs));}
static void success(void){
    model m,old;dcn302_io io;dcn302_dpp_transaction t;
    CHECK(dcn302_dpp_register_bytes[0][DCN302_DPP_R_CURSOR]==0xece0 && dcn302_dpp_register_bytes[0][DCN302_DPP_R_CNVC_CURSOR]==0x106c4);
    for(unsigned p=0;p<5;p++){
        init(&m,&io,p);old=m;prepare(&m,&io,&t);
        CHECK(!dcn302_dpp_apply_disabled(&io,&t) && t.applied && t.dirty && !t.poisoned && !m.invalid);
        for(unsigned r=0;r<DCN302_DPP_PROGRAM_COUNT;r++)CHECK((m.regs[p][r]&dcn302_dpp_owned[r])==
            (r==DCN302_DPP_R_RECOUT_SIZE || r==DCN302_DPP_R_MPC_SIZE?(1080u<<16|1920):r==DCN302_DPP_R_LB_MEMORY?0x3f00:
             r==DCN302_DPP_R_PIXEL_FORMAT?8:r==DCN302_DPP_R_FORMAT?0x24000000:r==DCN302_DPP_R_CM?1:0));
        unsigned writes=m.writes;CHECK(!dcn302_dpp_verify_installed(&io,&t) && m.writes==writes);
        CHECK(dcn302_dpp_apply_disabled(&io,&t)==DCN302_BUSY);
        CHECK(!dcn302_dpp_restore_disabled(&io,&t) && !t.dirty && !t.applied && !t.poisoned && same(&m,&old));
        CHECK(m.log[m.log_count-2]==DCN302_DPP_R_CNVC_CURSOR && m.log[m.log_count-1]==DCN302_DPP_R_CURSOR);cases++;
    }
    init(&m,&io,0);prepare(&m,&io,&t);memcpy(m.regs[0],t.after,sizeof(t.after));m.regs[0][DCN302_DPP_R_CLOCK]|=0xf00000;
    prepare(&m,&io,&t);old=m;CHECK(!dcn302_dpp_apply_disabled(&io,&t) && t.applied && !t.dirty && !m.writes);
    CHECK(!dcn302_dpp_verify_installed(&io,&t));CHECK(!dcn302_dpp_restore_disabled(&io,&t) && !m.writes && same(&m,&old));cases++;
    init(&m,&io,4);m.otg[3][0]=DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK;prepare(&m,&io,&t);
    CHECK(dcn302_dpp_apply_disabled(&io,&t)==DCN302_BUSY && !m.writes);m.otg[3][0]=0;
    CHECK(!dcn302_dpp_apply_disabled(&io,&t));CHECK(!dcn302_dpp_restore_disabled(&io,&t));cases++;
}
static void inputs(void){
    model m;dcn302_io io;dcn302_dpp_transaction t;nexis_gpu_timing request=mode();
    for(unsigned fault=0;fault<12;fault++){
        init(&m,&io,0);request=mode();
        switch(fault){case 0:request.hactive=0;break;case 1:request.vactive=0;break;case 2:request.hactive=16384;break;
        case 3:request.vactive=16384;break;case 4:io.read=NULL;break;case 5:io.write=NULL;break;
        case 6:m.regs[0][DCN302_DPP_R_VIEW_START]=1;break;case 7:m.regs[0][DCN302_DPP_R_VIEW_SIZE]^=1;break;
        case 8:m.regs[0][DCN302_DPP_R_CLOCK]=0;break;case 9:m.regs[0][DCN302_DPP_R_STATUS]|=4;break;
        case 10:m.regs[0][DCN302_DPP_R_LOCAL_CLOCK]=0;break;case 11:m.regs[0][DCN302_DPP_R_LOCAL_CLOCK]|=0x10000000;break;}
        CHECK(dcn302_dpp_prepare(&io,0,&request,guard,&t)!=DCN302_OK && !t.prepared && !m.writes && !m.invalid);cases++;
    }
    init(&m,&io,0);request=mode();
    CHECK(dcn302_dpp_prepare(&io,5,&request,guard,&t)==DCN302_INPUT);
    CHECK(dcn302_dpp_prepare(&io,0,&request,NULL,&t)==DCN302_INPUT);
    CHECK(dcn302_dpp_prepare(&io,0,&request,guard,NULL)==DCN302_INPUT);
    CHECK(dcn302_dpp_prepare(&t.owner,0,&request,guard,&t)==DCN302_INPUT);
    CHECK(dcn302_dpp_prepare(&io,0,&t.timing,guard,&t)==DCN302_INPUT);
    CHECK(dcn302_dpp_prepare(&io,0,(nexis_gpu_timing *)(UINTPTR_MAX-3),guard,&t)==DCN302_INPUT);
    CHECK(dcn302_dpp_prepare((dcn302_io *)((unsigned char *)&io+1),0,&request,guard,&t)==DCN302_INPUT && !m.reads);cases++;
    for(unsigned value=1;value<=16383;value+=8191){
        init(&m,&io,0);request.hactive=request.vactive=value;m.regs[0][DCN302_DPP_R_VIEW_SIZE]=value<<16|value;
        CHECK(!dcn302_dpp_prepare(&io,0,&request,guard,&t));CHECK(!dcn302_dpp_apply_disabled(&io,&t));
        CHECK(!dcn302_dpp_restore_disabled(&io,&t));cases++;
    }
    init(&m,&io,0);prepare(&m,&io,&t);unsigned reads=m.reads;request=mode();
    for(unsigned n=1;n<=reads;n++){
        init(&m,&io,0);m.fail_read=n;CHECK(dcn302_dpp_prepare(&io,0,&request,guard,&t)==DCN302_IO && !t.prepared && !m.writes);cases++;
    }
    init(&m,&io,0);m.unstable_read=18;
    CHECK(dcn302_dpp_prepare(&io,0,&request,guard,&t)==DCN302_READBACK && !m.writes);cases++;
}
static void conditions(void){
    model m;dcn302_io io;dcn302_dpp_transaction t;
    for(unsigned fault=0;fault<40;fault++){
        init(&m,&io,0);prepare(&m,&io,&t);
        if(fault<20){unsigned p=fault/4;switch(fault%4){case 0:m.otg[p][0]|=DCN302_MASTER_ENABLE_MASK;break;case 1:m.otg[p][0]|=DCN302_MASTER_ACTIVE_MASK;break;
        case 2:m.otg[p][1]|=DCN302_BUSY_MASK;break;case 3:m.otg[p][2]|=DCN302_VTG_ENABLE_MASK;break;}}
        else if(fault<26)m.regs[0][DCN302_DPP_R_STATUS]|=1u<<(2*(fault-19));
        else switch(fault){case 26:m.guard_ok=false;break;case 27:m.regs[0][DCN302_DPP_R_CLOCK]&=~0x200000u;break;
        case 28:m.regs[0][DCN302_DPP_R_POWER]^=0x100;break;case 29:m.regs[0][DCN302_DPP_R_VIEW_START]^=1;break;
        case 30:m.regs[0][DCN302_DPP_R_CURSOR]^=2;break;case 31:t.after[0]^=1;break;case 32:t.prepared=false;break;
        case 33:t.touched=1u<<DCN302_DPP_PROGRAM_COUNT;break;case 34:t.hubp=5;break;case 35:t.guard=NULL;break;case 36:io.context=&t;break;
        case 37:t.before[DCN302_DPP_R_MODE]|=0x1000;break;
        case 38:m.regs[0][DCN302_DPP_R_LOCAL_CLOCK]=0;break;case 39:m.regs[0][DCN302_DPP_R_LOCAL_CLOCK]|=0x10000000;break;}
        CHECK(dcn302_dpp_apply_disabled(&io,&t)!=DCN302_OK && !m.writes && !m.invalid);cases++;
    }
    init(&m,&io,0);prepare(&m,&io,&t);CHECK(dcn302_dpp_verify_installed(&io,&t)==DCN302_BUSY && !m.writes);cases++;
}
static void faults(void){
    model m,old;dcn302_io io;dcn302_dpp_transaction t;
    init(&m,&io,0);prepare(&m,&io,&t);unsigned start=m.reads;
    CHECK(!dcn302_dpp_apply_disabled(&io,&t));unsigned reads=m.reads-start,writes=m.writes;
    for(unsigned n=1;n<=reads;n++){
        init(&m,&io,0);prepare(&m,&io,&t);old=m;m.fail_read=m.reads+n;
        CHECK(dcn302_dpp_apply_disabled(&io,&t)!=DCN302_OK && !t.dirty && !t.poisoned && !t.applied && same(&m,&old) && !m.invalid);cases++;
    }
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=writes;n++){
        init(&m,&io,0);prepare(&m,&io,&t);old=m;if(kind==2)m.ignore_write=n;else{m.fail_write=n;m.posted=kind==1;}
        CHECK(dcn302_dpp_apply_disabled(&io,&t)!=DCN302_OK && !t.dirty && !t.poisoned && same(&m,&old) && !m.invalid);cases++;
    }
    for(unsigned kind=0;kind<4;kind++)for(unsigned n=1;n<=writes;n++){
        init(&m,&io,0);prepare(&m,&io,&t);old=m;
        if(kind==0)m.activate_write=n;else if(kind==1)m.clock_loss_write=n;else if(kind==2)m.power_loss_write=n;else m.guard_loss_write=n;
        CHECK(dcn302_dpp_apply_disabled(&io,&t)==DCN302_ROLLBACK && t.dirty && t.poisoned && m.writes==n);
        CHECK(dcn302_dpp_restore_disabled(&io,&t)==DCN302_ROLLBACK && m.writes==n);
        m.otg[4][0]=0;m.regs[0][DCN302_DPP_R_CLOCK]=old.regs[0][DCN302_DPP_R_CLOCK];m.regs[0][DCN302_DPP_R_STATUS]=old.regs[0][DCN302_DPP_R_STATUS];m.guard_ok=true;
        CHECK(!dcn302_dpp_restore_disabled(&io,&t) && !t.dirty && !t.poisoned && same(&m,&old) && !m.invalid);cases++;
    }
    init(&m,&io,0);prepare(&m,&io,&t);CHECK(!dcn302_dpp_apply_disabled(&io,&t));start=m.reads;unsigned w=m.writes;
    CHECK(!dcn302_dpp_restore_disabled(&io,&t));reads=m.reads-start;writes=m.writes-w;
    for(unsigned n=1;n<=reads;n++){
        init(&m,&io,0);prepare(&m,&io,&t);old=m;CHECK(!dcn302_dpp_apply_disabled(&io,&t));m.fail_read=m.reads+n;
        CHECK(dcn302_dpp_restore_disabled(&io,&t)==DCN302_ROLLBACK && t.dirty && t.poisoned);
        CHECK(!dcn302_dpp_restore_disabled(&io,&t) && !t.dirty && !t.poisoned && same(&m,&old) && !m.invalid);cases++;
    }
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=writes;n++){
        init(&m,&io,0);prepare(&m,&io,&t);old=m;CHECK(!dcn302_dpp_apply_disabled(&io,&t));
        if(kind==2)m.ignore_write=m.writes+n;else{m.fail_write=m.writes+n;m.posted=kind==1;}
        CHECK(dcn302_dpp_restore_disabled(&io,&t)==DCN302_ROLLBACK && t.poisoned && t.dirty);
        CHECK(!dcn302_dpp_restore_disabled(&io,&t) && !t.dirty && !t.poisoned && same(&m,&old) && !m.invalid);cases++;
    }
}
int main(void){success();inputs();conditions();faults();printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"native_dcn3_float_scaler_lb\":true,\"native_rgb8_color_bypass\":true,\"native_local_dpp_clock_verified\":true,\"both_physical_cursor_enables\":true,\"native_maximum_lb_config\":true,\"posted_failure_rollback\":true,\"physical_hardware_tested\":false}\n",cases,checks);return 0;}
