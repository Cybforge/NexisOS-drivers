#include "../../tools/gpu-driver/amd/dcn302_smu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases;
typedef struct {
    uint32_t response,argument,last_argument,last_response,command,parameter,version,interface,header,status,forced_floor;
    uint32_t features[13],floor[13];uint16_t frequencies[13][32];
    uint64_t time,due;unsigned latency,ops,fail,ignore,triggers,writes,rollback_writes;
    bool pending,post_failure,frozen,backwards,wrong_test,invalid;
    dcn302_smu *reentry;bool reentered;
    uint32_t messages[1024],parameters[1024];
} model;
static bool clock_valid(unsigned clock){return clock==2 || (clock>=8 && clock<=11);}
static void complete(model *m){
    CHECK(m->pending);uint32_t out=0;unsigned clock=m->parameter>>16,index=m->parameter&0xff;
    if(m->status==1)switch(m->command){
        case 1:out=m->parameter+(m->wrong_test?2u:1u);break;
        case 2:out=m->version;break;case 3:out=m->interface;break;case 4:out=m->header;break;
        case 11:CHECK(clock_valid(clock) && !(m->parameter&0xff00ff00u));out=index==255?m->features[clock]:m->frequencies[clock][index];break;
        case 9:CHECK(clock_valid(clock) && !(m->parameter&0xff000000u));out=m->forced_floor?m->forced_floor:m->parameter&0xffff;
            m->floor[clock]=out;break;
        default:m->invalid=true;break;
    }
    m->argument=out;m->response=m->status;m->last_argument=out;m->last_response=m->status;m->pending=false;
}
static bool rd(void *ctx,uint32_t address,uint32_t *out){
    model *m=ctx;if(++m->ops==m->fail)return false;
    if(address==DCN302_SMU_RESPONSE_BYTES){if(m->pending && m->time>=m->due)complete(m);*out=m->response;return true;}
    if(address==DCN302_SMU_ARGUMENT_BYTES){*out=m->argument;return true;}
    m->invalid=true;return false;
}
static bool wr(void *ctx,uint32_t address,uint32_t value){
    model *m=ctx;unsigned op=++m->ops;m->writes++;
    if(op==m->fail && !m->post_failure)return false;
    if(op!=m->ignore){
        if(address==DCN302_SMU_RESPONSE_BYTES){CHECK(!m->pending);m->response=value;if(value)m->rollback_writes++;}
        else if(address==DCN302_SMU_ARGUMENT_BYTES){CHECK(!m->pending);m->argument=value;}
        else if(address==DCN302_SMU_MESSAGE_BYTES){
            CHECK(!m->pending && !m->response && m->triggers<1024);
            m->command=value;m->parameter=m->argument;m->messages[m->triggers]=value;m->parameters[m->triggers]=m->argument;
            m->triggers++;m->due=m->time+m->latency;m->pending=true;
            if(m->reentry && !m->reentered){
                m->reentered=true;dcn302_smu_limits limits;
                CHECK(!dcn302_smu_clock_limits(m->reentry,DCN302_SMU_UCLK,&limits) && m->reentry->error==DCN302_SMU_BUSY);
            }
        }else {m->invalid=true;return false;}
    }
    return op!=m->fail;
}
static bool delay(void *ctx,uint32_t us){model *m=ctx;CHECK(us==10);if(!m->frozen)m->time+=us;return true;}
static uint64_t now(void *ctx){model *m=ctx;return m->backwards && m->time>=20?0:m->time;}
static dcn302_io init(model *m){
    memset(m,0,sizeof(*m));m->response=m->last_response=1;m->argument=m->last_argument=0xabcdef;
    m->version=0x3a0100;m->interface=0x40;m->header=1;m->status=1;
    for(unsigned n=0;n<13;n++){m->features[n]=0x10000003;m->frequencies[n][0]=100;m->frequencies[n][1]=600;m->frequencies[n][2]=2000;}
    return (dcn302_io){m,rd,wr,delay};
}
static void normal(void){
    model m;dcn302_smu s;dcn302_smu_limits limits;unsigned clocks[]={2,8,9,10,11};
    for(unsigned c=0;c<5;c++)for(unsigned fine=0;fine<2;fine++)for(unsigned latency=0;latency<2;latency++){
        dcn302_io io=init(&m);m.latency=latency?70:0;m.features[clocks[c]]=fine?0x80000002:3;
        if(fine)m.frequencies[clocks[c]][1]=2000;
        CHECK(dcn302_smu_open(&s,&io,now) && s.ready && !s.poisoned && s.version==0x3a0100 && m.triggers==4);
        CHECK(m.messages[0]==1 && m.parameters[0]==0x4e455849 && m.messages[1]==2 && m.messages[2]==4 && m.messages[3]==3);
        CHECK(dcn302_smu_clock_limits(&s,(enum dcn302_smu_clock)clocks[c],&limits) && limits.valid && limits.count==(fine?2:3) && limits.fine_grained==(fine!=0));
        CHECK(!s.floor_known[clocks[c]]);uint32_t mhz=99;
        CHECK(dcn302_smu_set_floor(&s,(enum dcn302_smu_clock)clocks[c],558,&mhz) && mhz==558 && m.floor[clocks[c]]==558 && s.floor_known[clocks[c]] && s.floor_mhz[clocks[c]]==558 && !s.poisoned);
        CHECK(m.messages[m.triggers-1]==9 && m.parameters[m.triggers-1]==(clocks[c]<<16|558));
        CHECK(dcn302_smu_set_floor(&s,(enum dcn302_smu_clock)clocks[c],100,&mhz) && mhz==100); /* Parent's explicit rollback of its known floor. */
        CHECK(!m.invalid && !s.busy);cases++;
    }
    for(unsigned levels=1;levels<=32;levels++){
        dcn302_io io=init(&m);m.features[2]=levels;for(unsigned n=0;n<levels;n++)m.frequencies[2][n]=(uint16_t)(100+n*30);
        CHECK(dcn302_smu_open(&s,&io,now) && dcn302_smu_clock_limits(&s,DCN302_SMU_UCLK,&limits) && limits.count==levels && limits.frequency_mhz[levels-1]==100+(levels-1)*30);cases++;
    }
    dcn302_io io=init(&m);m.response=0;m.pending=true;m.command=1;m.parameter=9;m.due=50;
    CHECK(dcn302_smu_open(&s,&io,now) && m.time==50 && m.triggers==4);cases++;
    io=init(&m);CHECK(dcn302_smu_open(&s,&io,now) && dcn302_smu_clock_limits(&s,DCN302_SMU_UCLK,&limits));
    unsigned triggers=m.triggers;m.reentry=&s;CHECK(dcn302_smu_set_floor(&s,DCN302_SMU_UCLK,600,&m.forced_floor));
    CHECK(m.reentered && s.error==DCN302_SMU_OK && s.clocks[2].valid && m.triggers==triggers+1);cases++;
}
static void failures(void){
    model m;dcn302_smu s;dcn302_smu_limits limits,zero={0};uint32_t value;
    for(unsigned kind=0;kind<9;kind++){
        dcn302_io io=init(&m);
        switch(kind){
            case 0:m.wrong_test=true;break;case 1:m.version=0;break;case 2:m.version=UINT32_MAX;break;
            case 3:m.interface=0x3f;break;case 4:m.header=2;break;case 5:m.response=0;break;
            case 6:m.response=0xab;break;case 7:m.latency=100;m.backwards=true;break;case 8:m.response=0;m.frozen=true;break;
        }
        CHECK(!dcn302_smu_open(&s,&io,now) && !s.ready && !s.busy && !m.invalid);
        if(kind>=5 && kind!=7)CHECK(!m.triggers && !m.writes);
        cases++;
    }
    for(unsigned status=0xfc;status<=0xff;status++){
        dcn302_io io=init(&m);CHECK(dcn302_smu_open(&s,&io,now));m.status=status;
        memset(&limits,0xa5,sizeof(limits));CHECK(!dcn302_smu_clock_limits(&s,DCN302_SMU_UCLK,&limits) && !memcmp(&limits,&zero,sizeof(limits)) && !s.poisoned && !s.busy);
        enum dcn302_smu_error expected[]={DCN302_SMU_BUSY,DCN302_SMU_PREREQUISITE,DCN302_SMU_UNSUPPORTED,DCN302_SMU_FAILED};CHECK(s.error==expected[status-0xfc]);
        m.status=1;CHECK(dcn302_smu_clock_limits(&s,DCN302_SMU_UCLK,&limits));cases++;
    }
    for(unsigned kind=0;kind<7;kind++){
        dcn302_io io=init(&m);CHECK(dcn302_smu_open(&s,&io,now));
        switch(kind){case 0:m.features[2]=0;break;case 1:m.features[2]=33;break;case 2:m.features[2]|=0x100;break;
            case 3:m.features[2]=0x80000003;break;case 4:m.frequencies[2][0]=0;break;case 5:m.frequencies[2][1]=100;break;case 6:m.frequencies[2][2]=500;break;}
        memset(&limits,0xa5,sizeof(limits));CHECK(!dcn302_smu_clock_limits(&s,DCN302_SMU_UCLK,&limits) && !memcmp(&limits,&zero,sizeof(limits)) && !s.clocks[2].valid && !s.poisoned);cases++;
    }
    for(unsigned clock=0;clock<20;clock++)if(!clock_valid(clock)){
        dcn302_io io=init(&m);CHECK(dcn302_smu_open(&s,&io,now));unsigned writes=m.writes;
        CHECK(!dcn302_smu_clock_limits(&s,(enum dcn302_smu_clock)clock,&limits));
        CHECK(!dcn302_smu_set_floor(&s,(enum dcn302_smu_clock)clock,100,&value) && !value && m.writes==writes);cases++;
    }
    for(unsigned kind=0;kind<7;kind++){
        dcn302_io io=init(&m);CHECK(dcn302_smu_open(&s,&io,now) && dcn302_smu_clock_limits(&s,DCN302_SMU_UCLK,&limits));
        unsigned writes=m.writes;uint32_t input[]={0,99,2001,65536,UINT32_MAX,100,100};
        if(kind==5)s.clocks[2].valid=false;
        if(kind==6)s.busy=true;
        CHECK(!dcn302_smu_set_floor(&s,DCN302_SMU_UCLK,input[kind],&value) && !value && m.writes==writes && !s.floor_known[2]);cases++;
    }
    for(unsigned kind=0;kind<4;kind++){
        dcn302_io io=init(&m);CHECK(dcn302_smu_open(&s,&io,now) && dcn302_smu_clock_limits(&s,DCN302_SMU_UCLK,&limits));
        CHECK(dcn302_smu_set_floor(&s,DCN302_SMU_UCLK,100,&value));
        if(kind==0)m.forced_floor=557;
        if(kind==1)m.forced_floor=2001;
        if(kind==2)m.status=0xab;
        if(kind==3)m.latency=2000010;
        CHECK(!dcn302_smu_set_floor(&s,DCN302_SMU_UCLK,558,&value) && !value && !s.floor_known[2] && s.poisoned && !s.busy);
        unsigned writes=m.writes,triggers=m.triggers;
        CHECK(!dcn302_smu_set_floor(&s,DCN302_SMU_UCLK,558,&value) && m.writes==writes && m.triggers==triggers);cases++;
    }
}
static void fault_transactions(void){
    model m;dcn302_smu s;dcn302_smu_limits limits;dcn302_io io=init(&m);
    CHECK(dcn302_smu_open(&s,&io,now));unsigned operations=m.ops;
    for(unsigned post=0;post<2;post++)for(unsigned fail=1;fail<=operations;fail++){
        io=init(&m);m.fail=fail;m.post_failure=post!=0;
        CHECK(!dcn302_smu_open(&s,&io,now) && !s.ready && !s.busy && !m.invalid);
        if(!s.dispatched && s.error==DCN302_SMU_READBACK)CHECK(!s.poisoned && !m.pending && m.argument==m.last_argument && m.response==m.last_response);
        cases++;
    }
    /* Ignored response/argument writes must fail before a trigger, then restore
     * the prior completed mailbox without replaying its message register. */
    for(unsigned ignore=3;ignore<=5;ignore+=2){
        io=init(&m);m.ignore=ignore;CHECK(!dcn302_smu_open(&s,&io,now) && !s.dispatched && !m.triggers && !s.poisoned && m.argument==0xabcdef && m.response==1);cases++;
    }
    /* A dropped trigger is indistinguishable from a pending command until the
     * bounded timeout. It must never cause another trigger or fake response. */
    io=init(&m);m.ignore=7;CHECK(!dcn302_smu_open(&s,&io,now) && s.poisoned && s.dispatched && !m.triggers && !m.response && !s.busy);cases++;
    io=init(&m);CHECK(dcn302_smu_open(&s,&io,now) && dcn302_smu_clock_limits(&s,DCN302_SMU_UCLK,&limits));unsigned before=m.ops;
    uint32_t actual;CHECK(dcn302_smu_set_floor(&s,DCN302_SMU_UCLK,558,&actual));operations=m.ops-before;
    for(unsigned post=0;post<2;post++)for(unsigned fault=1;fault<=operations;fault++){
        io=init(&m);CHECK(dcn302_smu_open(&s,&io,now) && dcn302_smu_clock_limits(&s,DCN302_SMU_UCLK,&limits));
        CHECK(dcn302_smu_set_floor(&s,DCN302_SMU_UCLK,100,&actual));m.fail=m.ops+fault;m.post_failure=post!=0;
        CHECK(!dcn302_smu_set_floor(&s,DCN302_SMU_UCLK,558,&actual) && !actual && !s.busy && !m.invalid);
        if(s.dispatched)CHECK(s.poisoned && !s.floor_known[2]);
        else if(s.error==DCN302_SMU_READBACK)CHECK(s.floor_known[2] && s.floor_mhz[2]==100 && m.floor[2]==100);
        cases++;
    }
    io=init(&m);CHECK(!dcn302_smu_open(NULL,&io,now));CHECK(!dcn302_smu_open(&s,NULL,now) && s.error==DCN302_SMU_INPUT);cases++;
}
int main(void){normal();failures();fault_transactions();printf("{\"passed\":true,\"cases\":%u,\"native_dal_smu_transport\":true,\"actual_floor_command\":true,\"display_clock_readback_proven\":false,\"physical_hardware_verified\":false}\n",cases);return 0;}
