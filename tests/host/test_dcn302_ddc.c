#include "../../tools/gpu-driver/amd/dcn302_ddc.h"
#include "../../tools/gpu-driver/amd/hdmi_scdc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GET(v,f) (((v)&DCN302_DDC_##f##_MASK)>>DCN302_DDC_##f##_SHIFT)
#define SET(v,f,n) (((v)&~DCN302_DDC_##f##_MASK)|(((uint32_t)(n)<<DCN302_DDC_##f##_SHIFT)&DCN302_DDC_##f##_MASK))
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static unsigned cases;
typedef struct {
    uint32_t regs[5][DCN302_DDC_REGISTER_COUNT],before[5][DCN302_DDC_REGISTER_COUNT];
    uint8_t fifo[144],scdc[256],edid[256];unsigned pointer,fifo_writes,goes,reads,writes,ops,fail_at,waits,slave_offset;
    bool posted,dead,stuck,zero_done,request_stuck,sleep_stuck,ignore_speed,invalid,ignore_scdc,preempt;
    uint32_t status_fault;unsigned pending;uint64_t time_us;
} model;
static bool common(enum dcn302_ddc_register r){return r>=DCN302_DDC_R_ARBITRATION;}
static bool locate(model *m,uint32_t offset,unsigned *bus,enum dcn302_ddc_register *reg){
    for(unsigned b=0;b<5;b++)for(unsigned r=0;r<DCN302_DDC_REGISTER_COUNT;r++)if(offset==dcn302_ddc_register_bytes[b][r]){
        *bus=b;*reg=(enum dcn302_ddc_register)r;return true;
    }
    m->invalid=true;return false;
}
static void store(model *m,unsigned b,enum dcn302_ddc_register r,uint32_t v){
    if(common(r))for(unsigned n=0;n<5;n++)m->regs[n][r]=v;
    else m->regs[b][r]=v;
}
static bool failing(model *m){m->ops++;return m->dead || m->ops==m->fail_at;}
static void begin(model *m,unsigned b){
    m->goes++;unsigned count=GET(m->regs[b][DCN302_DDC_R_CONTROL],COUNT)+1,index=0;
    CHECK(count<=4);
    for(unsigned n=0;n<count;n++){
        uint32_t t=m->regs[b][DCN302_DDC_R_TRANS0+n];unsigned bytes=GET(t,BYTES);
        CHECK(GET(t,START)==1 && GET(t,STOP_NACK)==1 && GET(t,STOP)==(n==count-1) && bytes && index+bytes+1<=144);
        unsigned a=m->fifo[index++];CHECK((a&1)==GET(t,RW));
        uint8_t *slave=(a>>1)==0x54?m->scdc:(a>>1)==0x50?m->edid:NULL;
        if(!slave){m->status_fault=DCN302_DDC_NACK_MASK;break;}
        if(a&1){CHECK(n==count-1);for(unsigned z=0;z<bytes;z++)m->fifo[index++]=slave[(uint8_t)m->slave_offset++];}
        else {
            m->slave_offset=m->fifo[index++];
            for(unsigned z=1;z<bytes;z++){
                unsigned off=(uint8_t)m->slave_offset++;uint8_t data=m->fifo[index++];
                if(!m->ignore_scdc || (a>>1)!=0x54 || off!=0x20)slave[off]=data;
            }
        }
    }
    store(m,b,DCN302_DDC_R_SW_STATUS,1);m->pending=3;
    if(m->preempt){store(m,b,DCN302_DDC_R_SW_STATUS,2);store(m,b,DCN302_DDC_R_ARBITRATION,SET(m->regs[b][DCN302_DDC_R_ARBITRATION],OWNER,2));m->pending=0;}
}
static bool rd(void *context,uint32_t offset,uint32_t *out){
    model *m=context;unsigned b;enum dcn302_ddc_register r;m->reads++;
    if(failing(m) || !locate(m,offset,&b,&r))return false;
    if(r==DCN302_DDC_R_DATA){CHECK(GET(m->regs[b][r],DATA_RW));CHECK(m->pointer<144);*out=SET(0,DATA_BYTE,m->fifo[m->pointer++]);return true;}
    if(r==DCN302_DDC_R_SW_STATUS && m->pending && !m->stuck){
        if(!--m->pending){store(m,b,r,(m->zero_done?0:DCN302_DDC_DONE_MASK)|m->status_fault);store(m,b,DCN302_DDC_R_CONTROL,m->regs[b][DCN302_DDC_R_CONTROL]&~DCN302_DDC_GO_MASK);}
    }
    if(r==DCN302_DDC_R_POWER_STATUS && m->sleep_stuck){*out=DCN302_DDC_SLEEP_STATE_MASK;return true;}
    *out=m->regs[b][r];return true;
}
static bool wr(void *context,uint32_t offset,uint32_t value){
    model *m=context;unsigned b;enum dcn302_ddc_register r;m->writes++;bool fail=failing(m);
    if((fail && (!m->posted || m->dead)) || !locate(m,offset,&b,&r))return false;
    CHECK(r!=DCN302_DDC_R_SW_STATUS && r!=DCN302_DDC_R_HW_STATUS && r!=DCN302_DDC_R_GPIO && r!=DCN302_DDC_R_TIME_BASE && r!=DCN302_DDC_R_POWER_STATUS);
    if(r==DCN302_DDC_R_ARBITRATION){
        unsigned owner=GET(m->regs[b][r],OWNER);
        value=SET(value,OWNER,owner==2?2:GET(value,REQUEST)?(m->request_stuck?0:1):0);value=SET(value,RELEASE,0);store(m,b,r,value);
    }else if(r==DCN302_DDC_R_CONTROL){
        if(GET(value,RESET) || GET(value,STATUS_RESET)){store(m,b,DCN302_DDC_R_SW_STATUS,0);m->pending=0;value&=~DCN302_DDC_GO_MASK;}
        bool go=GET(value,GO) && !GET(m->regs[b][r],GO);
        value&=~(DCN302_DDC_RESET_MASK|DCN302_DDC_STATUS_RESET_MASK|DCN302_DDC_SEND_RESET_MASK);store(m,b,r,value);
        if(go)begin(m,b);
    }else if(r==DCN302_DDC_R_DATA){
        if(GET(value,INDEX_WRITE))m->pointer=GET(value,INDEX);
        CHECK(m->pointer<144);
        if(!GET(value,DATA_RW)){m->fifo[m->pointer++]=(uint8_t)GET(value,DATA_BYTE);m->fifo_writes++;}
        store(m,b,r,value);
    }else if(!(r==DCN302_DDC_R_SPEED && m->ignore_speed))store(m,b,r,value);
    return !fail;
}
static bool delay(void *context,uint32_t us){model *m=context;m->waits++;if(failing(m))return false;CHECK(us<=1000);m->time_us+=us;return true;}
static void init(model *m,dcn302_ddc *d,unsigned bus){
    memset(m,0,sizeof(*m));
    for(unsigned b=0;b<5;b++){
        m->regs[b][DCN302_DDC_R_SPEED]=0x17700010;
        m->regs[b][DCN302_DDC_R_SETUP]=0x04000010;
        for(unsigned n=0;n<4;n++)m->regs[b][DCN302_DDC_R_TRANS0+n]=0x40000010;
        m->regs[b][DCN302_DDC_R_CONTROL]=0xa4004000;
        m->regs[b][DCN302_DDC_R_ARBITRATION]=DCN302_DDC_QUEUE_MASK;
        m->regs[b][DCN302_DDC_R_TIME_BASE]=SET(SET(0,REF_BASE,100),XTAL_DIV,2);
    }
    for(unsigned n=0;n<256;n++)m->edid[n]=(uint8_t)(n*43+7);
    m->scdc[1]=1;m->scdc[0x20]=0;m->scdc[0x21]=1;
    memcpy(m->before,m->regs,sizeof(m->before));dcn302_io io={m,rd,wr,delay};CHECK(dcn302_ddc_init(d,&io,bus,100000));
}
static void restored(model *m){
    for(unsigned b=0;b<5;b++)for(unsigned r=0;r<DCN302_DDC_REGISTER_COUNT;r++){
        if(r==DCN302_DDC_R_DATA)continue;
        if(m->regs[b][r]!=m->before[b][r]){
            fprintf(stderr,"restore case %u, bus %u reg %u got %08x wanted %08x op %u fault %u\n",cases,b,r,m->regs[b][r],m->before[b][r],m->ops,m->fail_at);exit(1);
        }
    }
    CHECK(!m->invalid);
}
static void normal_cases(void){
    model m;dcn302_ddc d;
    for(unsigned bus=0;bus<5;bus++){
        init(&m,&d,bus);uint8_t v=0xfe;CHECK(dcn302_ddc_read_byte(&d,0x54,1,&v) && v==1 && d.error==DCN302_DDC_OK);restored(&m);cases++;
        CHECK(dcn302_ddc_write_byte(&d,0x54,0x20,3) && m.scdc[0x20]==3);restored(&m);cases++;
        uint8_t offset=128,bytes[128]={0};dcn302_ddc_payload p[2]={{0x50,false,1,&offset},{0x50,true,128,bytes}};
        CHECK(dcn302_ddc_transfer(&d,p,2) && !memcmp(bytes,m.edid+128,128));restored(&m);cases++;
        CHECK(m.goes==3 && m.time_us<1000);
    }
}
static void io_cases(void){
    model m;dcn302_ddc d;uint8_t value=0xe7;
    init(&m,&d,2);CHECK(dcn302_ddc_read_byte(&d,0x54,1,&value));unsigned ops=m.ops;
    for(unsigned posted=0;posted<2;posted++)for(unsigned op=1;op<=ops;op++){
        init(&m,&d,2);m.fail_at=op;m.posted=posted;value=0xe7;
        CHECK(!dcn302_ddc_read_byte(&d,0x54,1,&value));CHECK(value==0xe7 && !d.busy);
        CHECK(d.error==DCN302_DDC_IO || d.error==DCN302_DDC_RELEASE);
        if(!d.poisoned)restored(&m);
        unsigned writes=m.writes;if(d.poisoned){CHECK(!dcn302_ddc_read_byte(&d,0x54,1,&value));CHECK(m.writes==writes);}
        cases++;
    }
    init(&m,&d,0);m.dead=true;CHECK(!dcn302_ddc_read_byte(&d,0x54,1,&value) && d.error==DCN302_DDC_IO && !m.writes);cases++;
}
static void faults(void){
    model m;dcn302_ddc d;uint8_t value=0xe7;
    uint32_t fault[]={DCN302_DDC_NACK_MASK,DCN302_DDC_NACK0_MASK,DCN302_DDC_NACK1_MASK,DCN302_DDC_NACK2_MASK,DCN302_DDC_NACK3_MASK,DCN302_DDC_TIMED_OUT_MASK,DCN302_DDC_ABORTED_MASK,DCN302_DDC_OVERFLOW_MASK};
    for(unsigned n=0;n<sizeof(fault)/sizeof(*fault);n++){
        init(&m,&d,0);m.status_fault=fault[n];CHECK(!dcn302_ddc_read_byte(&d,0x54,1,&value));
        CHECK(value==0xe7 && d.error==(n<5?DCN302_DDC_NACK:n==5?DCN302_DDC_TIMEOUT:DCN302_DDC_ABORTED));restored(&m);cases++;
    }
    for(unsigned n=0;n<4;n++){
        init(&m,&d,0);m.stuck=n==0;m.zero_done=n==1;m.request_stuck=n==2;m.sleep_stuck=n==3;
        CHECK(!dcn302_ddc_read_byte(&d,0x54,1,&value) && value==0xe7 && d.error==DCN302_DDC_TIMEOUT);
        CHECK(m.time_us<=100010);restored(&m);cases++;
    }
    init(&m,&d,0);m.ignore_speed=true;CHECK(!dcn302_ddc_read_byte(&d,0x54,1,&value) && !m.goes);restored(&m);cases++;
    for(unsigned kind=0;kind<8;kind++){
        init(&m,&d,0);
        if(kind==0)store(&m,0,DCN302_DDC_R_ARBITRATION,SET(0,OWNER,2));
        if(kind==1)store(&m,0,DCN302_DDC_R_HW_STATUS,2);
        if(kind==2)store(&m,0,DCN302_DDC_R_SW_STATUS,1);
        if(kind==3)m.regs[0][DCN302_DDC_R_GPIO]=DCN302_DDC_AUX_MASK;
        if(kind==4)m.regs[0][DCN302_DDC_R_GPIO]=DCN302_DDC_PIN_CLOCK_MASK;
        if(kind==5)m.regs[0][DCN302_DDC_R_GPIO]=DCN302_DDC_PIN_DATA_MASK;
        if(kind==6)store(&m,0,DCN302_DDC_R_ARBITRATION,DCN302_DDC_FIRMWARE_REQUEST_MASK);
        if(kind==7)store(&m,0,DCN302_DDC_R_HW_STATUS,DCN302_DDC_HW_REQUEST_MASK);
        CHECK(!dcn302_ddc_read_byte(&d,0x54,1,&value) && !m.writes && d.error==((kind<3 || kind>=6)?DCN302_DDC_BUSY:DCN302_DDC_PAD));cases++;
    }
    init(&m,&d,0);m.preempt=true;CHECK(!dcn302_ddc_read_byte(&d,0x54,1,&value) && d.poisoned && d.error==DCN302_DDC_RELEASE);
    CHECK(!GET(m.regs[0][DCN302_DDC_R_ARBITRATION],REQUEST) && GET(m.regs[0][DCN302_DDC_R_ARBITRATION],OWNER)==2 && GET(m.regs[0][DCN302_DDC_R_SW_STATUS],STATUS)==2);cases++;
}
static void invalid_cases(void){
    model m;dcn302_ddc d;uint8_t offset=1,value=0xee;dcn302_ddc_payload p[2]={{0x54,false,1,&offset},{0x54,true,1,&value}};
    for(unsigned kind=0;kind<7;kind++){
        init(&m,&d,0);dcn302_ddc_payload q[2];memcpy(q,p,sizeof(q));
        if(kind==0)q[0].address=0;
        if(kind==1)q[0].bytes=0;
        if(kind==2)q[1].bytes=129;
        if(kind==3)q[0].read=true;
        if(kind==4)q[1].data=NULL;
        if(kind==5){q[0].bytes=128;q[1].bytes=128;}
        CHECK(!dcn302_ddc_transfer(&d,kind==6?NULL:q,2) && d.error==DCN302_DDC_INPUT && !m.ops);cases++;
    }
    init(&m,&d,0);d.busy=true;CHECK(!dcn302_ddc_transfer(&d,p,2) && d.error==DCN302_DDC_BUSY && !m.ops);cases++;
    dcn302_io io={&m,rd,wr,delay};CHECK(!dcn302_ddc_init(&d,&io,5,100000) && !d.ready);cases++;
    CHECK(!dcn302_ddc_init(&d,&io,0,0) && !d.ready);cases++;
    CHECK(!dcn302_ddc_init(NULL,&io,0,100000));cases++;
}
static bool scdc_read(void *context,uint8_t addr,uint8_t off,uint8_t *v){return dcn302_ddc_read_byte(context,addr,off,v);}
static bool scdc_write(void *context,uint8_t addr,uint8_t off,uint8_t v){return dcn302_ddc_write_byte(context,addr,off,v);}
static bool scdc_delay(void *context,uint32_t us){dcn302_ddc *d=context;return d->io.delay_us(d->io.context,us);}
static void scdc_cases(void){
    model m;dcn302_ddc d;hdmi_scdc_snapshot saved;hdmi_scdc_io io={&d,scdc_read,scdc_write,scdc_delay};
    unsigned clocks[]={25000,148500,340000,340001,558100,600000};
    for(unsigned i=0;i<sizeof(clocks)/sizeof(*clocks);i++)for(unsigned low=0;low<2;low++){
        init(&m,&d,4);m.scdc[0x20]=0x80;
        CHECK(hdmi_scdc_configure(&io,clocks[i],low,&saved)==HDMI_SCDC_OK);
        CHECK(saved.valid && saved.tmds_config==0x80 && !saved.source_version && m.scdc[2]==1);
        unsigned cfg=clocks[i]>340000?3:low?1:0;CHECK(m.scdc[0x20]==(0x80|cfg));restored(&m);
        m.scdc[0x21]=cfg&1;m.scdc[0x40]=15;
        CHECK(hdmi_scdc_verify_link(&io,clocks[i],low)==HDMI_SCDC_OK);restored(&m);
        CHECK(hdmi_scdc_restore(&io,&saved)==HDMI_SCDC_OK && m.scdc[0x20]==0x80 && !m.scdc[2]);restored(&m);cases++;
    }
    init(&m,&d,0);CHECK(hdmi_scdc_configure(&io,558100,false,&saved)==HDMI_SCDC_OK);unsigned ops=m.ops;
    for(unsigned posted=0;posted<2;posted++)for(unsigned op=1;op<=ops;op++){
        init(&m,&d,0);m.fail_at=op;m.posted=posted;
        enum hdmi_scdc_error e=hdmi_scdc_configure(&io,558100,false,&saved);
        CHECK(e==HDMI_SCDC_IO || e==HDMI_SCDC_ROLLBACK);
        if(e!=HDMI_SCDC_ROLLBACK){CHECK(!m.scdc[0x20] && !m.scdc[2]);if(!d.poisoned)restored(&m);}
        cases++;
    }
    init(&m,&d,0);m.ignore_scdc=true;
    CHECK(hdmi_scdc_configure(&io,558100,false,&saved)==HDMI_SCDC_READBACK && !m.scdc[0x20] && !m.scdc[2]);restored(&m);cases++;
    init(&m,&d,0);m.scdc[1]=0;CHECK(hdmi_scdc_configure(&io,558100,false,&saved)==HDMI_SCDC_VERSION && !saved.valid && m.goes==1);cases++;
    init(&m,&d,0);CHECK(hdmi_scdc_configure(&io,558100,false,&saved)==HDMI_SCDC_OK);m.scdc[0x21]=1;
    CHECK(hdmi_scdc_verify_link(&io,558100,false)==HDMI_SCDC_TIMEOUT && m.time_us>=100000 && m.time_us<130000);restored(&m);cases++;
    init(&m,&d,0);CHECK(hdmi_scdc_configure(&io,558100,false,&saved)==HDMI_SCDC_OK);m.scdc[0x21]=0;m.scdc[0x40]=15;
    CHECK(hdmi_scdc_verify_link(&io,558100,false)==HDMI_SCDC_TIMEOUT);restored(&m);cases++;
    init(&m,&d,0);m.scdc[0x20]=0;CHECK(hdmi_scdc_verify_link(&io,558100,false)==HDMI_SCDC_READBACK);cases++;
    init(&m,&d,0);CHECK(hdmi_scdc_configure(&io,600001,false,&saved)==HDMI_SCDC_INPUT && !m.ops && !saved.valid);cases++;
    CHECK(hdmi_scdc_restore(&io,&saved)==HDMI_SCDC_INPUT && !m.ops);cases++;
}
int main(void){normal_cases();io_cases();faults();invalid_cases();scdc_cases();printf("{\"passed\":true,\"cases\":%u,\"native_dcn302_i2c_transaction\":true,\"native_i2c_scdc_integration\":true,\"physical_i2c_verified\":false,\"full_rx6600_driver_complete\":false}\n",cases);return 0;}
