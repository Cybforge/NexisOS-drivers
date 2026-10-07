#include "../../tools/gpu-driver/amd/rx6600.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
#define HSET(v,f,n) (((v)&~DCN302_HDMI_##f##_MASK)|(((uint32_t)(n)<<DCN302_HDMI_##f##_SHIFT)&DCN302_HDMI_##f##_MASK))
#define SSET(v,f,n) (((v)&~DCN302_SURFACE_##f##_MASK)|(((uint32_t)(n)<<DCN302_SURFACE_##f##_SHIFT)&DCN302_SURFACE_##f##_MASK))
int NEXIS_GPU_CALL driver_init_v2(const nexis_gpu_services *,nexis_gpu_instance *);
static unsigned cases;
typedef struct {
    uint8_t rom[2048];uint32_t otg[5][DCN302_REGISTER_COUNT],dig[5][DCN302_HDMI_REGISTER_COUNT],surface[5][DCN302_SURFACE_REGISTER_COUNT],hpd[5];
    uint32_t fb_base,fb_top,fb_offset,period;
    uint32_t dfs_pll,dfs_dentist,dfs_control,dfs_dto[5];
    uint32_t hubp[5][DCN302_HUBP_REGISTER_COUNT];
    uint32_t smu_argument,smu_response,smu_version,smu_interface,smu_header,smu_features,smu_status,smu_floor;
    uint64_t time;unsigned reads,writes,queries,fail_read,fail_query,smu_writes,smu_triggers,smu_clock_requests;
    bool frozen,invalid;
    nexis_gpu_resource vram,registers;
} model;
static void p16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t *p,uint32_t v){for(unsigned n=0;n<4;n++)p[n]=(uint8_t)(v>>(8*n));}
static unsigned bit(uint32_t mask){unsigned s=0;while(!(mask&1)){mask>>=1;s++;}return s;}
static void rom_init(model *m,unsigned link,unsigned ddc,unsigned hpd){
    uint8_t *b=m->rom;b[0]=0x55;b[1]=0xaa;b[2]=4;p16(b+0x18,280);memcpy(b+280,"PCIR",4);
    p16(b+284,0x1002);p16(b+286,0x73ff);p16(b+290,24);p16(b+296,4);b[301]=128;
    p16(b+0x48,1720);p16(b+1720,36);memcpy(b+1724,"ATOM",4);p16(b+1750,80);p16(b+1752,100);
    p16(b+80,4);p16(b+100,64);b[102]=2;b[103]=1;p16(b+104+12*2,200);p16(b+104+22*2,400);p16(b+104+27*2,1600);p16(b+104+28*2,600);
    p16(b+600,108);b[602]=2;b[603]=5;b[620]=1;
    p32(b+624,8192);p32(b+628,255);p16(b+634,84);b[637]=0x70;b[638]=8;b[639]=4;
    p16(b+200,20);b[202]=2;b[203]=1;
    p32(b+204,dcn302_ddc_gpio_bytes[ddc]/4);b[208]=(uint8_t)bit(dcn302_ddc_clk_mask[ddc]);b[209]=1;b[210]=(uint8_t)(0x90+ddc);
    p32(b+212,DCN302_GPIO_HPD_BYTES/4);b[216]=(uint8_t)bit(dcn302_hpd_mask[hpd]);b[217]=b[216];b[218]=5;
    p16(b+400,48);b[402]=1;b[403]=4;b[406]=1;p16(b+408,0x310c);p16(b+410,24);
    unsigned encoders[]={0x211e,0x221e,0x2120,0x2220,0x2121};p16(b+412,encoders[link]);p16(b+416,34);p16(b+420,8);
    b[424]=1;b[425]=4;b[426]=(uint8_t)(0x90+ddc);b[428]=2;b[429]=4;b[430]=5;b[431]=1;b[432]=255;
    b[434]=20;b[435]=6;p32(b+436,4);b[440]=255;
    p16(b+1600,84);b[1602]=4;b[1603]=4;p32(b+1608,60000);p16(b+1612,2700);p16(b+1614,2700);p16(b+1636,2700);
    b[1642]=5;b[1644]=6;b[1645]=6;b[1646]=6;
    unsigned sum=0;for(unsigned n=0;n<2047;n++)sum+=b[n];b[2047]=(uint8_t)(0-sum);
}
static bool NEXIS_GPU_CALL rd(void *ctx,unsigned bar,uint32_t offset,uint32_t *out){
    model *m=ctx;CHECK(bar==5);if(++m->reads==m->fail_read)return false;
    if(offset==DCN302_SMU_RESPONSE_BYTES){*out=m->smu_response;return true;}
    if(offset==DCN302_SMU_ARGUMENT_BYTES){*out=m->smu_argument;return true;}
    if(offset==DCN302_DFS_PLL_BYTES){*out=m->dfs_pll;return true;}
    if(offset==DCN302_DFS_DENTIST_BYTES){*out=m->dfs_dentist;return true;}
    if(offset==DCN302_DFS_DTO_CTRL_BYTES){*out=m->dfs_control;return true;}
    for(unsigned i=0;i<5;i++)if(offset==dcn302_dfs_dto_bytes[i]){*out=m->dfs_dto[i];return true;}
    for(unsigned i=0;i<5;i++){
        for(unsigned r=0;r<DCN302_REGISTER_COUNT;r++)if(offset==dcn302_register_bytes[i][r]){
            *out=r==DCN302_R_FRAME_COUNT?(uint32_t)(m->time/m->period)&DCN302_FRAME_COUNT_MASK:m->otg[i][r];return true;
        }
        for(unsigned r=0;r<DCN302_HDMI_REGISTER_COUNT;r++)if(offset==dcn302_hdmi_register_bytes[i][r]){*out=m->dig[i][r];return true;}
        for(unsigned r=0;r<DCN302_SURFACE_REGISTER_COUNT;r++)if(offset==dcn302_surface_register_bytes[i][r]){*out=m->surface[i][r];return true;}
        for(unsigned r=0;r<DCN302_HUBP_REGISTER_COUNT;r++)if(offset==dcn302_hubp_register_bytes[i][r]){*out=m->hubp[i][r];return true;}
        if(offset==dcn302_hpd_status_bytes[i]){*out=m->hpd[i];return true;}
    }
    if(offset==DCN302_FB_BASE_BYTES){*out=m->fb_base;return true;}if(offset==DCN302_FB_TOP_BYTES){*out=m->fb_top;return true;}
    if(offset==DCN302_FB_OFFSET_BYTES){*out=m->fb_offset;return true;}
    m->invalid=true;return false;
}
static bool NEXIS_GPU_CALL wr(void *ctx,unsigned bar,uint32_t offset,uint32_t value){
    model *m=ctx;CHECK(bar==5);
    if(offset==DCN302_SMU_RESPONSE_BYTES){m->smu_writes++;m->smu_response=value;return true;}
    if(offset==DCN302_SMU_ARGUMENT_BYTES){m->smu_writes++;m->smu_argument=value;return true;}
    if(offset==DCN302_SMU_MESSAGE_BYTES){
        m->smu_writes++;m->smu_triggers++;CHECK(!m->smu_response);unsigned clock=m->smu_argument>>16,index=m->smu_argument&0xff;
        uint32_t result=0;
        if(m->smu_status==1)switch(value){
            case 1:result=m->smu_argument+1;break;case 2:result=m->smu_version;break;
            case 3:result=m->smu_interface;break;case 4:result=m->smu_header;break;
            case 11:CHECK(clock==1 || clock==2 || (clock>=8 && clock<=11));result=index==255?m->smu_features:index==0?100:index==1?600:2000;break;
            case 9:CHECK(clock==1 || clock==2 || (clock>=8 && clock<=11));result=m->smu_argument&0xffff;m->smu_floor=result;m->smu_clock_requests++;break;
            default:m->invalid=true;return false;
        }
        m->smu_response=m->smu_status;m->smu_argument=result;return true;
    }
    m->writes++;
    for(unsigned i=0;i<5;i++)CHECK(!(m->otg[i][DCN302_R_CONTROL]&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) && !(m->otg[i][DCN302_R_VTG]&DCN302_VTG_ENABLE_MASK));
    if(offset==DCN302_DFS_DENTIST_BYTES){
        m->dfs_dentist=(value&~DCN302_DFS_DISP_READ_MASK)|((value&127)<<8)|DCN302_DFS_DISP_DONE_MASK|DCN302_DFS_DPP_DONE_MASK;return true;
    }
    if(offset==DCN302_DFS_DTO_CTRL_BYTES){m->dfs_control=value;return true;}
    for(unsigned i=0;i<5;i++)if(offset==dcn302_dfs_dto_bytes[i]){m->dfs_dto[i]=value;return true;}
    for(unsigned i=0;i<5;i++)for(unsigned r=0;r<DCN302_HUBP_REGISTER_COUNT;r++)if(offset==dcn302_hubp_register_bytes[i][r]){
        CHECK(!(value&(dcn302_hubp_forbidden[r]|dcn302_hubp_readonly[r])));
        uint32_t current=m->hubp[i][r];
        for(unsigned sr=0;sr<DCN302_SURFACE_REGISTER_COUNT;sr++)if(offset==dcn302_surface_register_bytes[i][sr])current=m->surface[i][sr];
        uint32_t next=(current&dcn302_hubp_readonly[r])|value;
        if(r==DCN302_HUBP_R_DCHUBP_CNTL && (value&0x1001u)==1u)next|=2;
        m->hubp[i][r]=next;
        for(unsigned sr=0;sr<DCN302_SURFACE_REGISTER_COUNT;sr++)if(offset==dcn302_surface_register_bytes[i][sr])m->surface[i][sr]=next;
        return true;
    }
    m->invalid=true;return false;
}
static bool NEXIS_GPU_CALL resource(void *ctx,unsigned bar,nexis_gpu_resource *out){
    model *m=ctx;memset(out,0,sizeof(*out));if(++m->queries==m->fail_query)return false;
    if(bar==0)*out=m->vram;else if(bar==5)*out=m->registers;else return false;return true;
}
static bool NEXIS_GPU_CALL delay(void *ctx,uint32_t us){model *m=ctx;CHECK(us==1 || us==10);if(!m->frozen)m->time+=us;return true;}
static uint64_t NEXIS_GPU_CALL now(void *ctx){return ((model *)ctx)->time;}
static void init(model *m,nexis_gpu_services *k,unsigned link,unsigned stream,unsigned pipe,unsigned bus,unsigned hpd,unsigned opp,unsigned mpcc,unsigned hubp,unsigned format){
    memset(m,0,sizeof(*m));memset(k,0,sizeof(*k));rom_init(m,link,bus,hpd);m->period=4166;
    m->smu_response=m->smu_status=1;m->smu_version=0x3a0100;m->smu_header=1;m->smu_interface=0x40;m->smu_features=3;
    m->dfs_pll=36|0x80000000u;m->dfs_dentist=24|(24u<<8)|(24u<<24)|DCN302_DFS_DISP_DONE_MASK|DCN302_DFS_DPP_DONE_MASK;
    *k=(nexis_gpu_services){.abi=2,.size=112,.vendor=0x1002,.device=0x73ff,.width=1920,.height=1080,.pitch=2048,.format=format,
        .framebuffer=0x800200000ULL,.framebuffer_bytes=2048u*4*1080,.rom=m->rom,.rom_bytes=2048,.service_context=m,
        .read32=rd,.write32=wr,.time_us=now,.delay_us=delay,.resource=resource};
    m->vram=(nexis_gpu_resource){0x800000000ULL,0x200000000ULL,7,0};m->registers=(nexis_gpu_resource){0xb0000000,0x100000,9,0};
    m->hpd[hpd]=DCN302_HPD_SENSE_MASK|DCN302_HPD_DELAYED_MASK;
    m->dig[link][DCN302_HDMI_R_BE]=HSET(HSET(HSET(0,LINK_MODE,3),FE_SOURCE,1u<<stream),HPD,hpd);
    m->dig[link][DCN302_HDMI_R_BE_ENABLE]=DCN302_HDMI_LINK_ENABLE_MASK|DCN302_HDMI_LINK_CLOCK_MASK;
    m->dig[stream][DCN302_HDMI_R_FE]=HSET(0,PIPE,pipe);
    uint32_t *o=m->otg[pipe];o[DCN302_R_CONTROL]=DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK;
    o[DCN302_R_CLOCK]=DCN302_CLOCK_ENABLE_MASK|DCN302_CLOCK_ON_MASK;o[DCN302_R_SOURCE]=opp<<DCN302_SEG0_SHIFT;
    o[DCN302_R_H_TOTAL]=2079;o[DCN302_R_H_BLANK]=2032|(112u<<16);o[DCN302_R_H_SYNC]=64u<<16;
    o[DCN302_R_V_TOTAL]=1117;o[DCN302_R_V_BLANK]=1115|(35u<<16);o[DCN302_R_V_SYNC]=5u<<16;
    m->surface[opp][DCN302_SURFACE_R_OUT_MUX]=mpcc;m->surface[mpcc][DCN302_SURFACE_R_TOP]=hubp;
    m->surface[mpcc][DCN302_SURFACE_R_BOTTOM]=15;m->surface[mpcc][DCN302_SURFACE_R_OPP]=opp;m->surface[mpcc][DCN302_SURFACE_R_MODE]=2;
    uint32_t *h=m->surface[hubp];h[DCN302_SURFACE_R_CONFIG]=8;h[DCN302_SURFACE_R_VIEW_SIZE]=SSET(SSET(0,WIDTH,1920),HEIGHT,1080);
    h[DCN302_SURFACE_R_CLOCK]=0xf00001u;
    h[DCN302_SURFACE_R_CROSSBAR]=SSET(SSET(SSET(0,RED,format?3:2),GREEN,1),BLUE,format?2:3);h[DCN302_SURFACE_R_PITCH]=2047;
    h[DCN302_SURFACE_R_ADDRESS]=h[DCN302_SURFACE_R_INUSE]=0x200000;h[DCN302_SURFACE_R_ADDRESS_HIGH]=h[DCN302_SURFACE_R_INUSE_HIGH]=0x80;
    m->fb_base=0x9000;m->fb_top=0x91ff;m->fb_offset=0x8000;
}
static void normal(void){
    model m;nexis_gpu_services k;rx6600_state s;nexis_gpu_scanout out;
    /* Independent routes all the way from ROM wiring to the HUBP, not a
     * constant DIG0=FE0=OTG0 assumption. Clock comes from the native counter. */
    for(unsigned pipe=0;pipe<5;pipe++)for(unsigned link=0;link<5;link++)for(unsigned format=0;format<2;format++){
        init(&m,&k,link,(pipe+2)%5,pipe,(link+1)%5,(pipe+3)%5,(pipe+1)%5,(link+2)%5,(link+4)%5,format);
        CHECK(rx6600_probe(&s,&k)==RX6600_OK && s.ready && !s.busy);
        CHECK(s.smu.ready && s.smu.clocks[1].valid && s.smu.clocks[2].valid && s.smu.clocks[8].valid && s.smu.clocks[9].valid && s.smu.clocks[10].valid && s.smu.clocks[11].valid && m.smu_triggers==28 && !m.smu_clock_requests);
        CHECK(s.memory.type==0x70 && s.memory.channels==8 && s.memory.channel_bytes==2 && s.memory.memory_mb==8192);
        CHECK(rx6600_read_mode(&s,&out) && out.framebuffer==k.framebuffer && out.pitch==2048 && out.format==format && out.flags==11 && out.timing.pixel_khz>558000);
        CHECK(rx6600_set_mode(&s,&out.timing));nexis_gpu_timing changed=out.timing;changed.pixel_khz=148500;
        CHECK(!rx6600_set_mode(&s,&changed) && s.error==RX6600_MODESET_PENDING && s.ready && !m.writes);
        m.time+=500000;rx6600_poll(&s);CHECK(s.ready && rx6600_read_mode(&s,&out));
        rx6600_shutdown(&s);CHECK(!s.ready && !rx6600_read_mode(&s,&out) && !out.flags && !m.invalid && !m.writes);cases++;
    }
}
static void failures(void){
    model m;nexis_gpu_services k;rx6600_state s;nexis_gpu_scanout out,zero={0};
    for(unsigned fault=0;fault<27;fault++){
        init(&m,&k,4,0,3,2,1,2,4,1,1);
        switch(fault){
            case 0:k.size=104;break;case 1:k.vendor=0x10de;break;case 2:k.device=0x73df;break;case 3:k.resource=NULL;break;
            case 4:k.rom_bytes=1;break;case 5:m.rom[100]^=1;break;case 6:m.vram.flags=1;break;
            case 7:m.registers.flags=1;break;case 8:m.registers.bytes=4096;break;case 9:m.vram.base++;break;
            case 10:m.hpd[1]=0;break;case 11:m.otg[3][DCN302_R_CONTROL]=0;break;
            case 12:m.surface[1][DCN302_SURFACE_R_PITCH]++;break;case 13:m.frozen=true;break;
            case 14:k.pitch=1919;break;case 15:k.format=2;break;case 16:m.period=2000;break;
            case 17:k.write32=NULL;break;case 18:m.smu_version=0;break;case 19:m.smu_interface=0x3f;break;
            case 20:m.smu_header=2;break;case 21:m.smu_features=0;break;case 22:m.smu_status=0xfe;break;case 23:m.smu_response=0xab;break;
            case 24:m.rom[638]=0;break;case 25:m.rom[639]=32;break;case 26:m.rom[603]=6;break;
        }
        if(fault>=24){unsigned sum=0;for(unsigned n=0;n<2047;n++)sum+=m.rom[n];m.rom[2047]=(uint8_t)(0-sum);}
        CHECK(rx6600_probe(&s,&k)!=RX6600_OK && !s.ready && !s.busy && !m.writes);
        memset(&out,0xa5,sizeof(out));CHECK(!rx6600_read_mode(&s,&out) && !memcmp(&out,&zero,sizeof(out)));cases++;
    }
    for(unsigned op=1;op<=100;op++){
        init(&m,&k,4,0,3,2,1,2,4,1,1);m.fail_read=op;CHECK(rx6600_probe(&s,&k)!=RX6600_OK && !s.ready && !m.writes && !m.invalid);cases++;
    }
    for(unsigned op=1;op<=4;op++){
        init(&m,&k,4,0,3,2,1,2,4,1,1);m.fail_query=op;CHECK(rx6600_probe(&s,&k)!=RX6600_OK && !s.ready && !m.writes);cases++;
    }
    for(unsigned fault=0;fault<20;fault++){
        init(&m,&k,4,0,3,2,1,2,4,1,1);CHECK(rx6600_probe(&s,&k)==RX6600_OK);
        switch(fault){
            case 0:m.vram.bytes/=2;break;case 1:m.registers.base+=4096;break;case 2:m.hpd[1]=0;break;
            case 3:m.dig[4][DCN302_HDMI_R_BE]=0;break;case 4:m.surface[1][DCN302_SURFACE_R_INUSE]++;break;
            case 5:m.otg[3][DCN302_R_H_TOTAL]++;break;
            case 6:m.period*=2;m.time+=500000;break;case 7:m.time+=31000000;break;case 8:m.time=0;break;
            case 9:m.fail_read=m.reads+1;break;
            case 10:m.otg[3][DCN302_R_V_CONTROL]|=DCN302_V_MIN_SELECT_MASK;break;
            case 11:m.otg[3][DCN302_R_V_MIN]++;break;case 12:m.otg[3][DCN302_R_V_MAX]++;break;
            case 13:s.smu.poisoned=true;break;case 14:s.smu.ready=false;break;
            case 15:m.dfs_pll++;break;case 16:m.dfs_dentist&=~DCN302_DFS_DPP_DONE_MASK;break;
            case 17:m.dfs_control|=2;break;case 18:m.dfs_dto[1]=1;break;case 19:m.dfs_dentist^=1u<<24;break;
        }
        memset(&out,0xa5,sizeof(out));CHECK(!rx6600_read_mode(&s,&out) && !memcmp(&out,&zero,sizeof(out)) && !s.ready && !s.busy && !m.writes);cases++;
    }
    init(&m,&k,4,0,3,2,1,2,4,1,1);CHECK(rx6600_probe(&s,&k)==RX6600_OK);s.busy=true;
    CHECK(!rx6600_read_mode(&s,&out) && s.ready);rx6600_poll(&s);CHECK(s.ready);cases++;
    init(&m,&k,4,0,3,2,1,2,4,1,1);CHECK(rx6600_probe(&s,&k)==RX6600_OK);m.hpd[1]=0;rx6600_poll(&s);CHECK(!s.ready);cases++;
}
typedef bool (NEXIS_GPU_CALL *pic_floor)(void *,enum dcn302_smu_clock,uint32_t,uint32_t *);
static void retained(nexis_gpu_entry_v2 entry,uintptr_t floor_address,uintptr_t dfs_address,uintptr_t bandwidth_address,uintptr_t hubp_address){
    model m;nexis_gpu_services k;nexis_gpu_instance out;nexis_gpu_scanout mode;
    init(&m,&k,4,0,3,2,1,2,4,1,1);memset(&out,0xa5,sizeof(out));CHECK(entry(&k,&out)==0 && out.abi==2 && out.size==sizeof(out));
    CHECK(out.read_mode && out.set_mode && out.poll && out.shutdown && out.state && out.state_bytes && out.hdmi && out.scdc);
    CHECK(out.read_mode(out.state,&mode) && mode.flags==11 && !(mode.flags&NEXIS_GPU_SCANOUT_AUDIO));
    rx6600_state *native=out.state;uint32_t acknowledged=0;
    if(bandwidth_address)CHECK((uintptr_t)native->bandwidth_plan==bandwidth_address);
    if(hubp_address)CHECK((uintptr_t)native->bandwidth_registers==hubp_address);
    dcn302_dml_output bandwidth,zero_bandwidth={0};unsigned before=m.smu_triggers;
    memset(&bandwidth,0xff,sizeof(bandwidth));
    CHECK(!native->bandwidth_plan(native,&mode.timing,false,&bandwidth) && !memcmp(&bandwidth,&zero_bandwidth,sizeof(bandwidth)) && m.smu_triggers==before && !m.writes);cases++;
    CHECK(!native->smu.floor_known[2] && !m.smu_clock_requests);
    if(floor_address)CHECK((uintptr_t)native->clock_floor==floor_address);
    bool floor_ok=floor_address?((pic_floor)floor_address)(native,DCN302_SMU_UCLK,558,&acknowledged):native->clock_floor(native,DCN302_SMU_UCLK,558,&acknowledged);
    CHECK(floor_ok && acknowledged==558 && m.smu_clock_requests==1 && m.smu_floor==558 && native->smu.floor_known[2] && native->smu.floor_mhz[2]==558);cases++;
    if(dfs_address)CHECK((uintptr_t)native->display_clocks==dfs_address);
    CHECK(native->dfs.valid && native->dfs.disp_khz==608333 && native->dfs.dpp_khz==608333);
    dcn302_dfs_request r={.disp_khz=700000,.dpp_khz=700000,.pipe_khz={0,700000,0,0,0}};
    CHECK(native->display_clocks(native,&r,RX6600_DFS_PREPARE) && native->dfs_transaction.prepared && !m.writes);
    CHECK(!native->display_clocks(native,NULL,RX6600_DFS_APPLY) && !m.writes);
    CHECK(native->clock_floor(native,DCN302_SMU_DISPCLK,native->dfs_transaction.required_disp_floor_mhz,&acknowledged));
    CHECK(native->clock_floor(native,DCN302_SMU_DPPCLK,native->dfs_transaction.required_dpp_floor_mhz,&acknowledged));
    CHECK(!native->display_clocks(native,NULL,RX6600_DFS_APPLY) && !m.writes && !native->dfs_transaction.dirty);
    uint32_t control=m.otg[3][DCN302_R_CONTROL];m.otg[3][DCN302_R_CONTROL]=0;
    CHECK(native->display_clocks(native,NULL,RX6600_DFS_APPLY) && m.writes>=2 && native->dfs_transaction.applied && native->dfs.disp_khz==730000 && native->dfs.dpp_khz==730000);
    unsigned triggers=m.smu_triggers;
    CHECK(!native->clock_floor(native,DCN302_SMU_DISPCLK,729,&acknowledged) && !acknowledged && m.smu_triggers==triggers);
    CHECK(!native->clock_floor(native,DCN302_SMU_DPPCLK,729,&acknowledged) && !acknowledged && m.smu_triggers==triggers);
    CHECK(native->display_clocks(native,NULL,RX6600_DFS_RESTORE) && !native->dfs_transaction.dirty && native->dfs.disp_khz==608333 && native->dfs.dpp_khz==608333);
    CHECK(!native->display_clocks(native,NULL,(enum rx6600_dfs_operation)-1));m.otg[3][DCN302_R_CONTROL]=control;
    unsigned completed_writes=m.writes;CHECK(completed_writes && m.smu_clock_requests==3 && native->ready && !native->busy);cases++;
    CHECK(native->clock_floor(native,DCN302_SMU_SOCCLK,600,&acknowledged));
    CHECK(native->clock_floor(native,DCN302_SMU_DCEFCLK,600,&acknowledged));
    CHECK(native->clock_floor(native,DCN302_SMU_PHYCLK,600,&acknowledged));
    before=m.smu_triggers;
    CHECK(native->bandwidth_plan(native,&mode.timing,false,&bandwidth) && bandwidth.disp_khz>=mode.timing.pixel_khz && bandwidth.urgent_ns>=4000);
    CHECK(native->dml_job.input.channels==8 && native->dml_job.input.channel_bytes==2 && native->dml_job.input.dram_mts==8928 && native->dml_job.input.ref_khz==27000);
    CHECK(native->dml_job.input.pipe==3 && native->dml_job.input.dpp_khz==native->dfs.pipe_khz[1]);
    CHECK(m.smu_triggers==before && m.writes==completed_writes && native->ready && !native->busy);cases++;
    /* The old prepared target retains a smaller DPP DTO after restoration.
     * A current-clock plan must not silently use it. Explicit target input
     * must be selected independently, without ever writing registers. */
    bool target_ok=native->bandwidth_plan(native,&mode.timing,true,&bandwidth);
    CHECK(native->dml_job.input.dpp_khz==native->dfs_transaction.after.pipe_khz[1]);
    CHECK(target_ok || !memcmp(&bandwidth,&zero_bandwidth,sizeof(bandwidth)));
    CHECK(m.smu_triggers==before && m.writes==completed_writes);cases++;
    CHECK(native->bandwidth_plan(native,&mode.timing,false,&bandwidth) && native->dml_job.input.dpp_khz==native->dfs.pipe_khz[1]);cases++;
    native->busy=true;CHECK(!native->bandwidth_plan(native,&mode.timing,false,&bandwidth) && !memcmp(&bandwidth,&zero_bandwidth,sizeof(bandwidth)));native->busy=false;cases++;
    native->dfs_transaction.dirty=true;CHECK(!native->bandwidth_plan(native,&mode.timing,false,&bandwidth) && !memcmp(&bandwidth,&zero_bandwidth,sizeof(bandwidth)));native->dfs_transaction.dirty=false;cases++;
    native->smu.floor_known[DCN302_SMU_SOCCLK]=false;
    CHECK(!native->bandwidth_plan(native,&mode.timing,false,&bandwidth) && !memcmp(&bandwidth,&zero_bandwidth,sizeof(bandwidth)) && native->ready);
    native->smu.floor_known[DCN302_SMU_SOCCLK]=true;cases++;
    nexis_gpu_timing bad=mode.timing;bad.hactive++;
    CHECK(!native->bandwidth_plan(native,&bad,false,&bandwidth) && !memcmp(&bandwidth,&zero_bandwidth,sizeof(bandwidth)));cases++;
    CHECK(native->bandwidth_registers(native,&mode.timing,false,RX6600_HUBP_PREPARE) && native->hubp_transaction.prepared && !native->hubp_transaction.dirty && m.writes==completed_writes);cases++;
    /* No writes until all real pipes are stopped and HUBP is drained. */
    CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_APPLY) && m.writes==completed_writes);
    m.otg[3][DCN302_R_CONTROL]=0;m.surface[1][DCN302_SURFACE_R_HUBP]=2;
    CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_APPLY) && m.writes==completed_writes);
    CHECK(native->bandwidth_registers(native,NULL,false,RX6600_HUBP_BLANK) && (m.surface[1][DCN302_SURFACE_R_HUBP]&0x1003u)==3u);cases++;
    unsigned blanked=m.writes;
    native->smu.floor_mhz[DCN302_SMU_DCEFCLK]=599;
    CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_APPLY) && m.writes==blanked);
    native->smu.floor_mhz[DCN302_SMU_DCEFCLK]=600;m.dfs_pll++;
    CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_APPLY) && m.writes==blanked);m.dfs_pll--;cases++;
    CHECK(native->bandwidth_registers(native,NULL,false,RX6600_HUBP_APPLY) && native->hubp_transaction.applied && native->hubp_transaction.dirty && m.writes>blanked && !m.invalid);cases++;
    triggers=m.smu_triggers;
    CHECK(!native->clock_floor(native,DCN302_SMU_DCEFCLK,599,&acknowledged) && m.smu_triggers==triggers);
    CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_CANCEL));
    CHECK(native->bandwidth_registers(native,NULL,false,RX6600_HUBP_RESTORE) && !native->hubp_transaction.dirty && !native->hubp_transaction.applied && !native->hubp_transaction.poisoned);cases++;
    CHECK(native->bandwidth_registers(native,NULL,false,RX6600_HUBP_CANCEL) && !native->hubp_transaction.prepared);
    CHECK(!native->bandwidth_registers(native,NULL,false,(enum rx6600_hubp_operation)-1));
    m.surface[1][DCN302_SURFACE_R_HUBP]=0;m.otg[3][DCN302_R_CONTROL]=control;
    completed_writes=m.writes;
    m.time+=500000;out.poll(out.state);CHECK(out.read_mode(out.state,&mode) && out.set_mode(out.state,&mode.timing));
    mode.timing.pixel_khz/=2;CHECK(!out.set_mode(out.state,&mode.timing));out.shutdown(out.state);CHECK(!out.read_mode(out.state,&mode) && m.writes==completed_writes && !m.invalid);cases++;
    init(&m,&k,4,0,3,2,1,2,4,1,1);k.size=104;memset(&out,0xa5,sizeof(out));nexis_gpu_instance zero={0};
    CHECK(entry(&k,&out)==RX6600_INPUT && !memcmp(&out,&zero,sizeof(out)) && !m.reads && !m.writes);cases++;
}
static void pic(const char *path,unsigned floor_offset,unsigned dfs_offset,unsigned bandwidth_offset,unsigned hubp_offset){
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length>64);rewind(f);
    uint8_t *data=malloc((size_t)length);CHECK(data && fread(data,1,(size_t)length,f)==(size_t)length);fclose(f);
    nexis_gpu_image m;CHECK(nexis_gpu_image_parse(data,(size_t)length,0x1002,0x73ff,&m));
    CHECK(floor_offset<m.text_bytes && dfs_offset<m.text_bytes && bandwidth_offset<m.text_bytes && hubp_offset<m.text_bytes);
    uint8_t *bases[2];
    for(unsigned n=0;n<2;n++){
        bases[n]=VirtualAlloc(NULL,m.memory_bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);uint8_t *base=bases[n];CHECK(base && (!n || base!=bases[0]));memcpy(base,data+64,m.image_bytes);DWORD old;
        unsigned code=(m.text_bytes+4095)&~4095u;CHECK(VirtualProtect(base,code,PAGE_EXECUTE_READ,&old));
        if(code<m.writable_offset)CHECK(VirtualProtect(base+code,m.writable_offset-code,PAGE_READONLY,&old));
        retained((void *)(base+m.entry),(uintptr_t)(base+floor_offset),(uintptr_t)(base+dfs_offset),(uintptr_t)(base+bandwidth_offset),(uintptr_t)(base+hubp_offset));
    }
    for(unsigned n=0;n<2;n++)CHECK(VirtualFree(bases[n],0,MEM_RELEASE));
    free(data);
}
int main(int argc,char **argv){CHECK(argc==6);normal();failures();retained(driver_init_v2,0,0,0,0);pic(argv[1],(unsigned)strtoul(argv[2],NULL,10),(unsigned)strtoul(argv[3],NULL,10),(unsigned)strtoul(argv[4],NULL,10),(unsigned)strtoul(argv[5],NULL,10));
    printf("{\"passed\":true,\"cases\":%u,\"native_rx6600_retained_backend\":true,\"real_pic_callbacks_executed\":true,\"distinct_pic_bases_verified\":true,\"native_smu_probe_integrated\":true,\"real_pic_clock_floor_command_executed\":true,\"real_pic_display_clock_transaction_executed\":true,\"real_pic_bandwidth_plan_executed\":true,\"atom_memory_topology_integrated\":true,\"mode_changing_transaction_complete\":false,\"firmware_mailbox_writes\":true,\"clock_floor_changes_modeled\":true,\"display_clock_writes_modeled\":true,\"physical_hardware_verified\":false}\n",cases);return 0;}
