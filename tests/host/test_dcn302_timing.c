#include "../../tools/gpu-driver/amd/dcn302_timing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases,checks;
#define GL0 DCN302_TIMING_R_GLOBAL0
#define GL1 DCN302_TIMING_R_GLOBAL1
#define GL2 DCN302_R_GLOBAL2
#define LOCK DCN302_R_LOCK
#define DBUF DCN302_TIMING_R_DBUF
typedef struct {
    uint32_t regs[5][DCN302_TIMING_REGISTER_COUNT];
    unsigned pipe,reads,writes,guards,delays,fail_read,fail_write,ignore_write,fail_guard,fail_delay;
    unsigned activate_write,clock_loss_write,guard_loss_write,unstable_read;
    unsigned log[160],log_count,pending_ticks,lock_ticks,unlock_ticks;
    uint64_t time;bool posted,invalid,guard_ok,stuck_lock,stuck_unlock,stuck_pending,frozen,backwards;
} model;
static bool rd(void *context,uint32_t address,uint32_t *out){
    model *m=context;unsigned n=++m->reads;if(n==m->fail_read)return false;
    for(unsigned p=0;p<5;p++)for(unsigned r=0;r<DCN302_TIMING_REGISTER_COUNT;r++)if(address==dcn302_timing_register_bytes[p][r]){
        *out=m->regs[p][r];if(n==m->unstable_read)*out^=1;return true;
    }
    m->invalid=true;return false;
}
static bool guard(void *context){model *m=context;return m->guard_ok && ++m->guards!=m->fail_guard;}
static uint64_t now(void *context){model *m=context;if(m->backwards && m->delays)return m->time-2;return m->time;}
static bool delay(void *context,uint32_t us){
    model *m=context;unsigned n=++m->delays;if(n==m->fail_delay)return false;
    if(!m->frozen)m->time+=us;
    uint32_t *r=m->regs[m->pipe];
    if((r[LOCK]&1) && !(r[LOCK]&DCN302_LOCK_STATUS_MASK) && !m->stuck_lock && m->lock_ticks && !--m->lock_ticks)r[LOCK]|=DCN302_LOCK_STATUS_MASK;
    if(!(r[LOCK]&1) && (r[LOCK]&DCN302_LOCK_STATUS_MASK) && !m->stuck_unlock && m->unlock_ticks && !--m->unlock_ticks)r[LOCK]&=~DCN302_LOCK_STATUS_MASK;
    if(!m->stuck_pending && m->pending_ticks && !--m->pending_ticks)r[DBUF]&=~(DCN302_TIMING_DBUF_PENDING_MASK|DCN302_TIMING_DBUF_INSTANT_MASK);
    return true;
}
static bool wr(void *context,uint32_t address,uint32_t value){
    model *m=context;unsigned n=++m->writes;uint32_t *regs=m->regs[m->pipe];
    for(unsigned r=0;r<DCN302_TIMING_REGISTER_COUNT;r++)if(address==dcn302_timing_register_bytes[m->pipe][r]){
        CHECK(dcn302_timing_owned[r] && !(value&dcn302_timing_write_excluded[r]));
        CHECK(!((value^regs[r])&~(dcn302_timing_owned[r]|dcn302_timing_write_excluded[r])));
        CHECK(m->guard_ok);
        for(unsigned p=0;p<5;p++)CHECK(!(m->regs[p][DCN302_R_CONTROL]&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) &&
            !(m->regs[p][DCN302_R_CLOCK]&DCN302_BUSY_MASK) && !(m->regs[p][DCN302_R_VTG]&DCN302_VTG_ENABLE_MASK));
        CHECK(m->log_count<160);m->log[m->log_count++]=r;
        if(r!=DBUF && r!=GL0 && r!=GL1 && r!=GL2 && r!=LOCK){
            CHECK((regs[LOCK]&(DCN302_LOCK_MASK|DCN302_LOCK_STATUS_MASK))==(DCN302_LOCK_MASK|DCN302_LOCK_STATUS_MASK));
            CHECK((regs[GL2]&dcn302_timing_owned[GL2])==(m->pipe<<DCN302_TIMING_LOCK_SELECT_SHIFT));
            CHECK(!(regs[DBUF]&DCN302_TIMING_DRR_MODE_MASK) && !(regs[GL0]&dcn302_timing_owned[GL0]) && !(regs[GL1]&dcn302_timing_owned[GL1]));
        }
        if(r==GL2 && (regs[LOCK]&(DCN302_LOCK_MASK|DCN302_LOCK_STATUS_MASK)))CHECK(!((value^regs[GL2])&dcn302_timing_owned[GL2]));
        if(n==m->ignore_write)return true;
        if(n==m->fail_write && !m->posted)return false;
        uint32_t ro=regs[r]&dcn302_timing_write_excluded[r];regs[r]=value|ro;
        if(r==LOCK){
            if(value&1){if(!m->stuck_lock && !m->lock_ticks)regs[r]|=DCN302_LOCK_STATUS_MASK;}
            else{if(!m->stuck_unlock && !m->unlock_ticks)regs[r]&=~DCN302_LOCK_STATUS_MASK;}
        }
        if(n==m->activate_write)m->regs[4][DCN302_R_CONTROL]|=DCN302_MASTER_ACTIVE_MASK;
        if(n==m->clock_loss_write)regs[DCN302_R_CLOCK]&=~DCN302_CLOCK_ON_MASK;
        if(n==m->guard_loss_write)m->guard_ok=false;
        return n!=m->fail_write;
    }
    m->invalid=true;return false;
}
static nexis_gpu_timing timing(void){return (nexis_gpu_timing){558100,1920,1940,1972,2080,1080,1083,1088,1118,3};}
static dcn302_sync sync_request(void){return (dcn302_sync){.vstartup=27,.vupdate_offset=6,.vupdate_width=13,.vready=16,.display_port=false};}
static void init(model *m,dcn302_io *io,unsigned pipe){
    memset(m,0,sizeof(*m));m->pipe=pipe;m->guard_ok=true;m->time=100;
    for(unsigned p=0;p<5;p++){
        uint32_t *r=m->regs[p];
        for(unsigned n=0;n<DCN302_TIMING_REGISTER_COUNT;n++)r[n]=0xa596aa55u&~(dcn302_timing_owned[n]|dcn302_timing_excluded[n]);
        r[DCN302_R_CLOCK]=DCN302_CLOCK_ENABLE_MASK|DCN302_CLOCK_ON_MASK;
        r[DCN302_R_SOURCE]=p<<DCN302_SEG0_SHIFT;r[DCN302_R_H_DIV]=r[DCN302_R_FORMAT]=r[DCN302_R_INTERLACE]=0;
        r[DCN302_R_H_TOTAL]=2199;r[DCN302_R_H_BLANK]=192u<<16|2112;r[DCN302_R_H_SYNC]=44u<<16;r[DCN302_R_H_POL]=0;
        r[DCN302_R_V_TOTAL]=r[DCN302_R_V_MIN]=r[DCN302_R_V_MAX]=1124;
        r[DCN302_R_V_BLANK]=41u<<16|1121;r[DCN302_R_V_SYNC]=5u<<16;r[DCN302_R_V_POL]=0;
        r[DCN302_R_V_CONTROL]=0xffffffffu;r[DCN302_R_V_STARTUP]=30;r[DCN302_R_V_UPDATE]=1u<<16|2;
        r[DCN302_R_V_READY]=3;r[DCN302_R_VTG]=41u<<16|1121;
        r[LOCK]=0xa5a50000;r[GL0]=0x8014000a;r[GL1]=0x801e0014;r[GL2]=0xa0000400|((p+1)%5)<<25;
        r[DBUF]=0x02800800;r[DCN302_TIMING_R_GLOBAL4]=0x00150019;
    }
    *io=(dcn302_io){m,rd,wr,delay};
}
static void prepare(model *m,dcn302_io *io,dcn302_timing_transaction *t){
    nexis_gpu_timing mode=timing();dcn302_sync s=sync_request();
    CHECK(!dcn302_timing_prepare(io,m->pipe,&mode,&s,guard,now,t) && t->prepared && !t->dirty && !m->writes);
}
static bool same_regs(const model *m,const model *old){return !memcmp(m->regs,old->regs,sizeof(m->regs));}
static void success(void){
    CHECK(dcn302_timing_register_bytes[0][GL2]==0x14148 && dcn302_timing_register_bytes[4][DBUF]==0x1486c);
    model m,old;dcn302_io io;dcn302_timing_transaction t;
    for(unsigned p=0;p<5;p++){
        init(&m,&io,p);old=m;prepare(&m,&io,&t);
        CHECK(!dcn302_timing_apply_disabled(&io,&t) && t.applied && t.dirty && !t.poisoned && !m.invalid);
        uint32_t *r=m.regs[p];
        CHECK(r[DCN302_R_H_TOTAL]==2079 && r[DCN302_R_V_TOTAL]==1117 && r[DCN302_R_V_MIN]==1117 && r[DCN302_R_V_MAX]==1117);
        CHECK(r[DCN302_R_H_BLANK]==(140u<<16|2060) && r[DCN302_R_V_BLANK]==(35u<<16|1115));
        CHECK(r[DCN302_R_H_SYNC]==32u<<16 && r[DCN302_R_V_SYNC]==5u<<16);
        CHECK((r[DCN302_R_V_STARTUP]&1023)==27 && (r[DCN302_R_V_UPDATE]&0x3ffffff)==(13u<<16|6) && (r[DCN302_R_V_READY]&0xffff)==16);
        CHECK(r[DCN302_R_V_CONTROL]==0x60 && !(r[DCN302_R_VTG]&DCN302_VTG_ENABLE_MASK) && !(r[LOCK]&0x101));
        CHECK((r[GL2]&dcn302_timing_owned[GL2])==(old.regs[p][GL2]&DCN302_TIMING_LOCK_SELECT_MASK));
        CHECK(m.log[0]==DBUF && m.log[4]==LOCK && m.log[m.log_count-1]==GL2);
        CHECK(dcn302_timing_apply_disabled(&io,&t)==DCN302_BUSY);
        unsigned start=m.log_count;CHECK(!dcn302_timing_restore_disabled(&io,&t) && !t.applied && !t.dirty && !t.poisoned && same_regs(&m,&old));
        CHECK(m.log[start]==GL2 && m.log[m.log_count-1]==GL2);cases++;
    }
    /* Prepare may sample active boot timing; parent activation bits never replay. */
    init(&m,&io,2);m.regs[2][DCN302_R_CONTROL]|=DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK;
    m.regs[2][DCN302_R_VTG]|=DCN302_VTG_ENABLE_MASK;prepare(&m,&io,&t);
    CHECK(dcn302_timing_apply_disabled(&io,&t)==DCN302_BUSY && !m.writes);
    m.regs[2][DCN302_R_CONTROL]&=~(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK);
    m.regs[2][DCN302_R_VTG]&=~DCN302_VTG_ENABLE_MASK;
    CHECK(!dcn302_timing_apply_disabled(&io,&t));CHECK(!dcn302_timing_restore_disabled(&io,&t));cases++;
    /* Delayed lock/unlock and pending drain use native bounded acknowledgments. */
    init(&m,&io,3);prepare(&m,&io,&t);m.lock_ticks=3;m.unlock_ticks=2;
    m.regs[3][DBUF]|=DCN302_TIMING_DBUF_PENDING_MASK|DCN302_TIMING_DBUF_INSTANT_MASK;m.pending_ticks=4;
    CHECK(!dcn302_timing_apply_disabled(&io,&t) && m.delays==9 && !m.invalid);
    CHECK(!dcn302_timing_restore_disabled(&io,&t));cases++;
    init(&m,&io,1);prepare(&m,&io,&t);memcpy(m.regs[1],t.after,sizeof(t.after));
    m.regs[1][DCN302_R_CLOCK]|=DCN302_CLOCK_ON_MASK;
    prepare(&m,&io,&t);old=m;CHECK(!dcn302_timing_apply_disabled(&io,&t));
    CHECK(!dcn302_timing_restore_disabled(&io,&t) && same_regs(&m,&old));cases++;
}
static void bounds_and_prepare(void){
    model m;dcn302_io io;dcn302_timing_transaction t;nexis_gpu_timing mode;dcn302_sync s;
    for(unsigned fault=0;fault<26;fault++){
        init(&m,&io,0);mode=timing();s=sync_request();
        switch(fault){case 0:mode.pixel_khz=0;break;case 1:mode.pixel_khz=4000001;break;case 2:mode.hactive=0;break;
        case 3:mode.vactive=0;break;case 4:mode.flags=4;break;case 5:mode.hsync_start=mode.hactive;break;
        case 6:mode.hsync_end=mode.hsync_start;break;case 7:mode.hsync_end=mode.htotal+1;break;
        case 8:mode.htotal=32769;break;case 9:mode.htotal=mode.hactive+31;break;case 10:mode.hsync_end=mode.hsync_start+3;break;
        case 11:mode.vsync_start=mode.vactive;break;case 12:mode.vsync_end=mode.vsync_start;break;
        case 13:mode.vsync_end=mode.vtotal+1;break;case 14:mode.vtotal=32769;break;case 15:mode.vtotal=mode.vactive+2;break;
        case 16:s.vstartup=0;break;case 17:s.vstartup=1024;break;case 18:s.vstartup=1023;mode.vtotal=999;break;
        case 19:s.vready=65536;break;case 20:s.vupdate_offset=65536;break;case 21:s.vupdate_width=0;break;
        case 22:s.vupdate_width=1024;break;case 23:io.read=NULL;break;case 24:io.write=NULL;break;case 25:io.delay_us=NULL;break;}
        CHECK(dcn302_timing_prepare(&io,0,&mode,&s,guard,now,&t)==DCN302_INPUT && !t.prepared && !m.reads && !m.writes);cases++;
    }
    for(unsigned f=0;f<5;f++){
        init(&m,&io,0);mode=timing();s=sync_request();
        mode.htotal=32768;mode.hactive=32700;mode.hsync_start=32720;mode.hsync_end=32724;
        mode.vtotal=32768;mode.vactive=32700;mode.vsync_start=32703;mode.vsync_end=32708;
        switch(f){case 0:mode.pixel_khz=4000000;break;case 1:s.vstartup=1023;break;case 2:s.vready=65535;break;
        case 3:s.vupdate_offset=65535;break;case 4:s.vupdate_width=1023;break;}
        CHECK(!dcn302_timing_prepare(&io,0,&mode,&s,guard,now,&t));
        CHECK(!dcn302_timing_apply_disabled(&io,&t));CHECK(!dcn302_timing_restore_disabled(&io,&t));cases++;
    }
    init(&m,&io,0);mode=timing();s=sync_request();
    CHECK(dcn302_timing_prepare(&io,5,&mode,&s,guard,now,&t)==DCN302_INPUT);
    CHECK(dcn302_timing_prepare(&io,0,&mode,&s,NULL,now,&t)==DCN302_INPUT);
    CHECK(dcn302_timing_prepare(&io,0,&mode,&s,guard,NULL,&t)==DCN302_INPUT);
    CHECK(dcn302_timing_prepare(&io,0,&mode,&s,guard,now,NULL)==DCN302_INPUT);
    CHECK(dcn302_timing_prepare(&t.owner,0,&mode,&s,guard,now,&t)==DCN302_INPUT);
    CHECK(dcn302_timing_prepare(&io,0,&t.timing,&s,guard,now,&t)==DCN302_INPUT);
    CHECK(dcn302_timing_prepare(&io,0,&mode,&t.sync,guard,now,&t)==DCN302_INPUT);
    CHECK(dcn302_timing_prepare((dcn302_io *)((unsigned char *)&io+1),0,&mode,&s,guard,now,&t)==DCN302_INPUT);
    CHECK(dcn302_timing_prepare(&io,0,(nexis_gpu_timing *)(UINTPTR_MAX-3),&s,guard,now,&t)==DCN302_INPUT);
    CHECK(!m.writes && !m.reads);cases++;
    unsigned char *bytes=malloc(sizeof(t)+64);CHECK(bytes);
    CHECK(dcn302_timing_prepare(&io,0,(nexis_gpu_timing *)(bytes+16),&s,guard,now,(dcn302_timing_transaction *)(bytes+24))==DCN302_INPUT);
    free(bytes);cases++;
    init(&m,&io,0);prepare(&m,&io,&t);unsigned reads=m.reads;
    for(unsigned n=1;n<=reads;n++){
        init(&m,&io,0);m.fail_read=n;CHECK(dcn302_timing_prepare(&io,0,&mode,&s,guard,now,&t)==DCN302_IO && !t.prepared && !m.writes);cases++;
    }
    for(unsigned fault=0;fault<10;fault++){
        init(&m,&io,0);
        switch(fault){case 0:m.unstable_read=32;break;case 1:m.regs[0][DCN302_R_CLOCK]&=~DCN302_CLOCK_ON_MASK;break;
        case 2:m.regs[0][DCN302_R_CLOCK]|=DCN302_SOFT_RESET_MASK;break;case 3:m.regs[0][LOCK]|=1;break;
        case 4:m.regs[0][LOCK]|=DCN302_LOCK_STATUS_MASK;break;case 5:m.regs[0][DCN302_R_INTERLACE]=1;break;
        case 6:m.regs[0][DCN302_R_H_DIV]=DCN302_H_DIV_MASK;break;case 7:m.regs[0][DCN302_R_FORMAT]=DCN302_FORMAT_MASK;break;
        case 8:m.regs[0][DCN302_R_FORMAT]=DCN302_DSC_MASK;break;case 9:m.regs[0][DCN302_R_SOURCE]=5<<DCN302_SEG0_SHIFT;break;}
        CHECK(dcn302_timing_prepare(&io,0,&mode,&s,guard,now,&t)!=DCN302_OK && !t.prepared && !m.writes);cases++;
    }
}
static void conditions(void){
    model m;dcn302_io io;dcn302_timing_transaction t;
    for(unsigned fault=0;fault<35;fault++){
        init(&m,&io,0);prepare(&m,&io,&t);
        if(fault<20){unsigned p=fault/4;switch(fault%4){case 0:m.regs[p][DCN302_R_CONTROL]|=DCN302_MASTER_ENABLE_MASK;break;
        case 1:m.regs[p][DCN302_R_CONTROL]|=DCN302_MASTER_ACTIVE_MASK;break;case 2:m.regs[p][DCN302_R_CLOCK]|=DCN302_BUSY_MASK;break;
        case 3:m.regs[p][DCN302_R_VTG]|=DCN302_VTG_ENABLE_MASK;break;}}
        else switch(fault){case 20:m.guard_ok=false;break;case 21:m.regs[0][DCN302_R_CLOCK]&=~DCN302_CLOCK_ON_MASK;break;
        case 22:m.regs[0][DCN302_R_SOURCE]^=1;break;case 23:m.regs[0][DCN302_R_H_TOTAL]^=1;break;
        case 24:m.regs[0][GL2]^=0x80000000;break;case 25:m.regs[0][LOCK]|=1;break;
        case 26:t.after[0]^=0x1000;break;case 27:t.before[0]|=DCN302_MASTER_ACTIVE_MASK;break;
        case 28:t.prepared=false;break;case 29:t.touched=1u<<31;break;case 30:t.pipe=5;break;
        case 31:io.context=&t;break;case 32:t.sync.vstartup=1024;break;case 33:t.now=NULL;break;case 34:t.guard=NULL;break;}
        CHECK(dcn302_timing_apply_disabled(&io,&t)!=DCN302_OK && !m.writes && !m.invalid);cases++;
    }
    init(&m,&io,0);prepare(&m,&io,&t);
    CHECK(dcn302_timing_apply_disabled((dcn302_io *)((unsigned char *)&io+1),&t)==DCN302_INPUT);
    CHECK(dcn302_timing_apply_disabled(&t.owner,&t)==DCN302_INPUT);
    CHECK(dcn302_timing_apply_disabled(&io,(dcn302_timing_transaction *)((unsigned char *)&t+1))==DCN302_INPUT && !m.writes);cases++;
}
static void failures(void){
    model m,old;dcn302_io io;dcn302_timing_transaction t;
    init(&m,&io,2);prepare(&m,&io,&t);unsigned start=m.reads;
    CHECK(!dcn302_timing_apply_disabled(&io,&t));unsigned reads=m.reads-start,writes=m.writes,apply_writes=writes;
    for(unsigned n=1;n<=reads;n++){
        init(&m,&io,2);prepare(&m,&io,&t);old=m;m.fail_read=m.reads+n;
        CHECK(dcn302_timing_apply_disabled(&io,&t)!=DCN302_OK && !t.applied);
        CHECK(!t.dirty && !t.poisoned && !m.invalid && same_regs(&m,&old));cases++;
    }
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=writes;n++){
        init(&m,&io,2);prepare(&m,&io,&t);old=m;
        if(kind==2)m.ignore_write=n;else{m.fail_write=n;m.posted=kind==1;}
        CHECK(dcn302_timing_apply_disabled(&io,&t)!=DCN302_OK && !t.applied);
        CHECK(!t.dirty && !t.poisoned && !m.invalid && same_regs(&m,&old));cases++;
    }
    init(&m,&io,2);prepare(&m,&io,&t);CHECK(!dcn302_timing_apply_disabled(&io,&t));start=m.reads;unsigned w=m.writes;
    CHECK(!dcn302_timing_restore_disabled(&io,&t));reads=m.reads-start;writes=m.writes-w;
    for(unsigned n=1;n<=reads;n++){
        init(&m,&io,2);prepare(&m,&io,&t);old=m;CHECK(!dcn302_timing_apply_disabled(&io,&t));m.fail_read=m.reads+n;
        CHECK(dcn302_timing_restore_disabled(&io,&t)==DCN302_ROLLBACK && t.dirty && t.poisoned && !m.invalid);
        CHECK(!dcn302_timing_restore_disabled(&io,&t) && !t.dirty && !t.poisoned && same_regs(&m,&old));cases++;
    }
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=writes;n++){
        init(&m,&io,2);prepare(&m,&io,&t);old=m;CHECK(!dcn302_timing_apply_disabled(&io,&t));
        if(kind==2)m.ignore_write=m.writes+n;else{m.fail_write=m.writes+n;m.posted=kind==1;}
        CHECK(dcn302_timing_restore_disabled(&io,&t)==DCN302_ROLLBACK && t.dirty && t.poisoned && !m.invalid);
        CHECK(!dcn302_timing_restore_disabled(&io,&t) && !t.dirty && !t.poisoned && same_regs(&m,&old));cases++;
    }
    for(unsigned loss=0;loss<3;loss++)for(unsigned n=1;n<=apply_writes;n++){
        init(&m,&io,2);prepare(&m,&io,&t);old=m;
        if(loss==0)m.activate_write=n;else if(loss==1)m.clock_loss_write=n;else m.guard_loss_write=n;
        CHECK(dcn302_timing_apply_disabled(&io,&t)==DCN302_ROLLBACK && t.dirty && t.poisoned && m.writes==n && !m.invalid);
        CHECK(dcn302_timing_restore_disabled(&io,&t)==DCN302_ROLLBACK && m.writes==n);
        m.regs[4][DCN302_R_CONTROL]&=~DCN302_MASTER_ACTIVE_MASK;m.regs[2][DCN302_R_CLOCK]|=DCN302_CLOCK_ON_MASK;m.guard_ok=true;
        CHECK(!dcn302_timing_restore_disabled(&io,&t) && !t.dirty && !t.poisoned && same_regs(&m,&old));cases++;
    }
}
static void waits(void){
    model m,old;dcn302_io io;dcn302_timing_transaction t;
    for(unsigned fault=0;fault<8;fault++){
        init(&m,&io,0);prepare(&m,&io,&t);old=m;
        switch(fault){case 0:m.stuck_lock=true;break;case 1:m.stuck_unlock=true;break;
        case 2:m.regs[0][DBUF]|=1;m.stuck_pending=true;break;
        case 3:m.stuck_lock=true;m.frozen=true;break;case 4:m.regs[0][DBUF]|=1;m.stuck_pending=true;m.frozen=true;break;
        case 5:m.lock_ticks=3;m.fail_delay=1;break;case 6:m.regs[0][DBUF]|=1;m.pending_ticks=4;m.fail_delay=1;break;
        case 7:m.lock_ticks=3;m.backwards=true;break;}
        CHECK(dcn302_timing_apply_disabled(&io,&t)!=DCN302_OK && !t.applied && !m.invalid && m.delays<=100000);
        if(fault==0 || fault==1 || fault==3)CHECK(t.dirty && t.poisoned);
        else if(fault==2 || fault==4 || fault==6)CHECK(!m.writes && !t.dirty);
        m.stuck_lock=m.stuck_unlock=m.stuck_pending=m.frozen=m.backwards=false;m.lock_ticks=m.unlock_ticks=0;
        if(m.regs[0][LOCK]&1)m.regs[0][LOCK]|=DCN302_LOCK_STATUS_MASK;else m.regs[0][LOCK]&=~DCN302_LOCK_STATUS_MASK;
        m.regs[0][DBUF]&=~(DCN302_TIMING_DBUF_PENDING_MASK|DCN302_TIMING_DBUF_INSTANT_MASK);
        CHECK(!dcn302_timing_restore_disabled(&io,&t) && !t.dirty && !t.poisoned && same_regs(&m,&old));cases++;
    }
    init(&m,&io,0);prepare(&m,&io,&t);m.fail_guard=1;
    CHECK(dcn302_timing_apply_disabled(&io,&t)==DCN302_BUSY && !m.writes);cases++;
}
int main(void){success();bounds_and_prepare();conditions();failures();waits();
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"native_dcn3_update_lock\":true,\"native_global_sync\":true,\"ro_and_instant_not_replayed\":true,\"posted_failure_rollback\":true,\"physical_hardware_tested\":false}\n",cases,checks);return 0;
}
