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
static void NEXIS_GPU_CALL tlog(void *context,const char *text){(void)context;if(getenv("RX6600_TRACE"))fprintf(stderr,"[module] %s\n",text);}
typedef struct {
    uint8_t rom[2048];uint32_t otg[5][DCN302_TIMING_REGISTER_COUNT],dig[5][DCN302_HDMI_REGISTER_COUNT],surface[5][DCN302_SURFACE_REGISTER_COUNT],hpd[5];
    uint32_t fb_base,fb_top,fb_offset,period;
    uint32_t dfs_pll,dfs_dentist,dfs_control,dfs_dto[5];
    uint32_t hubp[5][DCN302_HUBP_REGISTER_COUNT];
    uint32_t dpp[5][DCN302_DPP_REGISTER_COUNT];
    uint32_t hubbub[DCN302_HUBBUB_REGISTER_COUNT],reference,timer;
    uint32_t firmware_regs[4];unsigned firmware_reads,firmware_writes;
    unsigned firmware_read_loss,time_fault,delays,fail_delay;
    uint32_t smu_argument,smu_response,smu_version,smu_interface,smu_header,smu_features,smu_status,smu_floor;
    uint64_t time;unsigned reads,writes,queries,fail_read,fail_query,smu_writes,smu_triggers,smu_clock_requests;
    unsigned fail_mmio_write,ignore_mmio_write,activate_mmio_write,hubbub_writes,hubp_writes,timing_writes,dpp_writes,loss_mmio_write,loss_kind;
    bool posted;
    bool frozen,invalid;
    nexis_gpu_resource vram,registers;
} model;
static void p16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t *p,uint32_t v){for(unsigned n=0;n<4;n++)p[n]=(uint8_t)(v>>(8*n));}
static void firmware_loss(model *m){
    switch(m->loss_kind){
        case 0:m->otg[4][DCN302_R_CONTROL]|=DCN302_MASTER_ACTIVE_MASK;break;
        case 1:m->surface[1][DCN302_SURFACE_R_CLOCK]&=~0x200000u;break;
        case 2:m->registers.base+=4096;break;case 3:m->timer^=0x10000;break;
        case 4:m->hubp[1][DCN302_HUBP_R_DCN_SURF0_TTU_CNTL0]^=1;break;
        case 5:m->dpp[1][DCN302_DPP_R_STATUS]|=4;break;
        case 6:m->hpd[1]=0;break;
    }
}
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
    p16(b+1600,84);b[1602]=4;b[1603]=4;p32(b+1608,60000);p16(b+1612,10000);p16(b+1614,5000);p16(b+1636,2700);
    b[1642]=5;b[1644]=6;b[1645]=6;b[1646]=6;
    unsigned sum=0;for(unsigned n=0;n<2047;n++)sum+=b[n];b[2047]=(uint8_t)(0-sum);
}
static bool NEXIS_GPU_CALL rd(void *ctx,unsigned bar,uint32_t offset,uint32_t *out){
    model *m=ctx;CHECK(bar==5);if(++m->reads==m->fail_read)return false;
    const uint32_t firmware_bytes[]={0,4,0x8000,0x8004};
    for(unsigned n=0;n<4;n++)if(offset==firmware_bytes[n]){
        *out=m->firmware_regs[n];m->firmware_reads++;
        if(m->firmware_reads==m->firmware_read_loss)firmware_loss(m);
        if(m->time_fault==1)m->time-=1000;else if(m->time_fault==2)m->time+=3000000;
        return true;
    }
    if(offset==DCN302_SMU_RESPONSE_BYTES){*out=m->smu_response;return true;}
    if(offset==DCN302_SMU_ARGUMENT_BYTES){*out=m->smu_argument;return true;}
    if(offset==DCN302_HUBBUB_REF_BYTES){*out=m->reference;return true;}
    if(offset==DCN302_HUBBUB_TIMER_BYTES){*out=m->timer;return true;}
    for(unsigned r=0;r<DCN302_HUBBUB_REGISTER_COUNT;r++)if(offset==dcn302_hubbub_register_bytes[r]){*out=m->hubbub[r];return true;}
    if(offset==DCN302_DFS_PLL_BYTES){*out=m->dfs_pll;return true;}
    if(offset==DCN302_DFS_DENTIST_BYTES){*out=m->dfs_dentist;return true;}
    if(offset==DCN302_DFS_DTO_CTRL_BYTES){*out=m->dfs_control;return true;}
    for(unsigned i=0;i<5;i++)if(offset==dcn302_dfs_dto_bytes[i]){*out=m->dfs_dto[i];return true;}
    for(unsigned i=0;i<5;i++){
        for(unsigned r=0;r<DCN302_TIMING_REGISTER_COUNT;r++)if(offset==dcn302_timing_register_bytes[i][r]){
            *out=r==DCN302_R_FRAME_COUNT?(uint32_t)(m->time/m->period)&DCN302_FRAME_COUNT_MASK:m->otg[i][r];return true;
        }
        for(unsigned r=0;r<DCN302_HDMI_REGISTER_COUNT;r++)if(offset==dcn302_hdmi_register_bytes[i][r]){*out=m->dig[i][r];return true;}
        for(unsigned r=0;r<DCN302_SURFACE_REGISTER_COUNT;r++)if(offset==dcn302_surface_register_bytes[i][r]){*out=m->surface[i][r];return true;}
        for(unsigned r=0;r<DCN302_HUBP_REGISTER_COUNT;r++)if(offset==dcn302_hubp_register_bytes[i][r]){*out=m->hubp[i][r];return true;}
        for(unsigned r=0;r<DCN302_DPP_REGISTER_COUNT;r++)if(offset==dcn302_dpp_register_bytes[i][r]){*out=m->dpp[i][r];return true;}
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
    const uint32_t firmware_bytes[]={0,4,0x8000,0x8004};
    for(unsigned n=0;n<4;n++)if(offset==firmware_bytes[n]){
        m->firmware_writes++;
        if(m->writes==m->ignore_mmio_write)return true;
        if(m->writes==m->fail_mmio_write && !m->posted)return false;
        m->firmware_regs[n]=value;
        if(m->writes==m->loss_mmio_write)firmware_loss(m);
        if(m->time_fault==3)m->time-=1000;else if(m->time_fault==4)m->time+=3000000;
        return m->writes!=m->fail_mmio_write;
    }
    for(unsigned r=0;r<DCN302_HUBBUB_REGISTER_COUNT;r++)if(offset==dcn302_hubbub_register_bytes[r]){
        CHECK(!((value^m->hubbub[r])&~dcn302_hubbub_owned[r]));
        if(r)CHECK((m->hubbub[0]&0x33)==0x22);
        if(m->writes==m->ignore_mmio_write)return true;
        if(m->writes==m->fail_mmio_write && !m->posted)return false;
        m->hubbub[r]=value;m->hubbub_writes++;
        if(m->writes==m->activate_mmio_write)m->otg[4][DCN302_R_CONTROL]|=DCN302_MASTER_ACTIVE_MASK;
        return m->writes!=m->fail_mmio_write;
    }
    if(offset==DCN302_DFS_DENTIST_BYTES){
        m->dfs_dentist=(value&~DCN302_DFS_DISP_READ_MASK)|((value&127)<<8)|DCN302_DFS_DISP_DONE_MASK|DCN302_DFS_DPP_DONE_MASK;return true;
    }
    if(offset==DCN302_DFS_DTO_CTRL_BYTES){m->dfs_control=value;return true;}
    for(unsigned i=0;i<5;i++)if(offset==dcn302_dfs_dto_bytes[i]){m->dfs_dto[i]=value;return true;}
    for(unsigned i=0;i<5;i++)for(unsigned r=0;r<DCN302_DPP_REGISTER_COUNT;r++)if(offset==dcn302_dpp_register_bytes[i][r]){
        CHECK(r<DCN302_DPP_PROGRAM_COUNT && dcn302_dpp_owned[r] && !(value&dcn302_dpp_readonly[r]) && (m->hubbub[0]&0x33)==0x22);
        CHECK(!((value^m->dpp[i][r])&~(dcn302_dpp_owned[r]|dcn302_dpp_readonly[r])));
        if(m->writes==m->ignore_mmio_write)return true;
        if(m->writes==m->fail_mmio_write && !m->posted)return false;
        m->dpp[i][r]=value|(m->dpp[i][r]&dcn302_dpp_readonly[r]);m->dpp_writes++;
        if(m->writes==m->activate_mmio_write)m->otg[4][DCN302_R_CONTROL]|=DCN302_MASTER_ACTIVE_MASK;
        if(m->writes==m->loss_mmio_write)switch(m->loss_kind){
            case 0:m->otg[4][DCN302_R_CONTROL]|=DCN302_MASTER_ACTIVE_MASK;break;
            case 1:m->surface[i][DCN302_SURFACE_R_CLOCK]&=~0x200000u;break;
            case 2:m->registers.base+=4096;break;case 3:m->timer^=0x10000;break;
            case 4:m->hubp[i][DCN302_HUBP_R_DCN_SURF0_TTU_CNTL0]^=1;break;
            case 5:m->dpp[i][DCN302_DPP_R_STATUS]|=4;break;
        }
        return m->writes!=m->fail_mmio_write;
    }
    for(unsigned i=0;i<5;i++)for(unsigned r=0;r<DCN302_TIMING_REGISTER_COUNT;r++)if(offset==dcn302_timing_register_bytes[i][r]){
        CHECK(dcn302_timing_owned[r] && !(value&dcn302_timing_write_excluded[r]) && (m->hubbub[0]&0x33)==0x22);
        CHECK(!((value^m->otg[i][r])&~(dcn302_timing_owned[r]|dcn302_timing_write_excluded[r])));
        if(r!=DCN302_R_LOCK && r!=DCN302_R_GLOBAL2 && r!=DCN302_TIMING_R_GLOBAL0 && r!=DCN302_TIMING_R_GLOBAL1 && r!=DCN302_TIMING_R_DBUF){
            CHECK((m->otg[i][DCN302_R_LOCK]&0x101)==0x101 &&
                (m->otg[i][DCN302_R_GLOBAL2]&dcn302_timing_owned[DCN302_R_GLOBAL2])==(i<<DCN302_TIMING_LOCK_SELECT_SHIFT));
        }
        if(m->writes==m->ignore_mmio_write)return true;
        if(m->writes==m->fail_mmio_write && !m->posted)return false;
        m->otg[i][r]=value|(m->otg[i][r]&dcn302_timing_write_excluded[r]);
        if(r==DCN302_R_LOCK){if(value&1)m->otg[i][r]|=DCN302_LOCK_STATUS_MASK;else m->otg[i][r]&=~DCN302_LOCK_STATUS_MASK;}
        m->timing_writes++;
        if(m->writes==m->activate_mmio_write)m->otg[4][DCN302_R_CONTROL]|=DCN302_MASTER_ACTIVE_MASK;
        if(m->writes==m->loss_mmio_write)switch(m->loss_kind){
            case 0:m->otg[4][DCN302_R_CONTROL]|=DCN302_MASTER_ACTIVE_MASK;break;
            case 1:m->otg[i][DCN302_R_CLOCK]&=~DCN302_CLOCK_ON_MASK;break;
            case 2:m->registers.base+=4096;break;case 3:m->timer^=0x10000;break;
            case 4:m->hubp[1][DCN302_HUBP_R_DCN_SURF0_TTU_CNTL0]^=1;break;
        }
        return m->writes!=m->fail_mmio_write;
    }
    for(unsigned i=0;i<5;i++)for(unsigned r=0;r<DCN302_HUBP_REGISTER_COUNT;r++)if(offset==dcn302_hubp_register_bytes[i][r]){
        CHECK(!(value&(dcn302_hubp_forbidden[r]|dcn302_hubp_readonly[r])));
        uint32_t current=m->hubp[i][r];
        for(unsigned sr=0;sr<DCN302_SURFACE_REGISTER_COUNT;sr++)if(offset==dcn302_surface_register_bytes[i][sr])current=m->surface[i][sr];
        bool blank=r==DCN302_HUBP_R_DCHUBP_CNTL && ((current^value)&0x1001u);
        if(!blank)CHECK((m->hubbub[0]&0x33)==0x22);
        if(m->writes==m->ignore_mmio_write)return true;
        if(m->writes==m->fail_mmio_write && !m->posted)return false;
        uint32_t next=(current&dcn302_hubp_readonly[r])|value;
        if(r==DCN302_HUBP_R_DCHUBP_CNTL && (value&0x1001u)==1u)next|=2;
        m->hubp[i][r]=next;
        for(unsigned sr=0;sr<DCN302_SURFACE_REGISTER_COUNT;sr++)if(offset==dcn302_surface_register_bytes[i][sr])m->surface[i][sr]=next;
        if(!blank)m->hubp_writes++;
        if(m->writes==m->activate_mmio_write)m->otg[4][DCN302_R_CONTROL]|=DCN302_MASTER_ACTIVE_MASK;
        return m->writes!=m->fail_mmio_write;
    }
    m->invalid=true;return false;
}
static bool NEXIS_GPU_CALL resource(void *ctx,unsigned bar,nexis_gpu_resource *out){
    model *m=ctx;memset(out,0,sizeof(*out));if(++m->queries==m->fail_query)return false;
    if(bar==0)*out=m->vram;else if(bar==5)*out=m->registers;else return false;return true;
}
static bool NEXIS_GPU_CALL delay(void *ctx,uint32_t us){
    model *m=ctx;CHECK(us==1 || us==10);if(++m->delays==m->fail_delay)return false;
    if(!m->frozen)m->time+=us;
    if(m->time_fault==5)m->time-=1000;else if(m->time_fault==6)m->time+=3000000;
    return true;
}
static uint64_t NEXIS_GPU_CALL now(void *ctx){return ((model *)ctx)->time;}
static void init(model *m,nexis_gpu_services *k,unsigned link,unsigned stream,unsigned pipe,unsigned bus,unsigned hpd,unsigned opp,unsigned mpcc,unsigned hubp,unsigned format){
    memset(m,0,sizeof(*m));memset(k,0,sizeof(*k));rom_init(m,link,bus,hpd);m->period=4166;
    m->timer=0x1002;
    m->smu_response=m->smu_status=1;m->smu_version=0x3a0100;m->smu_header=1;m->smu_interface=0x40;m->smu_features=3;
    m->dfs_pll=36|0x80000000u;m->dfs_dentist=24|(24u<<8)|(24u<<24)|DCN302_DFS_DISP_DONE_MASK|DCN302_DFS_DPP_DONE_MASK;
    *k=(nexis_gpu_services){.abi=2,.size=136,.vendor=0x1002,.device=0x73ff,.width=1920,.height=1080,.pitch=2048,.format=format,
        .framebuffer=0x800200000ULL,.framebuffer_bytes=2048u*4*1080,.rom=m->rom,.rom_bytes=2048,.service_context=m,
        .read32=rd,.write32=wr,.time_us=now,.delay_us=delay,.resource=resource,.log=tlog};
    m->vram=(nexis_gpu_resource){0x800000000ULL,0x200000000ULL,7,0};m->registers=(nexis_gpu_resource){0xb0000000,0x100000,9,0};
    m->hpd[hpd]=DCN302_HPD_SENSE_MASK|DCN302_HPD_DELAYED_MASK;
    m->dig[link][DCN302_HDMI_R_BE]=HSET(HSET(HSET(0,LINK_MODE,3),FE_SOURCE,1u<<stream),HPD,hpd);
    m->dig[link][DCN302_HDMI_R_BE_ENABLE]=DCN302_HDMI_LINK_ENABLE_MASK|DCN302_HDMI_LINK_CLOCK_MASK;
    m->dig[stream][DCN302_HDMI_R_FE]=HSET(0,PIPE,pipe);
    uint32_t *o=m->otg[pipe];o[DCN302_R_CONTROL]=DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK;
    o[DCN302_R_CLOCK]=DCN302_CLOCK_ENABLE_MASK|DCN302_CLOCK_ON_MASK;o[DCN302_R_SOURCE]=opp<<DCN302_SEG0_SHIFT;
    o[DCN302_R_H_TOTAL]=2079;o[DCN302_R_H_BLANK]=2032|(112u<<16);o[DCN302_R_H_SYNC]=64u<<16;
    o[DCN302_R_V_TOTAL]=1117;o[DCN302_R_V_BLANK]=1115|(35u<<16);o[DCN302_R_V_SYNC]=5u<<16;
    o[DCN302_TIMING_R_GLOBAL0]=0x800a0001;o[DCN302_TIMING_R_GLOBAL1]=0x80140002;
    o[DCN302_R_GLOBAL2]=0x400|(2u<<DCN302_TIMING_LOCK_SELECT_SHIFT);o[DCN302_TIMING_R_DBUF]=0x02800800;
    m->surface[opp][DCN302_SURFACE_R_OUT_MUX]=mpcc;m->surface[mpcc][DCN302_SURFACE_R_TOP]=hubp;
    m->surface[mpcc][DCN302_SURFACE_R_BOTTOM]=15;m->surface[mpcc][DCN302_SURFACE_R_OPP]=opp;m->surface[mpcc][DCN302_SURFACE_R_MODE]=2;
    uint32_t *h=m->surface[hubp];h[DCN302_SURFACE_R_CONFIG]=8;h[DCN302_SURFACE_R_VIEW_SIZE]=SSET(SSET(0,WIDTH,1920),HEIGHT,1080);
    h[DCN302_SURFACE_R_CLOCK]=0xf00001u;
    h[DCN302_SURFACE_R_CROSSBAR]=SSET(SSET(SSET(0,RED,format?3:2),GREEN,1),BLUE,format?2:3);h[DCN302_SURFACE_R_PITCH]=2047;
    h[DCN302_SURFACE_R_ADDRESS]=h[DCN302_SURFACE_R_INUSE]=0x200000;h[DCN302_SURFACE_R_ADDRESS_HIGH]=h[DCN302_SURFACE_R_INUSE_HIGH]=0x80;
    for(unsigned i=0;i<5;i++){
        for(unsigned r=0;r<DCN302_DPP_PROGRAM_COUNT;r++)m->dpp[i][r]=dcn302_dpp_owned[r]|(0xa5c655a5&~(dcn302_dpp_owned[r]|dcn302_dpp_readonly[r]));
        m->dpp[i][DCN302_DPP_R_CM]&=~1u;m->dpp[i][DCN302_DPP_R_CM]|=0x100;
        m->dpp[i][DCN302_DPP_R_FORMAT]|=0x100000;m->dpp[i][DCN302_DPP_R_CSC]|=0xc;
        m->dpp[i][DCN302_DPP_R_CNVC_CURSOR]|=0x10000;
        m->dpp[i][DCN302_DPP_R_MODE]|=0x1000;m->dpp[i][DCN302_DPP_R_LB_MEMORY]|=0x03030000;
        m->dpp[i][DCN302_DPP_R_STATUS]=3;
        m->dpp[i][DCN302_DPP_R_LOCAL_CLOCK]=0x10;
    }
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
        CHECK(rx6600_set_mode(&s,&out.timing));nexis_gpu_timing changed=out.timing;changed.pixel_khz=700000; /* outside the connector range: rejected before any register is touched (a feasible switch is covered by test_rx6600_modeset.c) */
        CHECK(!rx6600_set_mode(&s,&changed) && s.ready && !m.writes);
        m.time+=500000;rx6600_poll(&s);CHECK(s.ready && rx6600_read_mode(&s,&out));
        rx6600_shutdown(&s);CHECK(!s.ready && !rx6600_read_mode(&s,&out) && !out.flags && !m.invalid && !m.writes);cases++;
    }
}
static void failures(void){
    model m;nexis_gpu_services k;rx6600_state s;nexis_gpu_scanout out,zero={0};
    /* A 512 KiB register BAR is enough: the highest register the driver uses (DFS PLL 0x5c040) is below it. */
    init(&m,&k,4,0,3,2,1,2,4,1,1);m.registers.bytes=0x80000;
    CHECK(rx6600_probe(&s,&k)==RX6600_OK && s.ready && !s.busy);rx6600_shutdown(&s);cases++;
    for(unsigned fault=0;fault<32;fault++){
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
            case 27:m.reference=1;break;case 28:m.timer=2;break;case 29:m.timer=0x1001;break;
            case 30:p16(m.rom+1612,2700);break;case 31:p16(m.rom+1612,12001);break;
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
    for(unsigned fault=0;fault<23;fault++){
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
            case 20:m.timer^=0x10000;break;case 21:m.reference=1;break;case 22:m.timer&=~0x1000u;break;
        }
        memset(&out,0xa5,sizeof(out));CHECK(!rx6600_read_mode(&s,&out) && !memcmp(&out,&zero,sizeof(out)) && !s.ready && !s.busy && !m.writes);cases++;
    }
    init(&m,&k,4,0,3,2,1,2,4,1,1);CHECK(rx6600_probe(&s,&k)==RX6600_OK);s.busy=true;
    CHECK(!rx6600_read_mode(&s,&out) && s.ready);rx6600_poll(&s);CHECK(s.ready);cases++;
    init(&m,&k,4,0,3,2,1,2,4,1,1);CHECK(rx6600_probe(&s,&k)==RX6600_OK);m.hpd[1]=0;rx6600_poll(&s);CHECK(!s.ready);cases++;
}
typedef bool (NEXIS_GPU_CALL *pic_floor)(void *,enum dcn302_smu_clock,uint32_t,uint32_t *);
static void retained(nexis_gpu_entry_v2 entry,uintptr_t floor_address,uintptr_t dfs_address,uintptr_t bandwidth_address,uintptr_t hubp_address,uintptr_t timing_address,uintptr_t dpp_address){
    model m;nexis_gpu_services k;nexis_gpu_instance out;nexis_gpu_scanout mode;
    init(&m,&k,4,0,3,2,1,2,4,1,1);memset(&out,0xa5,sizeof(out));CHECK(entry(&k,&out)==0 && out.abi==2 && out.size==sizeof(out));
    CHECK(out.read_mode && out.set_mode && out.poll && out.shutdown && out.state && out.state_bytes && out.hdmi && out.scdc);
    CHECK(out.read_mode(out.state,&mode) && mode.flags==11 && !(mode.flags&NEXIS_GPU_SCANOUT_AUDIO));
    rx6600_state *native=out.state;uint32_t acknowledged=0;
    if(bandwidth_address)CHECK((uintptr_t)native->bandwidth_plan==bandwidth_address);
    if(hubp_address)CHECK((uintptr_t)native->bandwidth_registers==hubp_address);
    if(timing_address)CHECK((uintptr_t)native->timing_registers==timing_address);
    if(dpp_address)CHECK((uintptr_t)native->dpp_registers==dpp_address);
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
    unsigned repeat=m.writes;CHECK(!native->display_clocks(native,NULL,RX6600_DFS_APPLY) && native->ready && m.writes==repeat);
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
    CHECK(native->dml_job.input.channels==8 && native->dml_job.input.channel_bytes==2 && native->dml_job.input.dram_mts==8928 && native->dml_job.input.ref_khz==50000);
    CHECK(native->dml_job.input.pipe==3 && native->dml_job.input.dpp_khz==native->dfs.pipe_khz[1]);
    CHECK(native->dml_workspace.pipe.pipe.scale_ratio_depth.lb_depth==dm_lb_16 && native->dml_workspace.lib.vba.LBBitPerPixel[0]==48);
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
    nexis_gpu_timing target=mode.timing;target.hsync_start=1940;target.hsync_end=1972;
    CHECK(native->bandwidth_registers(native,&target,false,RX6600_HUBP_PREPARE) && native->hubp_transaction.prepared && native->hubbub_transaction.prepared && native->timing_transaction.prepared && native->dpp_transaction.prepared && !native->hubp_transaction.dirty && m.writes==completed_writes);cases++;
    CHECK(native->timing_transaction.sync.vstartup==native->dml_job.output.vstartup &&
        native->timing_transaction.sync.vready==native->dml_job.output.vready_offset &&
        native->timing_transaction.sync.vupdate_offset==native->dml_job.output.vupdate_offset &&
        native->timing_transaction.sync.vupdate_width==native->dml_job.output.vupdate_width);
    CHECK(!native->timing_registers(native,RX6600_TIMING_APPLY) && m.writes==completed_writes);
    CHECK(!native->dpp_registers(native,RX6600_DPP_APPLY) && m.writes==completed_writes);
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
    m.timer^=0x10000;
    CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_APPLY) && m.writes==blanked);m.timer^=0x10000;cases++;
    CHECK(native->bandwidth_registers(native,NULL,false,RX6600_HUBP_APPLY) && native->hubp_transaction.applied && native->hubp_transaction.dirty && native->hubbub_transaction.applied && native->hubbub_transaction.dirty && (m.hubbub[0]&0x33)==0x22 && m.writes>blanked && !m.invalid);cases++;
    unsigned bandwidth_writes=m.writes-blanked,wm_writes=m.hubbub_writes;
    triggers=m.smu_triggers;
    CHECK(!native->clock_floor(native,DCN302_SMU_DCEFCLK,599,&acknowledged) && m.smu_triggers==triggers);
    CHECK(!native->clock_floor(native,DCN302_SMU_UCLK,600,&acknowledged) && !acknowledged && m.smu_triggers==triggers);
    /* Applied plans still bind floors if no register write was necessary. */
    native->hubp_transaction.dirty=native->hubbub_transaction.dirty=false;
    CHECK(!native->clock_floor(native,DCN302_SMU_DCEFCLK,599,&acknowledged) && m.smu_triggers==triggers);
    native->hubp_transaction.dirty=native->hubbub_transaction.dirty=true;
    CHECK(!native->display_clocks(native,NULL,RX6600_DFS_RESTORE));cases++;
    uint32_t old_dpp[5][DCN302_DPP_REGISTER_COUNT];memcpy(old_dpp,m.dpp,sizeof(old_dpp));
    unsigned no_scaler=m.writes;
    CHECK(!native->timing_registers(native,RX6600_TIMING_APPLY) && m.writes==no_scaler);
    CHECK(native->dpp_registers(native,RX6600_DPP_APPLY) && native->dpp_transaction.applied && m.dpp_writes &&
        !(m.dpp[1][DCN302_DPP_R_CURSOR]&1) && !(m.dpp[1][DCN302_DPP_R_CNVC_CURSOR]&1) &&
        (m.dpp[1][DCN302_DPP_R_LB_MEMORY]&dcn302_dpp_owned[DCN302_DPP_R_LB_MEMORY])==0x3f00);
    CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_RESTORE) &&
        !native->bandwidth_registers(native,NULL,false,RX6600_HUBP_CANCEL));cases++;
    uint32_t old_otg[5][DCN302_TIMING_REGISTER_COUNT];memcpy(old_otg,m.otg,sizeof(old_otg));
    unsigned timing_start=m.writes;
    CHECK(native->timing_registers(native,RX6600_TIMING_APPLY) && native->timing_transaction.applied && native->timing_transaction.dirty && m.timing_writes);
    repeat=m.writes;CHECK(!native->timing_registers(native,RX6600_TIMING_APPLY) && native->ready && m.writes==repeat);
    CHECK((m.otg[3][DCN302_R_V_STARTUP]&1023)==native->dml_job.output.vstartup &&
        (m.otg[3][DCN302_R_V_READY]&0xffff)==native->dml_job.output.vready_offset &&
        m.otg[3][DCN302_R_H_BLANK]==(140u<<16|2060) && m.otg[3][DCN302_R_H_SYNC]==32u<<16);
    unsigned timing_writes=m.writes-timing_start;
    CHECK(!native->dpp_registers(native,RX6600_DPP_RESTORE));
    CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_RESTORE) &&
        !native->bandwidth_registers(native,NULL,false,RX6600_HUBP_CANCEL) &&
        !native->display_clocks(native,NULL,RX6600_DFS_RESTORE));
    CHECK(native->timing_registers(native,RX6600_TIMING_RESTORE) && !native->timing_transaction.dirty &&
        !native->timing_transaction.poisoned && !memcmp(m.otg,old_otg,sizeof(old_otg)));cases++;
    /* Every owned scaler/conversion/cursor field belongs to the timing
     * dependency. A stale or externally changed color path blocks writes. */
    for(unsigned r=0;r<DCN302_DPP_PROGRAM_COUNT;r++){
        unsigned stopped=m.writes;uint32_t field=dcn302_dpp_owned[r]&(~dcn302_dpp_owned[r]+1u);
        m.dpp[1][r]^=field;
        CHECK(!native->timing_registers(native,RX6600_TIMING_APPLY) && native->ready && !native->timing_transaction.dirty && m.writes==stopped);
        m.dpp[1][r]^=field;cases++;
    }
    unsigned no_dpp=m.writes;native->dpp_transaction.applied=false;
    CHECK(!native->timing_registers(native,RX6600_TIMING_APPLY) && native->ready && m.writes==no_dpp);
    native->dpp_transaction.applied=true;m.dpp[1][DCN302_DPP_R_LOCAL_CLOCK]=0;
    CHECK(!native->timing_registers(native,RX6600_TIMING_APPLY) && native->ready && m.writes==no_dpp);
    m.dpp[1][DCN302_DPP_R_LOCAL_CLOCK]=0x10;cases++;
    for(unsigned fault=0;fault<13;fault++){
        unsigned stopped=m.writes;
        switch(fault){case 0:m.dfs_pll++;break;case 1:m.timer^=0x10000;break;case 2:native->smu.floor_mhz[DCN302_SMU_DCEFCLK]=599;break;
        case 3:m.hubbub[1]^=1;break;case 4:m.hubp[1][DCN302_HUBP_R_DCN_SURF0_TTU_CNTL0]^=1;break;
        case 5:m.surface[1][DCN302_SURFACE_R_HUBP]&=~1u;break;case 6:native->smu.poisoned=true;break;
        case 7:m.registers.base+=4096;break;case 8:m.otg[4][DCN302_R_CONTROL]=DCN302_MASTER_ACTIVE_MASK;break;
        case 9:native->smu.floor_known[DCN302_SMU_DISPCLK]=false;break;case 10:native->smu.floor_known[DCN302_SMU_DPPCLK]=false;break;
        case 11:native->smu.floor_mhz[DCN302_SMU_DISPCLK]=1;break;case 12:native->smu.floor_mhz[DCN302_SMU_DPPCLK]=1;break;}
        CHECK(!native->timing_registers(native,RX6600_TIMING_APPLY) && native->ready && !native->timing_transaction.dirty && m.writes==stopped);
        switch(fault){case 0:m.dfs_pll--;break;case 1:m.timer^=0x10000;break;case 2:native->smu.floor_mhz[DCN302_SMU_DCEFCLK]=600;break;
        case 3:m.hubbub[1]^=1;break;case 4:m.hubp[1][DCN302_HUBP_R_DCN_SURF0_TTU_CNTL0]^=1;break;
        case 5:m.surface[1][DCN302_SURFACE_R_HUBP]|=1;break;case 6:native->smu.poisoned=false;break;
        case 7:m.registers.base-=4096;break;case 8:m.otg[4][DCN302_R_CONTROL]=0;break;
        case 9:native->smu.floor_known[DCN302_SMU_DISPCLK]=true;break;case 10:native->smu.floor_known[DCN302_SMU_DPPCLK]=true;break;
        case 11:native->smu.floor_mhz[DCN302_SMU_DISPCLK]=730;break;case 12:native->smu.floor_mhz[DCN302_SMU_DPPCLK]=730;break;}cases++;
    }
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=timing_writes;n++){
        if(kind==2)m.ignore_mmio_write=m.writes+n;else{m.fail_mmio_write=m.writes+n;m.posted=kind==1;}
        CHECK(!native->timing_registers(native,RX6600_TIMING_APPLY) && native->ready && !native->timing_transaction.applied &&
            !native->timing_transaction.dirty && !native->timing_transaction.poisoned && !m.invalid && !memcmp(m.otg,old_otg,sizeof(old_otg)) &&
            native->hubp_transaction.applied && native->hubbub_transaction.applied && (m.hubbub[0]&0x33)==0x22);
        m.ignore_mmio_write=m.fail_mmio_write=0;m.posted=false;cases++;
    }
    /* A partially restored timing must retain both fetch and forced WM
     * policy. Retry the actual retained rollback before releasing either. */
    CHECK(native->timing_registers(native,RX6600_TIMING_APPLY));unsigned restore_start=m.writes;
    CHECK(native->timing_registers(native,RX6600_TIMING_RESTORE));unsigned restore_writes=m.writes-restore_start;
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=restore_writes;n++){
        CHECK(native->timing_registers(native,RX6600_TIMING_APPLY));
        if(kind==2)m.ignore_mmio_write=m.writes+n;else{m.fail_mmio_write=m.writes+n;m.posted=kind==1;}
        CHECK(!native->timing_registers(native,RX6600_TIMING_RESTORE) && !native->ready && native->timing_transaction.poisoned &&
            native->hubp_transaction.applied && native->hubbub_transaction.applied && (m.hubbub[0]&0x33)==0x22);
        CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_RESTORE));
        m.ignore_mmio_write=m.fail_mmio_write=0;m.posted=false;
        CHECK(native->timing_registers(native,RX6600_TIMING_RESTORE) && !native->timing_transaction.dirty &&
            !native->timing_transaction.poisoned && !native->ready && !memcmp(m.otg,old_otg,sizeof(old_otg)));
        /* A new native parent probe is mandatory after the poison. The
         * next independent failure scenario uses a fresh full handoff. */
        CHECK(native->dpp_registers(native,RX6600_DPP_RESTORE) && !native->dpp_transaction.applied && !memcmp(m.dpp,old_dpp,sizeof(old_dpp)));
        CHECK(native->bandwidth_registers(native,NULL,false,RX6600_HUBP_RESTORE));
        m.surface[1][DCN302_SURFACE_R_HUBP]=0;m.otg[3][DCN302_R_CONTROL]=control;
        CHECK(entry(&k,&out)==0);native=out.state;
        const enum dcn302_smu_clock floors[]={DCN302_SMU_UCLK,DCN302_SMU_SOCCLK,DCN302_SMU_DCEFCLK,DCN302_SMU_PHYCLK};
        for(unsigned f=0;f<4;f++)CHECK(native->clock_floor(native,floors[f],f?600:558,&acknowledged));
        CHECK(native->clock_floor(native,DCN302_SMU_DISPCLK,native->dfs.disp_floor_mhz,&acknowledged));
        CHECK(native->clock_floor(native,DCN302_SMU_DPPCLK,native->dfs.dpp_floor_mhz,&acknowledged));
        CHECK(native->bandwidth_registers(native,&target,false,RX6600_HUBP_PREPARE));
        m.otg[3][DCN302_R_CONTROL]=0;m.surface[1][DCN302_SURFACE_R_HUBP]=2;
        CHECK(native->bandwidth_registers(native,NULL,false,RX6600_HUBP_BLANK));
        CHECK(native->bandwidth_registers(native,NULL,false,RX6600_HUBP_APPLY));
        CHECK(native->dpp_registers(native,RX6600_DPP_APPLY));cases++;
    }
    CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_CANCEL));
    CHECK(native->dpp_registers(native,RX6600_DPP_RESTORE) && !native->dpp_transaction.dirty && !memcmp(m.dpp,old_dpp,sizeof(old_dpp)));
    CHECK(native->bandwidth_registers(native,NULL,false,RX6600_HUBP_RESTORE) && !native->hubp_transaction.dirty && !native->hubp_transaction.applied && !native->hubp_transaction.poisoned && !native->hubbub_transaction.dirty && !native->hubbub_transaction.applied && !native->hubbub_transaction.poisoned && !m.hubbub[0]);cases++;
    /* Exercise the actual combined retained code at each apply write,
     * including a device-posted failure and a silently ignored write. */
    model clean=m;
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=bandwidth_writes;n++){
        if(kind==2)m.ignore_mmio_write=m.writes+n;
        else{m.fail_mmio_write=m.writes+n;m.posted=kind==1;}
        CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_APPLY));
        CHECK(native->ready && !native->busy && !native->hubp_transaction.dirty && !native->hubp_transaction.poisoned &&
            !native->hubbub_transaction.dirty && !native->hubbub_transaction.poisoned && !m.invalid && !m.hubbub[0]);
        CHECK(!memcmp(m.hubbub,clean.hubbub,sizeof(m.hubbub)) && !memcmp(m.hubp,clean.hubp,sizeof(m.hubp)) && !memcmp(m.surface,clean.surface,sizeof(m.surface)));
        m.ignore_mmio_write=m.fail_mmio_write=0;m.posted=false;cases++;
    }
    /* If a pipe wakes after the first fetch write, keep forced policy and
     * scanout off until an explicit later stopped-state restoration. */
    m.activate_mmio_write=m.writes+wm_writes+1;
    CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_APPLY) && !native->ready &&
        native->hubp_transaction.poisoned && native->hubp_transaction.dirty && native->hubbub_transaction.dirty && (m.hubbub[0]&0x33)==0x22);
    unsigned quarantined=m.writes;
    CHECK(!native->bandwidth_registers(native,NULL,false,RX6600_HUBP_RESTORE) && m.writes==quarantined);
    m.otg[4][DCN302_R_CONTROL]=0;m.activate_mmio_write=0;
    CHECK(native->bandwidth_registers(native,NULL,false,RX6600_HUBP_RESTORE) && !native->hubp_transaction.poisoned &&
        !native->hubp_transaction.dirty && !native->hubbub_transaction.poisoned && !native->hubbub_transaction.dirty &&
        !native->ready && !m.hubbub[0]);cases++;
    /* Internal rollback clears register poison; only a new parent probe can
     * restore readiness after the lost pipeline identity. */
    out.shutdown(out.state);CHECK(entry(&k,&out)==RX6600_ROUTE); /* stopped pipe */
    m.surface[1][DCN302_SURFACE_R_HUBP]=0;m.otg[3][DCN302_R_CONTROL]=control;
    CHECK(entry(&k,&out)==0);native=out.state;
    CHECK(native->bandwidth_registers(native,NULL,false,RX6600_HUBP_CANCEL) && !native->hubp_transaction.prepared);
    CHECK(!native->bandwidth_registers(native,NULL,false,(enum rx6600_hubp_operation)-1));
    m.surface[1][DCN302_SURFACE_R_HUBP]=0;m.otg[3][DCN302_R_CONTROL]=control;
    completed_writes=m.writes;
    m.time+=500000;out.poll(out.state);CHECK(out.read_mode(out.state,&mode) && out.set_mode(out.state,&mode.timing));
    mode.timing.pixel_khz=700000;CHECK(!out.set_mode(out.state,&mode.timing));out.shutdown(out.state);CHECK(!out.read_mode(out.state,&mode) && m.writes==completed_writes && !m.invalid);cases++;
    init(&m,&k,4,0,3,2,1,2,4,1,1);CHECK(entry(&k,&out)==0);native=out.state;
    m.registers.base+=4096;triggers=m.smu_triggers;
    CHECK(!native->clock_floor(native,DCN302_SMU_UCLK,558,&acknowledged) && !acknowledged && !native->ready && m.smu_triggers==triggers);out.shutdown(out.state);cases++;
    init(&m,&k,4,0,3,2,1,2,4,1,1);k.size=104;memset(&out,0xa5,sizeof(out));nexis_gpu_instance zero={0};
    CHECK(entry(&k,&out)==RX6600_INPUT && !memcmp(&out,&zero,sizeof(out)) && !m.reads && !m.writes);cases++;
}
static void timing_losses(nexis_gpu_entry_v2 entry){
    for(unsigned fault=0;fault<5;fault++){
        model m;nexis_gpu_services k;nexis_gpu_instance out;nexis_gpu_scanout mode;uint32_t acknowledged;
        init(&m,&k,4,0,3,2,1,2,4,1,1);CHECK(entry(&k,&out)==0);rx6600_state *s=out.state;
        CHECK(out.read_mode(s,&mode));
        const enum dcn302_smu_clock floors[]={DCN302_SMU_UCLK,DCN302_SMU_SOCCLK,DCN302_SMU_DCEFCLK,DCN302_SMU_PHYCLK};
        for(unsigned n=0;n<4;n++)CHECK(s->clock_floor(s,floors[n],600,&acknowledged));
        CHECK(s->clock_floor(s,DCN302_SMU_DISPCLK,s->dfs.disp_floor_mhz,&acknowledged));
        CHECK(s->clock_floor(s,DCN302_SMU_DPPCLK,s->dfs.dpp_floor_mhz,&acknowledged));
        CHECK(s->bandwidth_registers(s,&mode.timing,false,RX6600_HUBP_PREPARE));
        m.otg[3][DCN302_R_CONTROL]=0;m.surface[1][DCN302_SURFACE_R_HUBP]=2;
        CHECK(s->bandwidth_registers(s,NULL,false,RX6600_HUBP_BLANK));
        CHECK(s->bandwidth_registers(s,NULL,false,RX6600_HUBP_APPLY));
        CHECK(s->dpp_registers(s,RX6600_DPP_APPLY));
        model before=m;m.loss_kind=fault;m.loss_mmio_write=m.writes+1;
        CHECK(!s->timing_registers(s,RX6600_TIMING_APPLY) && !s->ready && s->timing_transaction.dirty && s->timing_transaction.poisoned &&
            m.writes==before.writes+1 && s->hubp_transaction.applied && s->hubbub_transaction.applied && (m.hubbub[0]&0x33)==0x22);
        CHECK(!s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE) && !s->bandwidth_registers(s,NULL,false,RX6600_HUBP_CANCEL));
        CHECK(!s->timing_registers(s,RX6600_TIMING_RESTORE) && m.writes==before.writes+1);
        switch(fault){case 0:m.otg[4][DCN302_R_CONTROL]=0;break;case 1:m.otg[3][DCN302_R_CLOCK]|=DCN302_CLOCK_ON_MASK;break;
        case 2:m.registers=before.registers;break;case 3:m.timer=before.timer;break;
        case 4:m.hubp[1][DCN302_HUBP_R_DCN_SURF0_TTU_CNTL0]^=1;break;}
        m.loss_mmio_write=0;
        CHECK(s->timing_registers(s,RX6600_TIMING_RESTORE) && !s->ready && !s->timing_transaction.dirty && !s->timing_transaction.poisoned &&
            !memcmp(m.otg,before.otg,sizeof(m.otg)) && !m.invalid);
        CHECK(s->dpp_registers(s,RX6600_DPP_RESTORE) && !s->dpp_transaction.applied && !s->dpp_transaction.poisoned && !s->ready);
        CHECK(s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE) && !s->hubp_transaction.applied && !s->hubbub_transaction.applied &&
            !s->ready && !m.hubbub[0]);out.shutdown(out.state);cases++;
    }
}
static rx6600_state *prepare_dpp(nexis_gpu_entry_v2 entry,model *m,nexis_gpu_services *k,nexis_gpu_instance *out){
    init(m,k,4,0,3,2,1,2,4,1,1);CHECK(entry(k,out)==0);rx6600_state *s=out->state;nexis_gpu_scanout mode;
    CHECK(out->read_mode(s,&mode));uint32_t acknowledged;
    const enum dcn302_smu_clock floors[]={DCN302_SMU_UCLK,DCN302_SMU_SOCCLK,DCN302_SMU_DCEFCLK,DCN302_SMU_PHYCLK};
    for(unsigned n=0;n<4;n++)CHECK(s->clock_floor(s,floors[n],600,&acknowledged));
    CHECK(s->clock_floor(s,DCN302_SMU_DISPCLK,s->dfs.disp_floor_mhz,&acknowledged));
    CHECK(s->clock_floor(s,DCN302_SMU_DPPCLK,s->dfs.dpp_floor_mhz,&acknowledged));
    CHECK(s->bandwidth_registers(s,&mode.timing,false,RX6600_HUBP_PREPARE));
    m->otg[3][DCN302_R_CONTROL]=0;m->surface[1][DCN302_SURFACE_R_HUBP]=2;
    CHECK(s->bandwidth_registers(s,NULL,false,RX6600_HUBP_BLANK));
    CHECK(s->bandwidth_registers(s,NULL,false,RX6600_HUBP_APPLY));return s;
}
static void dpp_faults(nexis_gpu_entry_v2 entry){
    model m;nexis_gpu_services k;nexis_gpu_instance out;
    rx6600_state *s=prepare_dpp(entry,&m,&k,&out);model old=m;unsigned start=m.writes;
    CHECK(s->dpp_registers(s,RX6600_DPP_APPLY));unsigned writes=m.writes-start;
    CHECK(writes==18 && s->dpp_transaction.dirty && s->dpp_transaction.applied && s->ready);
    CHECK(!s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE) && !s->bandwidth_registers(s,NULL,false,RX6600_HUBP_CANCEL));
    CHECK(!s->dpp_registers(s,RX6600_DPP_APPLY) && !s->dpp_registers(s,(enum rx6600_dpp_operation)-1));
    CHECK(s->dpp_registers(s,RX6600_DPP_RESTORE) && !memcmp(m.dpp,old.dpp,sizeof(m.dpp)));
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=writes;n++){
        if(kind==2)m.ignore_mmio_write=m.writes+n;else{m.fail_mmio_write=m.writes+n;m.posted=kind==1;}
        bool applied=s->dpp_registers(s,RX6600_DPP_APPLY);
        if(applied || !s->ready || s->dpp_transaction.dirty || s->dpp_transaction.poisoned || memcmp(m.dpp,old.dpp,sizeof(m.dpp)))
            fprintf(stderr,"DPP apply fault kind %u write %u: result %u ready %u dirty %u poison %u error %u writes %u compare %d\n",
                kind,n,applied,s->ready,s->dpp_transaction.dirty,s->dpp_transaction.poisoned,s->dpp_transaction.error,m.writes,memcmp(m.dpp,old.dpp,sizeof(m.dpp)));
        CHECK(!applied && s->ready && !s->dpp_transaction.dirty && !s->dpp_transaction.poisoned &&
            !s->dpp_transaction.applied && !memcmp(m.dpp,old.dpp,sizeof(m.dpp)) && !m.invalid &&
            s->hubp_transaction.applied && s->hubbub_transaction.applied && (m.hubbub[0]&0x33)==0x22);
        m.ignore_mmio_write=m.fail_mmio_write=0;m.posted=false;cases++;
    }
    CHECK(s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE));out.shutdown(s);
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=writes;n++){
        s=prepare_dpp(entry,&m,&k,&out);old=m;CHECK(s->dpp_registers(s,RX6600_DPP_APPLY));
        if(kind==2)m.ignore_mmio_write=m.writes+n;else{m.fail_mmio_write=m.writes+n;m.posted=kind==1;}
        CHECK(!s->dpp_registers(s,RX6600_DPP_RESTORE) && !s->ready && s->dpp_transaction.poisoned &&
            s->hubp_transaction.applied && s->hubbub_transaction.applied && (m.hubbub[0]&0x33)==0x22);
        unsigned quarantined=m.writes;
        CHECK(!s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE) && !s->bandwidth_registers(s,NULL,false,RX6600_HUBP_CANCEL) &&
            !s->display_clocks(s,NULL,RX6600_DFS_RESTORE) && m.writes==quarantined);
        m.ignore_mmio_write=m.fail_mmio_write=0;m.posted=false;
        CHECK(s->dpp_registers(s,RX6600_DPP_RESTORE) && !s->ready && !s->dpp_transaction.poisoned &&
            !s->dpp_transaction.dirty && !s->dpp_transaction.applied && !memcmp(m.dpp,old.dpp,sizeof(m.dpp)));
        CHECK(s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE) && !m.hubbub[0]);out.shutdown(s);cases++;
    }
    for(unsigned fault=0;fault<6;fault++)for(unsigned n=1;n<=writes;n++){
        s=prepare_dpp(entry,&m,&k,&out);old=m;m.loss_kind=fault;m.loss_mmio_write=m.writes+n;
        CHECK(!s->dpp_registers(s,RX6600_DPP_APPLY) && !s->ready && s->dpp_transaction.dirty && s->dpp_transaction.poisoned &&
            m.writes==old.writes+n && s->hubp_transaction.applied && s->hubbub_transaction.applied && (m.hubbub[0]&0x33)==0x22);
        unsigned quarantined=m.writes;
        CHECK(!s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE) && !s->timing_registers(s,RX6600_TIMING_APPLY) &&
            !s->dpp_registers(s,RX6600_DPP_RESTORE) && m.writes==quarantined);
        switch(fault){case 0:m.otg[4][DCN302_R_CONTROL]=0;break;case 1:m.surface[1][DCN302_SURFACE_R_CLOCK]=old.surface[1][DCN302_SURFACE_R_CLOCK];break;
        case 2:m.registers=old.registers;break;case 3:m.timer=old.timer;break;
        case 4:m.hubp[1][DCN302_HUBP_R_DCN_SURF0_TTU_CNTL0]^=1;break;case 5:m.dpp[1][DCN302_DPP_R_STATUS]=old.dpp[1][DCN302_DPP_R_STATUS];break;}
        m.loss_mmio_write=0;
        CHECK(s->dpp_registers(s,RX6600_DPP_RESTORE) && !s->ready && !s->dpp_transaction.poisoned && !s->dpp_transaction.dirty &&
            !memcmp(m.dpp,old.dpp,sizeof(m.dpp)) && !m.invalid);
        CHECK(s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE) && !m.hubbub[0]);out.shutdown(s);cases++;
    }
}
static void firmware_fixture(model *m,unsigned kind){
    uint8_t *b=m->rom;p16(b+1750,800);p16(b+800,166);
    const unsigned commands[]={12,4,76},offsets[]={1000,1080,1160},revisions[]={7,5,kind==1?7:6},parameters[]={16,12,kind==1?60:32};
    for(unsigned n=0;n<3;n++){
        unsigned at=offsets[n];p16(b+804+commands[n]*2,at);b[at+2]=1;b[at+3]=(uint8_t)revisions[n];b[at+4]=4;b[at+5]=(uint8_t)parameters[n];
        unsigned p=at+6;
        if(kind==2 && n==0){ /* Native IIO write to DWORD0, without REG0's value shift. */
            b[p++]=55;p16(b+p,1);p+=2;
        }
        if(kind==3 && n==0){b[p++]=1;b[p++]=5;p16(b+p,0);p+=2;p32(b+p,0x1234);p+=4;}
        else if((kind==4 || kind==5) && n==0){ /* Legacy operands must fail, never return fake0. */
            b[p++]=kind==4?5:6;b[p++]=1;b[p++]=9;b[p++]=0;
        }else if(kind==6 && n==0){ /* Firmware must not enable scanout on its own. */
            b[p++]=1;b[p++]=5;p16(b+p,dcn302_register_bytes[3][DCN302_R_CONTROL]/4);p+=2;p32(b+p,DCN302_MASTER_ENABLE_MASK);p+=4;
        }else if(kind==7 && n==0){
            b[p++]=1;b[p++]=1;p16(b+p,0x2000);p+=2;b[p++]=0;b[p++]=56;
        } /* Unsupported op after a structurally valid write is preflighted first. */
        else if(kind==8 && n==0){b[p++]=81;b[p++]=10;} /* Native guarded delay. */
        else if(kind==9 && n==0){ /* Read source, then write its result. */
            b[p++]=2;b[p++]=0;b[p++]=0;p16(b+p,0x2000);p+=2;
            b[p++]=1;b[p++]=1;p16(b+p,0x2001);p+=2;b[p++]=0;
        }else if(kind==10 && n==0){ /* Real IIO read/modify/write program. */
            b[p++]=55;p16(b+p,1);p+=2;b[p++]=13;b[p++]=5;p16(b+p,9);p+=2;p32(b+p,2);p+=4;
        }else if(kind==11 && n==0){ /* Runtime unsupported operand after a posted write. */
            b[p++]=1;b[p++]=1;p16(b+p,0x2000);p+=2;b[p++]=0;
            b[p++]=5;b[p++]=1;b[p++]=9;b[p++]=0;
        }else if(kind==12 && n==0){
            for(unsigned r=0;r<2;r++){b[p++]=1;b[p++]=1;p16(b+p,0x2000+r);p+=2;b[p++]=0;}
        }
        else{b[p++]=1;b[p++]=1;p16(b+p,kind==2 && n==0?9:n?0x2001:0x2000);p+=2;b[p++]=0;}
        b[p++]=91;p16(b+at,p-at);
    }
    if(kind==2 || kind==10){
        p16(b+150,1400);unsigned p=1404;
        b[p++]=1;b[p++]=1;b[p++]=2;p16(b+p,0x2000);p+=2;b[p++]=9;p+=2;
        b[p++]=1;b[p++]=129;b[p++]=8;b[p++]=32;b[p++]=0;b[p++]=0;b[p++]=3;p16(b+p,0);p+=2;b[p++]=9;p+=2;
        p16(b+1400,p-1400);
    }
    b[2047]=0;unsigned sum=0;for(unsigned n=0;n<2047;n++)sum+=b[n];b[2047]=(uint8_t)(0-sum);
}
static rx6600_state *prepare_firmware(nexis_gpu_entry_v2 entry,model *m,nexis_gpu_services *k,nexis_gpu_instance *out,unsigned kind){
    init(m,k,4,0,3,2,1,2,4,1,1);firmware_fixture(m,kind);CHECK(entry(k,out)==0);rx6600_state *s=out->state;nexis_gpu_scanout mode;
    CHECK(out->read_mode(s,&mode));uint32_t acknowledged;
    const enum dcn302_smu_clock floors[]={DCN302_SMU_UCLK,DCN302_SMU_SOCCLK,DCN302_SMU_DCEFCLK,DCN302_SMU_PHYCLK};
    for(unsigned n=0;n<4;n++)CHECK(s->clock_floor(s,floors[n],600,&acknowledged));
    CHECK(s->clock_floor(s,DCN302_SMU_DISPCLK,s->dfs.disp_floor_mhz,&acknowledged));
    CHECK(s->clock_floor(s,DCN302_SMU_DPPCLK,s->dfs.dpp_floor_mhz,&acknowledged));
    CHECK(s->bandwidth_registers(s,&mode.timing,false,RX6600_HUBP_PREPARE));
    m->otg[3][DCN302_R_CONTROL]=0;m->surface[1][DCN302_SURFACE_R_HUBP]=2;
    CHECK(s->bandwidth_registers(s,NULL,false,RX6600_HUBP_BLANK));CHECK(s->bandwidth_registers(s,NULL,false,RX6600_HUBP_APPLY));
    CHECK(s->dpp_registers(s,RX6600_DPP_APPLY));return s;
}
static void firmware_cases(nexis_gpu_entry_v2 entry,uintptr_t command_address,uintptr_t read_address,uintptr_t write_address,uintptr_t delay_address){
    model m;nexis_gpu_services k;nexis_gpu_instance out;rx6600_state *s;
    for(unsigned kind=0;kind<4;kind++){
        s=prepare_firmware(entry,&m,&k,&out,kind);unsigned start=m.writes;uint32_t khz=s->timing_transaction.timing.pixel_khz;
        if(command_address)CHECK((uintptr_t)s->firmware_command==command_address);
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,khz,0)==ATOM_VM_OK && s->firmware_vm.ready &&
            s->firmware_changed && !s->firmware_poisoned && s->ready && m.writes==start+1 && m.firmware_writes==1 && !m.invalid);
        CHECK(m.firmware_regs[kind>=2?0:2]==(kind==3?0x48d0:khz*10u));
        CHECK(s->firmware_parameter_bytes==16 && s->firmware_parameters[4]==24 && s->firmware_parameters[5]==0x21 &&
            s->firmware_parameters[6]==3 && s->firmware_parameters[7]==1 && s->firmware_parameters[8]==3);
        CHECK(s->firmware_vm.io.context==s && s->firmware_vm.io.time_us && s->firmware_vm.io.delay_us);
        if(read_address)CHECK((uintptr_t)s->firmware_vm.io.read==read_address);
        if(write_address)CHECK((uintptr_t)s->firmware_vm.io.write==write_address);
        if(delay_address)CHECK((uintptr_t)s->firmware_vm.io.delay_us==delay_address);
        CHECK(s->firmware_command(s,ATOM_DISPLAY_ENCODER,khz,0)==ATOM_VM_OK && s->firmware_parameter_bytes==12 && m.firmware_regs[3]==0x04030f00);
        CHECK(s->firmware_command(s,ATOM_DISPLAY_TRANSMITTER,khz,10)==ATOM_VM_OK && s->firmware_parameter_bytes==(kind==1?60:32) &&
            m.firmware_regs[3]==0x04030a04 && s->firmware_parameters[8]==2 && s->firmware_parameters[9]==1 && s->firmware_parameters[10]==12);
        unsigned done=m.writes;uint32_t acknowledged;
        CHECK(!s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE) && !s->bandwidth_registers(s,NULL,false,RX6600_HUBP_CANCEL) &&
            !s->dpp_registers(s,RX6600_DPP_RESTORE) && !s->timing_registers(s,RX6600_TIMING_RESTORE) &&
            !s->display_clocks(s,NULL,RX6600_DFS_RESTORE) && !s->clock_floor(s,DCN302_SMU_PHYCLK,600,&acknowledged) && m.writes==done);
        out.shutdown(s);cases++;
    }
    for(unsigned kind=4;kind<8;kind++){
        s=prepare_firmware(entry,&m,&k,&out,kind);unsigned start=m.writes;
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->timing_transaction.timing.pixel_khz,0)==ATOM_VM_UNSUPPORTED &&
            !s->firmware_changed && !s->firmware_poisoned && s->ready && m.writes==start && !m.firmware_writes && !m.invalid);
        CHECK(s->dpp_registers(s,RX6600_DPP_RESTORE));CHECK(s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE));out.shutdown(s);cases++;
    }
    s=prepare_firmware(entry,&m,&k,&out,8);uint64_t time=m.time;unsigned start=m.writes;
    CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->timing_transaction.timing.pixel_khz,0)==ATOM_VM_OK &&
        !s->firmware_changed && !s->firmware_poisoned && m.time==time+10 && m.writes==start);out.shutdown(s);cases++;
    for(unsigned posted=0;posted<2;posted++){
        s=prepare_firmware(entry,&m,&k,&out,0);start=m.writes;m.fail_mmio_write=start+1;m.posted=posted;
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->timing_transaction.timing.pixel_khz,0)==ATOM_VM_IO &&
            s->firmware_changed && s->firmware_poisoned && !s->ready && m.writes==start+1 && !m.invalid);
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->timing_transaction.timing.pixel_khz,0)!=ATOM_VM_OK && m.writes==start+1);
        CHECK(!s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE) && !s->dpp_registers(s,RX6600_DPP_RESTORE));out.shutdown(s);cases++;
    }
    for(unsigned loss=0;loss<7;loss++){
        s=prepare_firmware(entry,&m,&k,&out,0);start=m.writes;m.loss_kind=loss;m.loss_mmio_write=start+1;
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->timing_transaction.timing.pixel_khz,0)==ATOM_VM_IO &&
            s->firmware_changed && s->firmware_poisoned && !s->ready && m.writes==start+1 && !m.invalid);
        CHECK(!s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE) && !s->dpp_registers(s,RX6600_DPP_RESTORE) &&
            !s->timing_registers(s,RX6600_TIMING_RESTORE) && m.writes==start+1);out.shutdown(s);cases++;
    }
}
static void firmware_stopped_failure(model *m,rx6600_state *s,unsigned writes){
    CHECK(!s->busy && !m->invalid && m->writes==writes);
    if(s->firmware_changed){
        CHECK(s->firmware_poisoned && !s->ready);
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)!=ATOM_VM_OK);
        CHECK(!s->dpp_registers(s,RX6600_DPP_RESTORE) && !s->timing_registers(s,RX6600_TIMING_RESTORE) &&
            !s->bandwidth_registers(s,NULL,false,RX6600_HUBP_RESTORE) && !s->display_clocks(s,NULL,RX6600_DFS_RESTORE));
        CHECK(m->writes==writes);
    }else CHECK(!s->firmware_poisoned && s->ready);
}
static __attribute__((noinline)) bool invoke_host_firmware_read(rx6600_state *s,enum atom_vm_space space,uint32_t index,uint32_t *out){
    return s->firmware_vm.io.read(s,space,index,out);
}
static __attribute__((noinline)) bool invoke_pic_firmware_read(rx6600_state *s,enum atom_vm_space space,uint32_t index,uint32_t *out){
    bool (NEXIS_GPU_CALL *callback)(void *,enum atom_vm_space,uint32_t,uint32_t *)=(void *)(uintptr_t)s->firmware_vm.io.read;
    return callback(s,space,index,out);
}
static bool invoke_firmware_read(nexis_gpu_entry_v2 entry,rx6600_state *s,enum atom_vm_space space,uint32_t index,uint32_t *out){
    /* VM-private callbacks use their platform's ordinary C ABI. The Linux
     * PIC module uses SysV; the direct Windows host build uses Microsoft C.
     * The public retained command/service ABI is explicitly SysV for both. */
    /* Keep the unlike-ABI indirect calls in separate non-inlined functions:
     * GCC may otherwise merge both pointer calls and lose the ABI choice. */
    if(entry==driver_init_v2)return invoke_host_firmware_read(s,space,index,out);
    return invoke_pic_firmware_read(s,space,index,out);
}
static __attribute__((noinline)) bool invoke_host_firmware_write(rx6600_state *s,enum atom_vm_space space,uint32_t index,uint32_t value){
    return s->firmware_vm.io.write(s,space,index,value);
}
static __attribute__((noinline)) bool invoke_pic_firmware_write(rx6600_state *s,enum atom_vm_space space,uint32_t index,uint32_t value){
    bool (NEXIS_GPU_CALL *callback)(void *,enum atom_vm_space,uint32_t,uint32_t)=(void *)(uintptr_t)s->firmware_vm.io.write;
    return callback(s,space,index,value);
}
static bool invoke_firmware_write(nexis_gpu_entry_v2 entry,rx6600_state *s,enum atom_vm_space space,uint32_t index,uint32_t value){
    if(entry==driver_init_v2)return invoke_host_firmware_write(s,space,index,value);
    return invoke_pic_firmware_write(s,space,index,value);
}
static void firmware_robustness(nexis_gpu_entry_v2 entry){
    model m;nexis_gpu_services k;nexis_gpu_instance out;rx6600_state *s;
    for(unsigned kind=9;kind<=10;kind++){
        s=prepare_firmware(entry,&m,&k,&out,kind);m.firmware_regs[2]=3;unsigned start=m.writes;
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)==ATOM_VM_OK &&
            s->firmware_changed && !s->firmware_poisoned && m.firmware_reads==1 && m.writes==start+1 &&
            m.firmware_regs[kind==9?3:0]==3 && !m.invalid);out.shutdown(s);cases++;
    }
    /* Reset an isolated model and its exact prepared native state between
     * faults. This avoids recalculating the identical DML plan thousands of
     * times; each injected command still executes all real parent guards. */
    s=prepare_firmware(entry,&m,&k,&out,9);m.firmware_regs[2]=3;model original=m;
    rx6600_state *saved=malloc(sizeof(*saved));CHECK(saved);memcpy(saved,s,sizeof(*saved));
    CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)==ATOM_VM_OK);
    unsigned reads=m.reads-original.reads,queries=m.queries-original.queries;
    CHECK(reads>100 && queries>4 && m.firmware_reads==1);
    for(unsigned n=1;n<=reads;n++){
        m=original;memcpy(s,saved,sizeof(*s));m.fail_read=m.reads+n;
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)==ATOM_VM_IO);
        CHECK(m.reads==m.fail_read && m.firmware_writes<=1);
        firmware_stopped_failure(&m,s,original.writes+m.firmware_writes);cases++;
    }
    for(unsigned n=1;n<=queries;n++){
        m=original;memcpy(s,saved,sizeof(*s));m.fail_query=m.queries+n;
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)==ATOM_VM_IO);
        CHECK(m.queries==m.fail_query && m.firmware_writes<=1);
        firmware_stopped_failure(&m,s,original.writes+m.firmware_writes);cases++;
    }
    m=original;memcpy(s,saved,sizeof(*s));out.shutdown(s);free(saved);
    for(unsigned kind=0;kind<7;kind++){
        s=prepare_firmware(entry,&m,&k,&out,9);unsigned start=m.writes;m.loss_kind=kind;m.firmware_read_loss=1;
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)==ATOM_VM_IO && m.firmware_reads==1);
        firmware_stopped_failure(&m,s,start);out.shutdown(s);cases++;
    }
    for(unsigned kind=1;kind<=6;kind++){
        s=prepare_firmware(entry,&m,&k,&out,kind>=5?8:kind<=2?9:0);unsigned start=m.writes;m.time_fault=kind;
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)==(kind&1?ATOM_VM_IO:ATOM_VM_LIMIT));
        CHECK(m.firmware_writes==(kind==3 || kind==4?1:0));
        firmware_stopped_failure(&m,s,start+m.firmware_writes);out.shutdown(s);cases++;
    }
    s=prepare_firmware(entry,&m,&k,&out,8);unsigned start=m.writes;m.fail_delay=m.delays+1;
    CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)==ATOM_VM_IO);
    CHECK(m.delays==m.fail_delay);firmware_stopped_failure(&m,s,start);out.shutdown(s);cases++;
    s=prepare_firmware(entry,&m,&k,&out,11);start=m.writes;
    CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)==ATOM_VM_UNSUPPORTED && m.firmware_writes==1);
    firmware_stopped_failure(&m,s,start+1);out.shutdown(s);cases++;
    for(unsigned posted=0;posted<2;posted++)for(unsigned n=1;n<=2;n++){
        s=prepare_firmware(entry,&m,&k,&out,12);start=m.writes;m.fail_mmio_write=start+n;m.posted=posted;
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)==ATOM_VM_IO && m.firmware_writes==n);
        firmware_stopped_failure(&m,s,start+n);out.shutdown(s);cases++;
    }
    for(unsigned loss=0;loss<7;loss++)for(unsigned n=1;n<=2;n++){
        s=prepare_firmware(entry,&m,&k,&out,12);start=m.writes;m.loss_kind=loss;m.loss_mmio_write=start+n;
        CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)==ATOM_VM_IO && m.firmware_writes==n);
        firmware_stopped_failure(&m,s,start+n);out.shutdown(s);cases++;
    }
    const enum atom_display_command commands[]={ATOM_DISPLAY_PIXEL_CLOCK,ATOM_DISPLAY_ENCODER,ATOM_DISPLAY_TRANSMITTER};
    const unsigned offsets[]={1000,1080,1160};
    for(unsigned cmd=0;cmd<3;cmd++)for(unsigned field=0;field<3;field++){
        s=prepare_firmware(entry,&m,&k,&out,0);start=m.writes;
        m.rom[offsets[cmd]+(field==0?2:field==1?3:5)]=(uint8_t)(field==0?2:field==1?0:63);
        m.rom[2047]=0;unsigned sum=0;for(unsigned n=0;n<2047;n++)sum+=m.rom[n];m.rom[2047]=(uint8_t)(0-sum);
        CHECK(s->firmware_command(s,commands[cmd],s->clock.pixel_khz,cmd==2?10:0)!=ATOM_VM_OK);
        firmware_stopped_failure(&m,s,start);out.shutdown(s);cases++;
    }
    for(unsigned fault=0;fault<14;fault++){
        s=prepare_firmware(entry,&m,&k,&out,0);start=m.writes;uint32_t khz=s->clock.pixel_khz;
        enum atom_display_command command=ATOM_DISPLAY_PIXEL_CLOCK;unsigned action=0;
        switch(fault){
            case 0:s->busy=true;break;case 1:s->ready=false;break;case 2:s->dpp_transaction.applied=false;break;
            case 3:s->route.path=s->board.count;break;case 4:khz++;break;case 5:command=(enum atom_display_command)5;break;
            case 6:action=1;break;case 7:command=ATOM_DISPLAY_TRANSMITTER;action=2;break;
            case 8:m.otg[4][DCN302_R_CONTROL]=DCN302_MASTER_ACTIVE_MASK;break;
            case 9:m.otg[4][DCN302_R_VTG]=DCN302_VTG_ENABLE_MASK;break;case 10:m.hpd[1]=0;break;
            case 11:s->smu.floor_known[DCN302_SMU_DISPCLK]=false;break;
            case 12:s->smu.floor_known[DCN302_SMU_DPPCLK]=false;break;case 13:m.registers.bytes-=4;break;
        }
        CHECK(s->firmware_command(s,command,khz,action)!=ATOM_VM_OK && !s->firmware_changed && !s->firmware_poisoned && m.writes==start && !m.invalid);
        s->busy=false;out.shutdown(s);cases++;
    }
    s=prepare_firmware(entry,&m,&k,&out,8);
    CHECK(s->firmware_command(s,ATOM_DISPLAY_PIXEL_CLOCK,s->clock.pixel_khz,0)==ATOM_VM_OK);start=m.writes;
    for(unsigned space=ATOM_VM_PLL;space<=3;space++){
        uint32_t value=123;s->firmware_error=ATOM_VM_OK;
        CHECK(!invoke_firmware_read(entry,s,(enum atom_vm_space)space,0,&value) && !value && s->firmware_error==ATOM_VM_UNSUPPORTED);
        CHECK(!invoke_firmware_write(entry,s,(enum atom_vm_space)space,0,1) && m.writes==start && !m.invalid);cases++;
    }
    const uint32_t bounds[]={0x40000,UINT32_MAX};
    for(unsigned n=0;n<2;n++){
        uint32_t value=123;s->firmware_error=ATOM_VM_OK;unsigned before=m.reads;
        CHECK(!invoke_firmware_read(entry,s,ATOM_VM_MMIO,bounds[n],&value) && !value && s->firmware_error==ATOM_VM_BOUNDS && m.reads==before);
        CHECK(!invoke_firmware_write(entry,s,ATOM_VM_MMIO,bounds[n],1) && m.writes==start && !m.invalid);cases++;
    }
    for(unsigned pipe=0;pipe<5;pipe++)for(unsigned kind=0;kind<2;kind++){
        s->firmware_error=ATOM_VM_OK;
        CHECK(!invoke_firmware_write(entry,s,ATOM_VM_MMIO,dcn302_register_bytes[pipe][kind?DCN302_R_VTG:DCN302_R_CONTROL]/4,
            kind?DCN302_VTG_ENABLE_MASK:DCN302_MASTER_ENABLE_MASK) && s->firmware_error==ATOM_VM_UNSUPPORTED && m.writes==start && !m.invalid);cases++;
    }
    CHECK(!s->firmware_changed && !s->firmware_poisoned && s->ready);out.shutdown(s);
}
static void pic(const char *path,unsigned floor_offset,unsigned dfs_offset,unsigned bandwidth_offset,unsigned hubp_offset,unsigned timing_offset,unsigned dpp_offset,
    unsigned firmware_offset,unsigned read_offset,unsigned write_offset,unsigned delay_offset){
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length>64);rewind(f);
    uint8_t *data=malloc((size_t)length);CHECK(data && fread(data,1,(size_t)length,f)==(size_t)length);fclose(f);
    nexis_gpu_image m;CHECK(nexis_gpu_image_parse(data,(size_t)length,0x1002,0x73ff,&m));
    CHECK(floor_offset<m.text_bytes && dfs_offset<m.text_bytes && bandwidth_offset<m.text_bytes && hubp_offset<m.text_bytes && timing_offset<m.text_bytes && dpp_offset<m.text_bytes);
    CHECK(firmware_offset<m.text_bytes && read_offset<m.text_bytes && write_offset<m.text_bytes && delay_offset<m.text_bytes);
    uint8_t *bases[2];
    for(unsigned n=0;n<2;n++){
        bases[n]=VirtualAlloc(NULL,m.memory_bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);uint8_t *base=bases[n];CHECK(base && (!n || base!=bases[0]));memcpy(base,data+64,m.image_bytes);DWORD old;
        unsigned code=(m.text_bytes+4095)&~4095u;CHECK(VirtualProtect(base,code,PAGE_EXECUTE_READ,&old));
        if(code<m.writable_offset)CHECK(VirtualProtect(base+code,m.writable_offset-code,PAGE_READONLY,&old));
        retained((void *)(base+m.entry),(uintptr_t)(base+floor_offset),(uintptr_t)(base+dfs_offset),(uintptr_t)(base+bandwidth_offset),(uintptr_t)(base+hubp_offset),(uintptr_t)(base+timing_offset),(uintptr_t)(base+dpp_offset));
        timing_losses((void *)(base+m.entry));
        dpp_faults((void *)(base+m.entry));
        firmware_cases((void *)(base+m.entry),(uintptr_t)(base+firmware_offset),(uintptr_t)(base+read_offset),(uintptr_t)(base+write_offset),(uintptr_t)(base+delay_offset));
        firmware_robustness((void *)(base+m.entry));
    }
    for(unsigned n=0;n<2;n++)CHECK(VirtualFree(bases[n],0,MEM_RELEASE));
    free(data);
}
int main(int argc,char **argv){CHECK(argc==12);normal();failures();retained(driver_init_v2,0,0,0,0,0,0);timing_losses(driver_init_v2);dpp_faults(driver_init_v2);firmware_cases(driver_init_v2,0,0,0,0);firmware_robustness(driver_init_v2);pic(argv[1],(unsigned)strtoul(argv[2],NULL,10),(unsigned)strtoul(argv[3],NULL,10),(unsigned)strtoul(argv[4],NULL,10),(unsigned)strtoul(argv[5],NULL,10),(unsigned)strtoul(argv[6],NULL,10),(unsigned)strtoul(argv[7],NULL,10),(unsigned)strtoul(argv[8],NULL,10),(unsigned)strtoul(argv[9],NULL,10),(unsigned)strtoul(argv[10],NULL,10),(unsigned)strtoul(argv[11],NULL,10));
    printf("{\"passed\":true,\"cases\":%u,\"native_rx6600_retained_backend\":true,\"real_pic_callbacks_executed\":true,\"distinct_pic_bases_verified\":true,\"native_smu_probe_integrated\":true,\"real_pic_clock_floor_command_executed\":true,\"real_pic_display_clock_transaction_executed\":true,\"real_pic_bandwidth_plan_executed\":true,\"atom_memory_topology_integrated\":true,\"mode_changing_transaction_complete\":false,\"firmware_mailbox_writes\":true,\"clock_floor_changes_modeled\":true,\"display_clock_writes_modeled\":true,\"physical_hardware_verified\":false}\n",cases);return 0;}
