#include "../../tools/gpu-driver/amd/dcn302_hubbub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases,checks;
typedef struct {
    uint32_t regs[DCN302_HUBBUB_REGISTER_COUNT],ref,timer,otg[5][3],ctrl[5],clock[5];
    unsigned reads,writes,fail_read,fail_write,ignore_write,activate_write,clock_loss_write,ref_change_write;
    unsigned log[128],log_count,unstable_read;
    bool posted,invalid;
} model;
static bool rd(void *context,uint32_t addr,uint32_t *out){
    model *m=context;unsigned n=++m->reads;if(n==m->fail_read)return false;
    if(addr==DCN302_HUBBUB_REF_BYTES){*out=m->ref;return true;}
    if(addr==DCN302_HUBBUB_TIMER_BYTES){*out=m->timer;if(n==m->unstable_read)*out^=0x10000;return true;}
    for(unsigned r=0;r<DCN302_HUBBUB_REGISTER_COUNT;r++)if(addr==dcn302_hubbub_register_bytes[r]){*out=m->regs[r];if(n==m->unstable_read)*out^=1;return true;}
    for(unsigned p=0;p<5;p++){
        if(addr==dcn302_hubp_register_bytes[p][DCN302_HUBP_R_DCHUBP_CNTL]){*out=m->ctrl[p];return true;}
        if(addr==dcn302_hubp_clock_bytes[p]){*out=m->clock[p];return true;}
        unsigned index[]={DCN302_R_CONTROL,DCN302_R_CLOCK,DCN302_R_VTG};
        for(unsigned r=0;r<3;r++)if(addr==dcn302_register_bytes[p][index[r]]){*out=m->otg[p][r];return true;}
    }
    m->invalid=true;return false;
}
static bool wr(void *context,uint32_t addr,uint32_t value){
    model *m=context;unsigned n=++m->writes;
    for(unsigned r=0;r<DCN302_HUBBUB_REGISTER_COUNT;r++)if(addr==dcn302_hubbub_register_bytes[r]){
        CHECK(!((value^m->regs[r])&~dcn302_hubbub_owned[r]));
        for(unsigned p=0;p<5;p++)CHECK(!(m->otg[p][0]&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) &&
            !(m->otg[p][1]&DCN302_BUSY_MASK) && !(m->otg[p][2]&DCN302_VTG_ENABLE_MASK));
        /* Policy must be held low throughout watermark updates/restoration. */
        if(r)CHECK((m->regs[0]&0x33)==0x22);
        CHECK(m->log_count<128);m->log[m->log_count++]=r;
        if(n==m->ignore_write)return true;
        if(n==m->fail_write && !m->posted)return false;
        m->regs[r]=value;
        if(n==m->activate_write)m->otg[4][0]|=DCN302_MASTER_ACTIVE_MASK;
        if(n==m->clock_loss_write)m->clock[0]=0;
        if(n==m->ref_change_write)m->timer^=0x10000;
        return n!=m->fail_write;
    }
    m->invalid=true;return false;
}
static void init(model *m,dcn302_io *io){
    memset(m,0,sizeof(*m));m->timer=0x1002;
    for(unsigned r=0;r<DCN302_HUBBUB_REGISTER_COUNT;r++)m->regs[r]=0xa596aa55u&~dcn302_hubbub_owned[r];
    for(unsigned p=0;p<5;p++){m->ctrl[p]=3;m->clock[p]=0xf00001;}
    *io=(dcn302_io){m,rd,wr,NULL};
}
static dcn302_dml_output request(void){return (dcn302_dml_output){.disp_khz=560000,.dpp_khz=560000,
    .urgent_ns=4000,.memory_trip_ns=4001,.stutter_enter_exit_ns=10000,.stutter_exit_ns=12000,
    .dram_change_ns=24000,.frac_urg_nom=123,.frac_urg_flip=998};}
