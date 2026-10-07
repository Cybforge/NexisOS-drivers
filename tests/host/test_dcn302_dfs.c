#include "../../tools/gpu-driver/amd/dcn302_dfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases;
typedef struct {
    uint32_t pll,dentist,ctrl,dto[5],otg[5][3];uint64_t time,completion;
    unsigned reads,writes,delays,fail_read,fail_write,ignore_write,fail_delay;
    bool invalid,posted,stuck,frozen,reverse,activate;uint32_t pending;
} model;
static void complete(model *m){
    if(m->pending && !m->stuck && m->time>=m->completion){
        m->dentist=(m->dentist&~DCN302_DFS_DISP_READ_MASK)|((m->dentist&127)<<8)|DCN302_DFS_DISP_DONE_MASK|DCN302_DFS_DPP_DONE_MASK;m->pending=0;
    }
}
static bool read_reg(void *ctx,uint32_t address,uint32_t *out){
    model *m=ctx;complete(m);if(++m->reads==m->fail_read)return false;
    if(address==DCN302_DFS_PLL_BYTES){*out=m->pll;return true;}
    if(address==DCN302_DFS_DENTIST_BYTES){*out=m->dentist;return true;}
    if(address==DCN302_DFS_DTO_CTRL_BYTES){*out=m->ctrl;return true;}
    for(unsigned i=0;i<5;i++){
        if(address==dcn302_dfs_dto_bytes[i]){*out=m->dto[i];return true;}
        enum dcn302_register indices[]={DCN302_R_CONTROL,DCN302_R_CLOCK,DCN302_R_VTG};
        for(unsigned j=0;j<3;j++)if(address==dcn302_register_bytes[i][indices[j]]){*out=m->otg[i][j];return true;}
    }
    m->invalid=true;return false;
}
static bool write_reg(void *ctx,uint32_t address,uint32_t value){
    model *m=ctx;unsigned n=++m->writes;
    for(unsigned i=0;i<5;i++)CHECK(!(m->otg[i][0]&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) && !(m->otg[i][1]&DCN302_BUSY_MASK) && !(m->otg[i][2]&DCN302_VTG_ENABLE_MASK));
    if(n==m->ignore_write)return true;
    if(n==m->fail_write && !m->posted)return false;
    if(address==DCN302_DFS_DENTIST_BYTES){
        CHECK(!m->pending);uint32_t mask=DCN302_DFS_DISP_WRITE_MASK|DCN302_DFS_DPP_WRITE_MASK;
        CHECK(!((value^m->dentist)&~mask));
        m->dentist=(m->dentist&~mask)|(value&mask);
        m->dentist&=~(DCN302_DFS_DISP_DONE_MASK|DCN302_DFS_DPP_DONE_MASK);m->pending=1;m->completion=m->time+25;
    }else if(address==DCN302_DFS_DTO_CTRL_BYTES)m->ctrl=value;
    else{
        bool found=false;for(unsigned i=0;i<5;i++)if(address==dcn302_dfs_dto_bytes[i]){m->dto[i]=value;found=true;}
        if(!found){m->invalid=true;return false;}
    }
    if(m->activate)m->otg[4][0]|=DCN302_MASTER_ACTIVE_MASK;
    return n!=m->fail_write;
}
static bool delay(void *ctx,uint32_t us){model *m=ctx;CHECK(us==5 || us==50);if(++m->delays==m->fail_delay)return false;if(!m->frozen)m->time+=us;complete(m);return true;}
static uint64_t now(void *ctx){model *m=ctx;return m->reverse?m->time--:m->time;}
static void init(model *m,dcn302_io *io,dcn302_smu *smu){
    memset(m,0,sizeof(*m));memset(smu,0,sizeof(*smu));m->time=100000;m->pll=36|0x80000000; /* 36.5 * 100MHz */
    m->dentist=0x80000000u|24|(24u<<8)|(24u<<24)|DCN302_DFS_DISP_DONE_MASK|DCN302_DFS_DPP_DONE_MASK;
    for(unsigned i=0;i<5;i++)m->dto[i]=0x80008000u|128|(255u<<16);
    m->ctrl=0x80000000u|1|16|256|4096|65536;
    *io=(dcn302_io){m,read_reg,write_reg,delay};smu->io=*io;smu->time_us=now;smu->ready=true;smu->floor_known[9]=smu->floor_known[10]=true;smu->floor_mhz[9]=smu->floor_mhz[10]=30000;
}
static bool equal_clock(const model *a,const model *b){return a->pll==b->pll && a->dentist==b->dentist && a->ctrl==b->ctrl && !memcmp(a->dto,b->dto,sizeof(a->dto));}
static dcn302_dfs_request request(void){dcn302_dfs_request r={.disp_khz=558100,.dpp_khz=560000,.pipe_khz={558100,0,123456,0,11111}};return r;}
static void success(void){
    model m,original;dcn302_io io;dcn302_smu smu;dcn302_dfs_transaction t;dcn302_dfs_snapshot s;
    init(&m,&io,&smu);CHECK(!dcn302_dfs_read(&io,&s));CHECK(s.vco_khz==3650000 && s.disp_khz==608333 && s.dpp_khz==608333 && s.pipe_khz[0]==305359);cases++;
    dcn302_dfs_request r=request();original=m;CHECK(!dcn302_dfs_prepare(&io,&r,&t) && t.prepared && !m.writes);
    CHECK(t.required_disp_floor_mhz==609 && t.required_dpp_floor_mhz==609 && t.after.disp_khz==561538 && t.after.dpp_khz==561538);
    CHECK(!dcn302_dfs_apply_disabled(&io,now,&smu,&t) && t.applied && t.dirty && !t.poisoned && !m.invalid);
    CHECK(!dcn302_dfs_read(&io,&s) && s.disp_khz>=r.disp_khz && s.dpp_khz>=r.dpp_khz);
    for(unsigned i=0;i<5;i++){if(r.pipe_khz[i])CHECK(s.pipe_khz[i]>=r.pipe_khz[i]);CHECK((m.dto[i]&0xff00ff00)==(original.dto[i]&0xff00ff00));}
    CHECK(dcn302_dfs_apply_disabled(&io,now,&smu,&t)==DCN302_DFS_BUSY);
    CHECK(!dcn302_dfs_restore_disabled(&io,now,&smu,&t) && !t.applied && !t.poisoned && !t.dirty && equal_clock(&m,&original));cases++;
    /* Actual quantization over all divider ranges, monotonic clock and DTO
     * lower bound. Diverse values catch overflow/truncation and boundary gaps. */
    for(unsigned frequency=30000;frequency<=1825000;frequency+=2345){
        init(&m,&io,&smu);r=(dcn302_dfs_request){.disp_khz=frequency,.dpp_khz=frequency,.pipe_khz={frequency,frequency/3,1,0,frequency-1}};
        CHECK(!dcn302_dfs_prepare(&io,&r,&t));CHECK(t.after.disp_khz>=frequency && t.after.dpp_khz>=frequency);
        CHECK(!dcn302_dfs_apply_disabled(&io,now,&smu,&t));CHECK(!dcn302_dfs_read(&io,&s));
        for(unsigned i=0;i<5;i++)if(r.pipe_khz[i])CHECK(s.pipe_khz[i]>=r.pipe_khz[i]);
        CHECK(!dcn302_dfs_restore_disabled(&io,now,&smu,&t));CHECK(!m.invalid);cases++;
    }
    for(unsigned did=8;did<=127;did++){
        init(&m,&io,&smu);m.dentist=(m.dentist&~0x7f007f7fu)|did|(did<<8)|(did<<24);
        CHECK(!dcn302_dfs_read(&io,&s));unsigned quarter=did<=63?did:did<=95?2*did-64:did<=125?4*did-256:did==126?248:512;
        CHECK(s.disp_khz==14600000u/quarter && s.dpp_khz==s.disp_khz);cases++;
    }
    init(&m,&io,&smu);m.pll=36|0x80010000u;m.dentist=(m.dentist&~0x7f007f7fu)|20|(20u<<8)|(20u<<24);
    CHECK(!dcn302_dfs_read(&io,&s) && s.disp_khz==730000 && s.dpp_khz==730000 && s.disp_floor_mhz==731 && s.dpp_floor_mhz==731);
    r=(dcn302_dfs_request){.disp_khz=700000,.dpp_khz=700000};CHECK(!dcn302_dfs_prepare(&io,&r,&t));
    smu.floor_mhz[9]=smu.floor_mhz[10]=730;CHECK(dcn302_dfs_apply_disabled(&io,now,&smu,&t)==DCN302_DFS_PREREQUISITE && !m.writes);cases++;
}
static void reject(void){
    model m;dcn302_io io;dcn302_smu smu;dcn302_dfs_snapshot s,zero={0};dcn302_dfs_transaction t;dcn302_dfs_request r;
    for(unsigned fault=0;fault<25;fault++){
        init(&m,&io,&smu);
        switch(fault){
            case 0:m.pll=0;break;case 1:m.dentist&=~DCN302_DFS_DISP_DONE_MASK;break;case 2:m.dentist&=~DCN302_DFS_DPP_DONE_MASK;break;
            case 3:m.dentist^=1u<<8;break;case 4:m.dentist&=~DCN302_DFS_DISP_WRITE_MASK;break;case 5:m.dentist&=~DCN302_DFS_DPP_WRITE_MASK;break;
            case 6:case 7:case 8:case 9:case 10:m.ctrl|=dcn302_dfs_dto_db[fault-6];break;
            case 11:case 12:case 13:case 14:case 15:m.dto[fault-11]&=~255u;break;
            case 16:case 17:case 18:case 19:case 20:m.dto[fault-16]&=~0xff0000u;break;
            case 21:m.dto[0]=(m.dto[0]&~0xff00ffu)|255|(128u<<16);break;
            case 22:io.read=NULL;break;case 23:m.dentist=(m.dentist&~127u)|7;break;case 24:m.dentist=(m.dentist&~DCN302_DFS_DPP_WRITE_MASK)|(7u<<24);break;
        }
        memset(&s,0xa5,sizeof(s));CHECK(dcn302_dfs_read(&io,&s)!=DCN302_DFS_OK && !memcmp(&s,&zero,sizeof(s)) && !m.writes);cases++;
    }
    for(unsigned n=1;n<=16;n++){init(&m,&io,&smu);m.fail_read=n;CHECK(dcn302_dfs_read(&io,&s)==DCN302_DFS_IO && !memcmp(&s,&zero,sizeof(s)));cases++;}
    for(unsigned fault=0;fault<11;fault++){
        init(&m,&io,&smu);r=request();
        switch(fault){case 0:r.disp_khz=0;break;case 1:r.dpp_khz=0;break;case 2:r.disp_khz=1825001;break;case 3:r.dpp_khz=1825001;break;
            case 4:r.pipe_khz[4]=0xffffffffu;break;case 5:r.pipe_khz[4]=562000;break;case 6:m.dentist|=1u<<15;break;
            case 7:m.dentist=(m.dentist&~0x7f007f7fu)|127|(127u<<8)|(127u<<24);break;
            case 8:r.disp_khz=0xffffffff;break;case 9:r.dpp_khz=0xffffffff;break;case 10:r.pipe_khz[0]=1000000000;break;}
        CHECK(dcn302_dfs_prepare(&io,&r,&t)!=DCN302_DFS_OK && !t.prepared && !m.writes);cases++;
    }
    for(unsigned fault=0;fault<33;fault++){
        init(&m,&io,&smu);r=request();CHECK(!dcn302_dfs_prepare(&io,&r,&t));
        switch(fault){
            case 0:smu.ready=false;break;case 1:smu.busy=true;break;case 2:smu.poisoned=true;break;case 3:smu.floor_known[9]=false;break;case 4:smu.floor_known[10]=false;break;
            case 5:smu.floor_mhz[9]=608;break;case 6:smu.floor_mhz[10]=608;break;
            case 7:case 8:case 9:case 10:case 11:m.otg[fault-7][0]|=DCN302_MASTER_ENABLE_MASK;break;
            case 12:case 13:case 14:case 15:case 16:m.otg[fault-12][0]|=DCN302_MASTER_ACTIVE_MASK;break;
            case 17:case 18:case 19:case 20:case 21:m.otg[fault-17][1]|=DCN302_BUSY_MASK;break;
            case 22:case 23:case 24:case 25:case 26:m.otg[fault-22][2]|=DCN302_VTG_ENABLE_MASK;break;
            case 27:m.pll++;break;
            case 28:smu.io.context=&t;break;case 29:smu.io.read=NULL;break;case 30:smu.io.write=NULL;break;case 31:smu.io.delay_us=NULL;break;case 32:smu.time_us=NULL;break;
        }
        CHECK(dcn302_dfs_apply_disabled(&io,now,&smu,&t)!=DCN302_DFS_OK && !m.writes && !t.dirty);cases++;
    }
    for(unsigned fault=0;fault<6;fault++){
        init(&m,&io,&smu);r=request();CHECK(!dcn302_dfs_prepare(&io,&r,&t));
        switch(fault){case 0:t.before.vco_khz--;break;case 1:t.before.disp_khz--;break;case 2:t.after.dpp_khz--;break;
            case 3:t.required_disp_floor_mhz--;break;case 4:t.request.pipe_khz[0]+=10000;break;case 5:t.prepared=false;break;}
        CHECK(dcn302_dfs_apply_disabled(&io,now,&smu,&t)==DCN302_DFS_INPUT && !m.writes);cases++;
    }
}
static void failures(void){
    model m,initial;dcn302_io io;dcn302_smu smu;dcn302_dfs_transaction t;dcn302_dfs_request r=request();
    init(&m,&io,&smu);CHECK(!dcn302_dfs_prepare(&io,&r,&t));unsigned start=m.reads;CHECK(!dcn302_dfs_apply_disabled(&io,now,&smu,&t));unsigned reads=m.reads-start,writes=m.writes;
    for(unsigned n=1;n<=reads;n++){
        init(&m,&io,&smu);CHECK(!dcn302_dfs_prepare(&io,&r,&t));initial=m;m.fail_read=m.reads+n;
        CHECK(dcn302_dfs_apply_disabled(&io,now,&smu,&t)!=DCN302_DFS_OK);
        if(!t.poisoned)CHECK(equal_clock(&m,&initial) && !t.dirty && !t.applied);
        else CHECK(t.dirty && !t.applied);
        CHECK(!m.invalid);cases++;
    }
    for(unsigned mode=0;mode<3;mode++)for(unsigned n=1;n<=writes;n++){
        init(&m,&io,&smu);CHECK(!dcn302_dfs_prepare(&io,&r,&t));initial=m;
        if(mode==2)m.ignore_write=n;else{m.fail_write=n;m.posted=mode==1;}
        CHECK(dcn302_dfs_apply_disabled(&io,now,&smu,&t)!=DCN302_DFS_OK);
        if(!t.poisoned)CHECK(equal_clock(&m,&initial) && !t.dirty);else CHECK(t.dirty);
        CHECK(!m.invalid);cases++;
    }
    for(unsigned fault=0;fault<6;fault++){
        init(&m,&io,&smu);CHECK(!dcn302_dfs_prepare(&io,&r,&t));
        switch(fault){case 0:m.stuck=true;break;case 1:m.frozen=true;break;case 2:m.reverse=true;break;
            case 3:m.fail_delay=1;break;case 4:m.fail_write=1;m.posted=true;break;case 5:m.activate=true;break;}
        CHECK(dcn302_dfs_apply_disabled(&io,now,&smu,&t)!=DCN302_DFS_OK && !t.applied && !m.invalid);
        CHECK(t.poisoned && t.dirty && m.writes==1);cases++;
    }
    init(&m,&io,&smu);CHECK(!dcn302_dfs_prepare(&io,&r,&t));m.stuck=true;CHECK(dcn302_dfs_apply_disabled(&io,now,&smu,&t)==DCN302_DFS_ROLLBACK);
    unsigned old_writes=m.writes;CHECK(dcn302_dfs_restore_disabled(&io,now,&smu,&t)==DCN302_DFS_ROLLBACK && m.writes==old_writes);
    /* Explicit later rollback only after the original request has completed. */
    m.stuck=false;complete(&m);CHECK(!dcn302_dfs_restore_disabled(&io,now,&smu,&t) && !t.poisoned && !t.dirty);cases++;
    init(&m,&io,&smu);CHECK(!dcn302_dfs_prepare(&io,&r,&t));CHECK(!dcn302_dfs_apply_disabled(&io,now,&smu,&t));smu.floor_known[10]=false;old_writes=m.writes;
    CHECK(dcn302_dfs_restore_disabled(&io,now,&smu,&t)==DCN302_DFS_ROLLBACK && t.poisoned && m.writes==old_writes);cases++;
}
int main(void){success();reject();failures();printf("{\"passed\":true,\"cases\":%u,\"native_display_dfs_dto_transaction\":true,\"clock_writes_modeled\":true,\"physical_hardware_verified\":false,\"full_rx6600_driver_complete\":false}\n",cases);return 0;}
