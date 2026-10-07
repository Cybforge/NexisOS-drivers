#include "../../tools/gpu-driver/amd/dcn302_hubp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
#define CTRL DCN302_HUBP_R_DCHUBP_CNTL
#define BLANK (DCN302_HUBP_HUBP_BLANK_EN_MASK|DCN302_HUBP_HUBP_TTU_DISABLE_MASK)
#define CLOCKS 0xf00001u
static unsigned cases,checks;
typedef struct {
    uint32_t regs[5][DCN302_HUBP_REGISTER_COUNT],clocks[5],otg[5][3];
    uint64_t time,completion;
    unsigned reads,writes,delays,fail_read,fail_write,ignore_write,fail_delay,activate_write,drop_clock_write,unstable_reads;
    bool invalid,posted,stuck,frozen,reverse,status_change,unstable;
} model;
static void complete(model *m){
    if(!m->stuck && m->time>=m->completion)for(unsigned p=0;p<5;p++)
        m->regs[p][CTRL]|=2;
}
static bool read_reg(void *ctx,uint32_t addr,uint32_t *value){
    model *m=ctx;complete(m);unsigned n=++m->reads;if(n==m->fail_read)return false;
    for(unsigned p=0;p<5;p++){
        if(addr==dcn302_hubp_clock_bytes[p]){*value=m->clocks[p];return true;}
        for(unsigned r=0;r<DCN302_HUBP_REGISTER_COUNT;r++)if(addr==dcn302_hubp_register_bytes[p][r]){
            *value=m->regs[p][r];
            if(m->status_change)*value^=(n&1)?dcn302_hubp_readonly[r]|dcn302_hubp_forbidden[r]:0;
            if(m->unstable && r==0)*value^=(++m->unstable_reads)&1;
            return true;
        }
        unsigned indices[]={DCN302_R_CONTROL,DCN302_R_CLOCK,DCN302_R_VTG};
        for(unsigned r=0;r<3;r++)if(addr==dcn302_register_bytes[p][indices[r]]){*value=m->otg[p][r];return true;}
    }
    m->invalid=true;return false;
}
static bool write_reg(void *ctx,uint32_t addr,uint32_t value){
    model *m=ctx;unsigned n=++m->writes;
    for(unsigned p=0;p<5;p++)for(unsigned r=0;r<DCN302_HUBP_REGISTER_COUNT;r++)if(addr==dcn302_hubp_register_bytes[p][r]){
        CHECK(!(value&(dcn302_hubp_forbidden[r]|dcn302_hubp_readonly[r])));
        uint32_t mask=r==CTRL?BLANK|dcn302_hubp_owned[r]:dcn302_hubp_owned[r];
        CHECK(!((value^m->regs[p][r])&~(mask|dcn302_hubp_forbidden[r]|dcn302_hubp_readonly[r])));
        if(r!=CTRL || !((value^m->regs[p][r])&BLANK)){
            CHECK((m->clocks[p]&CLOCKS)==CLOCKS && (m->regs[p][CTRL]&(BLANK|2))==3);
            for(unsigned pipe=0;pipe<5;pipe++)CHECK(!(m->otg[pipe][0]&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) &&
                !(m->otg[pipe][1]&DCN302_BUSY_MASK) && !(m->otg[pipe][2]&DCN302_VTG_ENABLE_MASK));
        }
        if(n==m->ignore_write)return true;
        if(n==m->fail_write && !m->posted)return false;
        uint32_t ro=dcn302_hubp_readonly[r];m->regs[p][r]=(m->regs[p][r]&ro)|(value&~ro);
        if(r==CTRL && (value&BLANK)==1)CHECK(m->regs[p][r]&2);
        if(n==m->activate_write)m->otg[4][0]|=DCN302_MASTER_ACTIVE_MASK;
        if(n==m->drop_clock_write)m->clocks[p]=0;
        return n!=m->fail_write;
    }
    m->invalid=true;return false;
}
static bool delay(void *ctx,uint32_t us){model *m=ctx;CHECK(us==1);if(++m->delays==m->fail_delay)return false;if(!m->frozen)m->time+=us;complete(m);return true;}
static uint64_t now(void *ctx){model *m=ctx;return m->reverse?m->time--:m->time;}
static void init(model *m,dcn302_io *io){
    memset(m,0,sizeof(*m));m->time=100000;
    for(unsigned p=0;p<5;p++){
        m->clocks[p]=CLOCKS;
        for(unsigned r=0;r<DCN302_HUBP_REGISTER_COUNT;r++)m->regs[p][r]=0xa596aa55u&~(dcn302_hubp_owned[r]|dcn302_hubp_readonly[r]|dcn302_hubp_forbidden[r]);
        m->regs[p][CTRL]&=~(BLANK|DCN302_HUBP_HUBP_DISABLE_MASK);
        m->regs[p][CTRL]|=3;
        /* Other status bits and W1C readback must never enter a write. */
        m->regs[p][DCN302_HUBP_R_DCN_DMDATA_VM_CNTL]|=dcn302_hubp_readonly[DCN302_HUBP_R_DCN_DMDATA_VM_CNTL]|dcn302_hubp_forbidden[DCN302_HUBP_R_DCN_DMDATA_VM_CNTL];
    }
    *io=(dcn302_io){m,read_reg,write_reg,delay};
}
static nexis_gpu_timing timing(void){return (nexis_gpu_timing){558100,1920,1968,2032,2080,1080,1083,1088,1118,3};}
static dcn302_dml_output request(void){
    dcn302_dml_output o={.disp_khz=560000,.dpp_khz=560000,.vstartup=30,.vready_offset=100,.vupdate_offset=100,.vupdate_width=100};
    for(unsigned n=0;n<DCN302_HUBP_FIELD_COUNT;n++){
        const dcn302_hubp_field *f=&dcn302_hubp_fields[n];uint32_t v=(f->mask>>f->shift)/2+1;
        memcpy((unsigned char *)&o+f->offset,&v,sizeof(v));
    }
    return o;
}
static bool same(const model *a,const model *b){
    for(unsigned p=0;p<5;p++)for(unsigned r=0;r<DCN302_HUBP_REGISTER_COUNT;r++)
        if((a->regs[p][r]^b->regs[p][r])&~(dcn302_hubp_forbidden[r]|dcn302_hubp_readonly[r]))return false;
    return true;
}
static void success(void){
    model m,old;dcn302_io io;dcn302_hubp_transaction t;nexis_gpu_timing shape=timing();dcn302_dml_output o=request();
    /* Independent real Navi23 addresses, including different register prefixes. */
    CHECK(dcn302_hubp_register_bytes[0][DCN302_HUBP_R_HUBPRET_CONTROL]==0xecb0 &&
        dcn302_hubp_register_bytes[4][DCN302_HUBP_R_DCN_GLOBAL_TTU_CNTL]==0xf96c &&
        dcn302_hubp_register_bytes[2][DCN302_HUBP_R_DCHUBP_CNTL]==0xf1ac);
    for(unsigned p=0;p<5;p++){
        init(&m,&io);old=m;
        CHECK(!dcn302_hubp_prepare(&io,p,&shape,&o,&t) && t.prepared && !m.writes);
        CHECK(!dcn302_hubp_apply_disabled(&io,&t) && t.applied && t.dirty && !t.poisoned && !m.invalid);
        for(unsigned n=0;n<DCN302_HUBP_FIELD_COUNT;n++){
            const dcn302_hubp_field *f=&dcn302_hubp_fields[n];uint32_t v;memcpy(&v,(unsigned char *)&o+f->offset,sizeof(v));
            CHECK(((m.regs[p][f->reg]&f->mask)>>f->shift)==v);
        }
        CHECK(m.regs[p][CTRL]&0x100);CHECK((m.regs[p][CTRL]&BLANK)==1);
        CHECK(dcn302_hubp_apply_disabled(&io,&t)==DCN302_HUBP_BUSY);
        CHECK(!dcn302_hubp_restore_disabled(&io,&t) && !t.applied && !t.dirty && !t.poisoned && same(&m,&old));cases++;
    }
    /* Both branches and exact boundary of AMD's VREADY calculation. */
    for(unsigned delta=0;delta<3;delta++){
        init(&m,&io);o=request();o.vstartup=36;o.vupdate_width=o.vupdate_offset=0;o.vready_offset=shape.htotal*delta;
        CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t));
        CHECK(!!(t.after[CTRL]&0x100)==(delta>=1)); /* vblank_end=35; subtract >=1 */
        o.vready_offset=0;o.vupdate_width=o.vupdate_offset=0;
        CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t) && !(t.after[CTRL]&0x100));cases++;
    }
    /* Mutable DMDATA read status and W1C bits are excluded from snapshots. */
    init(&m,&io);m.status_change=true;o=request();CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t));
    /* Stop status changing at CTRL while applying: NO_OUTSTANDING is required. */
    m.status_change=false;CHECK(!dcn302_hubp_apply_disabled(&io,&t));CHECK(!dcn302_hubp_restore_disabled(&io,&t));cases++;
}
static void bounds(void){
    model m;dcn302_io io;dcn302_hubp_transaction t;nexis_gpu_timing shape=timing();dcn302_dml_output o;
    for(unsigned n=0;n<DCN302_HUBP_FIELD_COUNT;n++)for(unsigned overflow=0;overflow<2;overflow++){
        init(&m,&io);o=request();const dcn302_hubp_field *f=&dcn302_hubp_fields[n];uint32_t v=(f->mask>>f->shift)+overflow;
        memcpy((unsigned char *)&o+f->offset,&v,sizeof(v));
        enum dcn302_hubp_error e=dcn302_hubp_prepare(&io,n%5,&shape,&o,&t);
        CHECK(e==(overflow?DCN302_HUBP_RANGE:DCN302_HUBP_OK) && !m.writes && t.prepared==!overflow);cases++;
    }
    for(unsigned fault=0;fault<17;fault++){
        init(&m,&io);shape=timing();o=request();
        switch(fault){case 0:shape.htotal=0;break;case 1:shape.vsync_start=0;break;case 2:shape.hactive=5000;break;
            case 3:shape.pixel_khz=600001;break;case 4:shape.flags=4;break;case 5:o.vstartup=0;break;
            case 6:o.vstartup=shape.vtotal-shape.vactive;break;case 7:o.vready_offset=UINT32_MAX;break;
            case 8:o.vready_offset=o.vupdate_width=o.vupdate_offset=UINT32_MAX;break;
            case 9:io.read=NULL;break;case 10:io.write=NULL;break;case 11:m.clocks[0]=0;break;
            case 12:m.clocks[0]&=~1u;break;case 13:o.disp_khz=0;break;case 14:o.dpp_khz=0;break;
            case 15:o.disp_khz=UINT32_MAX;break;case 16:o.dpp_khz=UINT32_MAX;break;}
        CHECK(dcn302_hubp_prepare(&io,0,&shape,&o,&t)!=DCN302_HUBP_OK && !m.writes && !t.prepared);cases++;
    }
    init(&m,&io);shape=timing();o=request();CHECK(dcn302_hubp_prepare(&io,5,&shape,&o,&t)==DCN302_HUBP_INPUT);cases++;
    CHECK(dcn302_hubp_prepare(&io,0,&shape,&o,NULL)==DCN302_HUBP_INPUT);cases++;
    CHECK(dcn302_hubp_prepare(&io,0,&t.timing,&o,&t)==DCN302_HUBP_INPUT);cases++;
    CHECK(dcn302_hubp_prepare(&io,0,&shape,&t.request,&t)==DCN302_HUBP_INPUT);cases++;
    CHECK(dcn302_hubp_prepare(&t.owner,0,&shape,&o,&t)==DCN302_HUBP_INPUT);cases++;
    /* Partial overlap where the input starts before the output and pointer wrap. */
    unsigned char *storage=malloc(sizeof(t)+64);CHECK(storage);
    CHECK(dcn302_hubp_prepare(&io,0,(nexis_gpu_timing *)(storage+24),&o,(dcn302_hubp_transaction *)(storage+32))==DCN302_HUBP_INPUT);free(storage);cases++;
    CHECK(dcn302_hubp_prepare(&io,0,(nexis_gpu_timing *)(UINTPTR_MAX-2),&o,&t)==DCN302_HUBP_INPUT);cases++;
    CHECK(dcn302_hubp_prepare(&io,0,&shape,(dcn302_dml_output *)((unsigned char *)&o+1),&t)==DCN302_HUBP_INPUT);cases++;
    CHECK(dcn302_hubp_prepare(&io,0,&shape,&o,(dcn302_hubp_transaction *)((unsigned char *)&t+1))==DCN302_HUBP_INPUT);cases++;
    init(&m,&io);m.unstable=true;CHECK(dcn302_hubp_prepare(&io,0,&shape,&o,&t)==DCN302_HUBP_READBACK && !m.writes);cases++;
    init(&m,&io);CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t));unsigned reads=m.reads;
    for(unsigned n=1;n<=reads;n++){init(&m,&io);m.fail_read=n;CHECK(dcn302_hubp_prepare(&io,0,&shape,&o,&t)==DCN302_HUBP_IO && !m.writes && !t.prepared);cases++;}
}
static void preconditions(void){
    model m;dcn302_io io;dcn302_hubp_transaction t;nexis_gpu_timing shape=timing();dcn302_dml_output o=request();
    for(unsigned fault=0;fault<34;fault++){
        init(&m,&io);CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t));
        if(fault<20){unsigned p=fault/4;switch(fault%4){case 0:m.otg[p][0]|=DCN302_MASTER_ENABLE_MASK;break;
            case 1:m.otg[p][0]|=DCN302_MASTER_ACTIVE_MASK;break;case 2:m.otg[p][1]|=DCN302_BUSY_MASK;break;case 3:m.otg[p][2]|=DCN302_VTG_ENABLE_MASK;break;}}
        else switch(fault){case 20:m.regs[0][CTRL]&=~1u;break;case 21:m.regs[0][CTRL]|=0x1000u;break;case 22:m.stuck=true;m.regs[0][CTRL]&=~2u;break;
            case 23:m.regs[0][CTRL]|=4;break;case 24:m.regs[0][CTRL]|=0x10000000u;break;case 25:m.regs[0][CTRL]|=0x100000u;break;
            case 26:m.clocks[0]=0;break;case 27:m.regs[0][0]^=1;break;case 28:t.after[0]^=1;break;
            case 29:t.prepared=false;break;case 30:t.touched=UINT64_C(1)<<63;break;case 31:io.context=&t;break;
            case 32:t.timing.htotal=0;break;case 33:t.hubp=5;break;}
        CHECK(dcn302_hubp_apply_disabled(&io,&t)!=DCN302_HUBP_OK && !m.writes && !t.dirty && !m.invalid);cases++;
    }
}
static void failures(void){
    model m,old;dcn302_io io;dcn302_hubp_transaction t;nexis_gpu_timing shape=timing();dcn302_dml_output o=request();
    init(&m,&io);CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t));unsigned start=m.reads;
    CHECK(!dcn302_hubp_apply_disabled(&io,&t));unsigned reads=m.reads-start,writes=m.writes;
    for(unsigned n=1;n<=reads;n++){
        init(&m,&io);CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t));old=m;m.fail_read=m.reads+n;
        CHECK(dcn302_hubp_apply_disabled(&io,&t)!=DCN302_HUBP_OK && !t.applied);
        CHECK(!t.poisoned && !t.dirty && same(&m,&old) && !m.invalid);cases++;
    }
    for(unsigned mode=0;mode<3;mode++)for(unsigned n=1;n<=writes;n++){
        init(&m,&io);CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t));old=m;
        if(mode==2)m.ignore_write=n;else{m.fail_write=n;m.posted=mode==1;}
        CHECK(dcn302_hubp_apply_disabled(&io,&t)!=DCN302_HUBP_OK && !t.applied);
        CHECK(!t.poisoned && !t.dirty && same(&m,&old) && !m.invalid);cases++;
    }
    for(unsigned fault=0;fault<2;fault++){
        init(&m,&io);CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t));
        if(fault)m.drop_clock_write=1;else m.activate_write=1;
        CHECK(dcn302_hubp_apply_disabled(&io,&t)==DCN302_HUBP_ROLLBACK && t.poisoned && t.dirty && m.writes==1);
        CHECK((m.regs[0][CTRL]&BLANK)==1);
        unsigned before=m.writes;CHECK(dcn302_hubp_restore_disabled(&io,&t)==DCN302_HUBP_ROLLBACK && m.writes==before);
        m.otg[4][0]=0;m.clocks[0]=CLOCKS;CHECK(!dcn302_hubp_restore_disabled(&io,&t) && !t.poisoned && !t.dirty);cases++;
    }
    init(&m,&io);CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t));CHECK(!dcn302_hubp_apply_disabled(&io,&t));
    start=m.reads;unsigned write_start=m.writes;CHECK(!dcn302_hubp_restore_disabled(&io,&t));reads=m.reads-start;writes=m.writes-write_start;
    for(unsigned n=1;n<=reads;n++){
        init(&m,&io);CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t));CHECK(!dcn302_hubp_apply_disabled(&io,&t));m.fail_read=m.reads+n;
        CHECK(dcn302_hubp_restore_disabled(&io,&t)==DCN302_HUBP_ROLLBACK && t.poisoned && t.dirty && !m.invalid);
        CHECK(!dcn302_hubp_restore_disabled(&io,&t) && !t.dirty && !t.poisoned);cases++;
    }
    for(unsigned mode=0;mode<3;mode++)for(unsigned n=1;n<=writes;n++){
        init(&m,&io);CHECK(!dcn302_hubp_prepare(&io,0,&shape,&o,&t));CHECK(!dcn302_hubp_apply_disabled(&io,&t));
        if(mode==2)m.ignore_write=m.writes+n;else{m.fail_write=m.writes+n;m.posted=mode==1;}
        CHECK(dcn302_hubp_restore_disabled(&io,&t)==DCN302_HUBP_ROLLBACK && t.poisoned && t.dirty && !m.invalid);
        CHECK(!dcn302_hubp_restore_disabled(&io,&t) && !t.dirty && !t.poisoned);cases++;
    }
}
static void blank(void){
    model m;dcn302_io io;
    init(&m,&io);m.regs[0][CTRL]&=~(BLANK|2);m.completion=m.time+5;m.otg[0][0]=DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK;
    CHECK(!dcn302_hubp_blank(&io,0,now) && (m.regs[0][CTRL]&(BLANK|2))==3 && m.delays==5 && m.writes==1 && !m.invalid);cases++;
    for(unsigned fault=0;fault<9;fault++){
        init(&m,&io);m.regs[0][CTRL]&=~(BLANK|2);m.completion=m.time+5;
        switch(fault){case 0:m.stuck=true;break;case 1:m.frozen=true;break;case 2:m.reverse=true;break;
            case 3:m.fail_delay=1;break;case 4:m.fail_write=1;m.posted=true;break;case 5:m.ignore_write=1;break;
            case 6:m.clocks[0]=0;break;case 7:m.regs[0][CTRL]|=4;break;case 8:io.delay_us=NULL;break;}
        CHECK(dcn302_hubp_blank(&io,0,now)!=DCN302_HUBP_OK && !m.invalid && m.writes<=1 && m.delays<=100000);
        if(fault<4)CHECK(!m.writes && !(m.regs[0][CTRL]&1));
        if(fault==4)CHECK((m.regs[0][CTRL]&BLANK)==1);
        cases++;
    }
}
int main(void){success();bounds();preconditions();failures();blank();printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"physical_hardware_tested\":false}\n",cases,checks);return 0;}