static void prepare(model *m,dcn302_io *io,dcn302_hubbub_transaction *t){
    dcn302_reference ref;dcn302_dml_output o=request();
    CHECK(!dcn302_reference_read(io,100000,&ref));
    CHECK(!dcn302_hubbub_prepare(io,0,&ref,&o,t) && t->prepared && !t->dirty && !m->writes);
}
static void reference_tests(void){
    model m;dcn302_io io;dcn302_reference r,zero={0};
    CHECK(DCN302_HUBBUB_REF_BYTES==0x424 && DCN302_HUBBUB_TIMER_BYTES==0xe77c);
    for(unsigned div=0;div<16;div++){
        init(&m,&io);m.timer=0x1000|div;uint32_t xtal=div==2?100000:50000;
        CHECK(!dcn302_reference_read(&io,xtal,&r) && r.valid && r.khz==50000 && m.reads==4 && !m.writes);cases++;
        m.ref=2;CHECK(!dcn302_reference_read(&io,xtal,&r) && r.ref_control==2);cases++;
    }
    for(unsigned khz=40000;khz<=60000;khz+=10000){init(&m,&io);CHECK(!dcn302_reference_read(&io,khz*2,&r) && r.khz==khz);cases++;}
    for(unsigned fault=0;fault<14;fault++){
        init(&m,&io);uint32_t xtal=100000;
        switch(fault){case 0:m.ref=1;break;case 1:m.timer&=~0x1000u;break;case 2:xtal=79990;break;
        case 3:xtal=120010;break;case 4:xtal=9999;break;case 5:xtal=200010;break;case 6:xtal=100001;break;
        case 7:m.timer=0x1000;break;case 8:io.read=NULL;break;case 9:m.unstable_read=4;break;
        default:m.fail_read=fault-9;break;}
        memset(&r,0xa5,sizeof(r));CHECK(dcn302_reference_read(&io,xtal,&r)!=DCN302_HUBBUB_OK && !memcmp(&r,&zero,sizeof(r)) && !m.writes && !m.invalid);cases++;
    }
    CHECK(dcn302_reference_read(&io,100000,NULL)==DCN302_HUBBUB_INPUT);cases++;
    CHECK(dcn302_reference_read(&io,100000,(dcn302_reference *)((unsigned char *)&r+1))==DCN302_HUBBUB_INPUT);cases++;
    CHECK(dcn302_reference_read(&io,100000,(dcn302_reference *)&io)==DCN302_HUBBUB_INPUT);cases++;
    init(&m,&io);CHECK(!dcn302_reference_read(&io,100000,&r));dcn302_reference other=r;
    /* Explicit identity comparison is independent of tail padding. */
    for(unsigned n=offsetof(dcn302_reference,valid)+sizeof(r.valid);n<sizeof(r);n++)((unsigned char *)&other)[n]^=0xff;
    CHECK(dcn302_reference_equal(&r,&other));other.khz++;CHECK(!dcn302_reference_equal(&r,&other));cases++;
}
static void success(void){
    model m,old;dcn302_io io;dcn302_hubbub_transaction t;dcn302_reference ref;dcn302_dml_output o=request();
    CHECK(dcn302_hubbub_register_bytes[0]==0xe720 && dcn302_hubbub_register_bytes[1]==0xe724 &&
        dcn302_hubbub_register_bytes[24]==0xe814 && dcn302_hubbub_register_bytes[30]==0xe714);
    for(unsigned pipe=0;pipe<5;pipe++){
        init(&m,&io);old=m;CHECK(!dcn302_reference_read(&io,100000,&ref));
        CHECK(!dcn302_hubbub_prepare(&io,pipe,&ref,&o,&t) && !m.writes);
        CHECK(!dcn302_hubbub_apply_disabled(&io,&t) && t.applied && t.dirty && !t.poisoned && !m.invalid && m.log[0]==0);
        for(unsigned s=0;s<4;s++){
            unsigned at=1+7*s;
            CHECK((m.regs[at]&0x3fff3fff)==0x00c800c8 && (m.regs[at+1]&1023)==998 && (m.regs[at+2]&1023)==123);
            CHECK((m.regs[at+3]&0x3fff)==201 && m.regs[at+4]==0x01f401f4 && m.regs[at+5]==0x02580258 && m.regs[at+6]==0x04b004b0);
        }
        CHECK((m.regs[0]&0x33)==0x22 && m.regs[29]==3000 && (m.regs[30]&0x1ff000)==0x1ff000);
        CHECK(dcn302_hubbub_apply_disabled(&io,&t)==DCN302_HUBBUB_BUSY);
        unsigned first=m.log_count;CHECK(!dcn302_hubbub_restore_disabled(&io,&t) && !t.applied && !t.dirty && !t.poisoned);
        CHECK(m.log[first]==30 && m.log[m.log_count-1]==0 && !memcmp(m.regs,old.regs,sizeof(m.regs)));cases++;
    }
    init(&m,&io);prepare(&m,&io,&t);memcpy(m.regs,t.after,sizeof(m.regs));
    prepare(&m,&io,&t);CHECK(!dcn302_hubbub_apply_disabled(&io,&t) && t.applied && !t.dirty && !m.writes);
    CHECK(!dcn302_hubbub_restore_disabled(&io,&t) && !t.applied && !m.writes);cases++;
}
static void bounds(void){
    model m;dcn302_io io;dcn302_hubbub_transaction t;dcn302_reference ref;dcn302_dml_output o;
    for(unsigned field=0;field<5;field++)for(unsigned overflow=0;overflow<2;overflow++){
        init(&m,&io);CHECK(!dcn302_reference_read(&io,100000,&ref));o=request();
        uint32_t value=(field<2?16383u:65535u)*20u+overflow;
        switch(field){case 0:o.urgent_ns=value;break;case 1:o.memory_trip_ns=value;break;
        case 2:o.stutter_enter_exit_ns=value;break;case 3:o.stutter_exit_ns=value;break;case 4:o.dram_change_ns=value;break;}
        CHECK(dcn302_hubbub_prepare(&io,0,&ref,&o,&t)==(overflow?DCN302_HUBBUB_RANGE:DCN302_HUBBUB_OK) && t.prepared==!overflow && !m.writes);cases++;
    }
    for(unsigned f=0;f<2;f++)for(unsigned value=0;value<=1001;value++){
        init(&m,&io);CHECK(!dcn302_reference_read(&io,100000,&ref));o=request();
        if(f)o.frac_urg_flip=value;else o.frac_urg_nom=value;
        CHECK(dcn302_hubbub_prepare(&io,0,&ref,&o,&t)==(value>1000?DCN302_HUBBUB_INPUT:DCN302_HUBBUB_OK) && !m.writes);cases++;
    }
    for(unsigned fault=0;fault<9;fault++){
        init(&m,&io);CHECK(!dcn302_reference_read(&io,100000,&ref));o=request();
        switch(fault){case 0:o.urgent_ns=0;break;case 1:o.memory_trip_ns=0;break;case 2:o.disp_khz=0;break;case 3:o.dpp_khz=0;break;
        case 4:ref.valid=false;break;case 5:ref.khz++;break;case 6:o.urgent_ns=UINT32_MAX;break;case 7:io.write=NULL;break;case 8:io.read=NULL;break;}
        CHECK(dcn302_hubbub_prepare(&io,0,&ref,&o,&t)!=DCN302_HUBBUB_OK && !t.prepared && !m.writes);cases++;
    }
    init(&m,&io);CHECK(!dcn302_reference_read(&io,100000,&ref));o=request();
    CHECK(dcn302_hubbub_prepare(&io,5,&ref,&o,&t)==DCN302_HUBBUB_INPUT);
    CHECK(dcn302_hubbub_prepare(&io,0,&ref,&o,NULL)==DCN302_HUBBUB_INPUT);
    CHECK(dcn302_hubbub_prepare(&io,0,&t.reference,&o,&t)==DCN302_HUBBUB_INPUT);
    CHECK(dcn302_hubbub_prepare(&t.owner,0,&ref,&o,&t)==DCN302_HUBBUB_INPUT);
    CHECK(dcn302_hubbub_prepare(&io,0,&ref,&t.request,&t)==DCN302_HUBBUB_INPUT);
    CHECK(dcn302_hubbub_prepare(&io,0,(dcn302_reference *)(UINTPTR_MAX-1),&o,&t)==DCN302_HUBBUB_INPUT);cases++;
    unsigned char *bytes=malloc(sizeof(t)+64);CHECK(bytes);
    CHECK(dcn302_hubbub_prepare(&io,0,(dcn302_reference *)(bytes+24),&o,(dcn302_hubbub_transaction *)(bytes+32))==DCN302_HUBBUB_INPUT);free(bytes);cases++;
    init(&m,&io);prepare(&m,&io,&t);unsigned count=m.reads-4;
    for(unsigned n=1;n<=count;n++){
        init(&m,&io);CHECK(!dcn302_reference_read(&io,100000,&ref));m.fail_read=m.reads+n;
        CHECK(dcn302_hubbub_prepare(&io,0,&ref,&o,&t)==DCN302_HUBBUB_IO && !t.prepared && !m.writes);cases++;
    }
    init(&m,&io);CHECK(!dcn302_reference_read(&io,100000,&ref));m.unstable_read=m.reads+4+31+1;
    CHECK(dcn302_hubbub_prepare(&io,0,&ref,&o,&t)==DCN302_HUBBUB_READBACK && !t.prepared && !m.writes);cases++;
}
static void conditions(void){
    model m;dcn302_io io;dcn302_hubbub_transaction t;
    for(unsigned fault=0;fault<39;fault++){
        init(&m,&io);prepare(&m,&io,&t);
        if(fault<20){unsigned p=fault/4;switch(fault%4){case 0:m.otg[p][0]=DCN302_MASTER_ENABLE_MASK;break;
        case 1:m.otg[p][0]=DCN302_MASTER_ACTIVE_MASK;break;case 2:m.otg[p][1]=DCN302_BUSY_MASK;break;case 3:m.otg[p][2]=DCN302_VTG_ENABLE_MASK;break;}}
        else switch(fault){case 20:m.ctrl[0]&=~1u;break;case 21:m.ctrl[0]&=~2u;break;case 22:m.ctrl[0]|=4;break;
        case 23:m.ctrl[0]|=0x1000;break;case 24:m.ctrl[0]|=0x100000;break;case 25:m.ctrl[0]|=0x10000000;break;
        case 26:m.clock[0]=0;break;case 27:m.ref=1;break;case 28:m.timer^=0x10000;break;case 29:m.regs[1]^=1;break;
        case 30:m.regs[1]^=0x80000000;break;case 31:t.after[1]^=1;break;case 32:t.prepared=false;break;
        case 33:t.touched=UINT64_C(1)<<63;break;case 34:t.hubp=5;break;case 35:io.context=&t;break;
        case 36:t.request.frac_urg_nom=1001;break;case 37:t.reference.khz++;break;case 38:t.reference.valid=false;break;}
        CHECK(dcn302_hubbub_apply_disabled(&io,&t)!=DCN302_HUBBUB_OK && !m.writes && !t.dirty && !m.invalid);cases++;
    }
}
static void failures(void){
    model m,old;dcn302_io io;dcn302_hubbub_transaction t;
    init(&m,&io);prepare(&m,&io,&t);unsigned start=m.reads;
    CHECK(!dcn302_hubbub_apply_disabled(&io,&t));unsigned reads=m.reads-start,writes=m.writes;
    for(unsigned n=1;n<=reads;n++){
        init(&m,&io);prepare(&m,&io,&t);old=m;m.fail_read=m.reads+n;
        CHECK(dcn302_hubbub_apply_disabled(&io,&t)!=DCN302_HUBBUB_OK && !t.applied);
        CHECK(!t.dirty && !t.poisoned && !m.invalid && !memcmp(m.regs,old.regs,sizeof(m.regs)));cases++;
    }
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=writes;n++){
        init(&m,&io);prepare(&m,&io,&t);old=m;
        if(kind==2)m.ignore_write=n;else{m.fail_write=n;m.posted=kind==1;}
        CHECK(dcn302_hubbub_apply_disabled(&io,&t)!=DCN302_HUBBUB_OK && !t.applied);
        CHECK(!t.dirty && !t.poisoned && !m.invalid && !memcmp(m.regs,old.regs,sizeof(m.regs)));cases++;
    }
    for(unsigned fault=0;fault<3;fault++){
        init(&m,&io);prepare(&m,&io,&t);old=m;
        if(fault==0)m.activate_write=1;else if(fault==1)m.clock_loss_write=1;else m.ref_change_write=1;
        CHECK(dcn302_hubbub_apply_disabled(&io,&t)==DCN302_HUBBUB_ROLLBACK && t.poisoned && t.dirty && m.writes==1);
        CHECK(dcn302_hubbub_restore_disabled(&io,&t)==DCN302_HUBBUB_ROLLBACK && m.writes==1);
        m.otg[4][0]=0;m.clock[0]=0xf00001;m.timer=old.timer;
        CHECK(!dcn302_hubbub_restore_disabled(&io,&t) && !t.dirty && !t.poisoned && !memcmp(m.regs,old.regs,sizeof(m.regs)));cases++;
    }
    init(&m,&io);prepare(&m,&io,&t);CHECK(!dcn302_hubbub_apply_disabled(&io,&t));start=m.reads;unsigned w=m.writes;
    CHECK(!dcn302_hubbub_restore_disabled(&io,&t));reads=m.reads-start;writes=m.writes-w;
    for(unsigned n=1;n<=reads;n++){
        init(&m,&io);prepare(&m,&io,&t);CHECK(!dcn302_hubbub_apply_disabled(&io,&t));m.fail_read=m.reads+n;
        CHECK(dcn302_hubbub_restore_disabled(&io,&t)==DCN302_HUBBUB_ROLLBACK && t.dirty && t.poisoned && !m.invalid);
        CHECK(!dcn302_hubbub_restore_disabled(&io,&t) && !t.dirty && !t.poisoned);cases++;
    }
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=writes;n++){
        init(&m,&io);prepare(&m,&io,&t);CHECK(!dcn302_hubbub_apply_disabled(&io,&t));
        if(kind==2)m.ignore_write=m.writes+n;else{m.fail_write=m.writes+n;m.posted=kind==1;}
        CHECK(dcn302_hubbub_restore_disabled(&io,&t)==DCN302_HUBBUB_ROLLBACK && t.dirty && t.poisoned && !m.invalid);
        CHECK(!dcn302_hubbub_restore_disabled(&io,&t) && !t.dirty && !t.poisoned);cases++;
    }
}
int main(void){reference_tests();success();bounds();conditions();failures();printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"native_reference_clock_decode\":true,\"native_watermark_writes\":true,\"policy_first_restore_last\":true,\"posted_failure_rollback\":true,\"physical_hardware_tested\":false}\n",cases,checks);return 0;}
