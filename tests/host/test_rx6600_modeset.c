/* Host test of the RX6600 mode-switch orchestrator (tools/gpu-driver/amd/rx6600_modeset.c).
 *
 * The register model is the one of test_rx6600.c, extended with what a complete switch touches:
 * OTG enable/disable with a running frame counter whose period follows the programmed timing and
 * pixel clock, the PHYPLL resync registers, HDMI/AFMT registers, a monitor with SCDC behaviour
 * (scrambler/lock status that depends on what the source really does) and a firmware (ATOM)
 * double that changes the modeled pixel clock and transmitter.  The ATOM bytecode path itself is
 * covered by test_rx6600.c; here the *sequencing and rollback* are under test:
 *   - a complete, correct switch (up, down, up again; scrambling on/off),
 *   - a switch to a mode the sink cannot do (no SCDC) is refused before anything is touched,
 *   - every single MMIO write of a switch failing (rejected, posted-error, silently ignored),
 *     every firmware command failing (before or after its effect), every monitor access failing,
 *     and a pixel clock that comes out wrong,
 *   with the invariant: afterwards either the driver is ready and the hardware model equals its
 *   state before the attempt (picture running), or the driver is not ready and every output is off.
 * Register responses are modeled, not measured: this does not prove anything about real hardware. */
#include "../../tools/gpu-driver/amd/rx6600.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
#define HSET(v,f,n) (((v)&~DCN302_HDMI_##f##_MASK)|(((uint32_t)(n)<<DCN302_HDMI_##f##_SHIFT)&DCN302_HDMI_##f##_MASK))
#define SSET(v,f,n) (((v)&~DCN302_SURFACE_##f##_MASK)|(((uint32_t)(n)<<DCN302_SURFACE_##f##_SHIFT)&DCN302_SURFACE_##f##_MASK))
static unsigned cases,restored,switched_off,harmless,video_only;
static void NEXIS_GPU_CALL tlog(void *context,const char *text){(void)context;if(getenv("RX6600_TRACE"))fprintf(stderr,"[module] %s\n",text);}
typedef struct {
    uint8_t rom[2048];
    uint32_t otg[5][DCN302_TIMING_REGISTER_COUNT],dig[5][DCN302_HDMI_REGISTER_COUNT],surface[5][DCN302_SURFACE_REGISTER_COUNT],hpd[5];
    uint32_t fb_base,fb_top,fb_offset;
    uint32_t dfs_pll,dfs_dentist,dfs_control,dfs_dto[5];
    uint32_t hubp[5][DCN302_HUBP_REGISTER_COUNT];
    uint32_t dpp[5][DCN302_DPP_REGISTER_COUNT];
    uint32_t hubbub[DCN302_HUBBUB_REGISTER_COUNT],reference,timer;
    uint32_t smu_argument,smu_response,smu_version,smu_interface,smu_header,smu_features,smu_status,smu_floor;
    uint64_t time;
    uint32_t resync[5],pixel_khz,pll_khz;
    uint32_t az_index[6],az[6][0x100],dto[3];
    uint8_t edid[256];
    uint64_t frame[5],frame_phase[5],frame_time[5];
    unsigned hubp_index;
    bool tx_enabled;
    uint8_t scdc[256];
    unsigned reads,writes,queries,fail_read,fail_query,smu_writes,smu_triggers,smu_clock_requests;
    unsigned fail_mmio_write,ignore_mmio_write;bool posted;
    unsigned fw_ops,fail_fw,fail_fw_kind,sink_ops,fail_sink,pll_error_ppm;
    bool invalid;
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
    p16(b+1600,84);b[1602]=4;b[1603]=4;p32(b+1608,60000);p16(b+1612,10000);p16(b+1614,5000);p16(b+1636,2700);
    b[1642]=5;b[1644]=6;b[1645]=6;b[1646]=6;
    unsigned sum=0;for(unsigned n=0;n<2047;n++)sum+=b[n];b[2047]=(uint8_t)(0-sum);
}
/* Frame counter: advances only while the OTG runs, period = htotal*vtotal / pixel clock. */
static uint32_t frame_count(model *m,unsigned i){
    uint64_t pixels=(uint64_t)((m->otg[i][DCN302_R_H_TOTAL]&DCN302_H_TOTAL_MASK)+1)*((m->otg[i][DCN302_R_V_TOTAL]&DCN302_V_TOTAL_MASK)+1);
    uint64_t period=m->pixel_khz?pixels*1000/m->pixel_khz:1;if(!period)period=1;
    if((m->otg[i][DCN302_R_CONTROL]&DCN302_MASTER_ENABLE_MASK) && (m->otg[i][DCN302_R_VTG]&DCN302_VTG_ENABLE_MASK)){
        m->frame_phase[i]+=m->time-m->frame_time[i];m->frame[i]+=m->frame_phase[i]/period;m->frame_phase[i]%=period;
    }
    m->frame_time[i]=m->time;return (uint32_t)m->frame[i]&DCN302_FRAME_COUNT_MASK;
}
static bool NEXIS_GPU_CALL rd(void *ctx,unsigned bar,uint32_t offset,uint32_t *out){
    model *m=ctx;CHECK(bar==5);if(++m->reads==m->fail_read)return false;
    if(offset==DCN302_SMU_RESPONSE_BYTES){*out=m->smu_response;return true;}
    if(offset==DCN302_SMU_ARGUMENT_BYTES){*out=m->smu_argument;return true;}
    if(offset==DCN302_HUBBUB_REF_BYTES){*out=m->reference;return true;}
    if(offset==DCN302_HUBBUB_TIMER_BYTES){*out=m->timer;return true;}
    for(unsigned r=0;r<DCN302_HUBBUB_REGISTER_COUNT;r++)if(offset==dcn302_hubbub_register_bytes[r]){*out=m->hubbub[r];return true;}
    if(offset==DCN302_DFS_PLL_BYTES){*out=m->dfs_pll;return true;}
    if(offset==DCN302_DFS_DENTIST_BYTES){*out=m->dfs_dentist;return true;}
    if(offset==DCN302_DFS_DTO_CTRL_BYTES){*out=m->dfs_control;return true;}
    for(unsigned i=0;i<5;i++)if(offset==dcn302_dfs_dto_bytes[i]){*out=m->dfs_dto[i];return true;}
    for(unsigned i=0;i<5;i++)if(offset==dcn302_pixel_resync_bytes[i]){*out=m->resync[i];return true;}
    for(unsigned e=0;e<DCN302_AZ_ENDPOINTS;e++){
        if(offset==dcn302_az_index_bytes[e]){*out=m->az_index[e];return true;}
        if(offset==dcn302_az_data_bytes[e]){*out=m->az[e][m->az_index[e]&0xff];return true;}
    }
    if(offset==DCN302_DTO_SOURCE_BYTES){*out=m->dto[0];return true;}
    if(offset==DCN302_DTO0_PHASE_BYTES){*out=m->dto[1];return true;}
    if(offset==DCN302_DTO0_MODULE_BYTES){*out=m->dto[2];return true;}
    for(unsigned i=0;i<5;i++){
        for(unsigned r=0;r<DCN302_TIMING_REGISTER_COUNT;r++)if(offset==dcn302_timing_register_bytes[i][r]){
            *out=r==DCN302_R_FRAME_COUNT?frame_count(m,i):m->otg[i][r];return true;
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
#define INJECT_BEFORE() do{if(m->writes==m->ignore_mmio_write)return true;if(m->writes==m->fail_mmio_write && !m->posted)return false;}while(0)
#define INJECT_AFTER() return m->writes!=m->fail_mmio_write
static bool all_stopped(model *m){
    for(unsigned i=0;i<5;i++)if((m->otg[i][DCN302_R_CONTROL]&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) || (m->otg[i][DCN302_R_VTG]&DCN302_VTG_ENABLE_MASK))return false;
    return true;
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
    /* OTG enable/VTG writes are the only ones allowed while a pipe runs. */
    for(unsigned i=0;i<5;i++){
        if(offset==dcn302_register_bytes[i][DCN302_R_CONTROL]){
            INJECT_BEFORE();
            frame_count(m,i);
            bool enable=value&DCN302_MASTER_ENABLE_MASK;
            m->otg[i][DCN302_R_CONTROL]=(value&~DCN302_MASTER_ACTIVE_MASK)|(enable?DCN302_MASTER_ACTIVE_MASK:0);
            m->frame_time[i]=m->time;
            if(i==3){ /* the pipe feeding HUBP 'hubp_index': requests drain when it stops */
                if(enable)m->surface[m->hubp_index][DCN302_SURFACE_R_HUBP]&=~2u;else m->surface[m->hubp_index][DCN302_SURFACE_R_HUBP]|=2u;
            }
            INJECT_AFTER();
        }
        if(offset==dcn302_register_bytes[i][DCN302_R_VTG]){INJECT_BEFORE();m->otg[i][DCN302_R_VTG]=value;INJECT_AFTER();}
    }
    /* Plane unblank and the HDMI packet registers are legitimately written while the OTG runs. */
    bool exempt=false;
    for(unsigned i=0;i<5;i++){
        if(offset==dcn302_hubp_register_bytes[i][DCN302_HUBP_R_DCHUBP_CNTL])exempt=true;
        for(unsigned r=0;r<DCN302_HDMI_REGISTER_COUNT;r++)if(offset==dcn302_hdmi_register_bytes[i][r])exempt=true;
    }
    for(unsigned e=0;e<DCN302_AZ_ENDPOINTS;e++)if(offset==dcn302_az_index_bytes[e] || offset==dcn302_az_data_bytes[e])exempt=true; /* audio endpoint is switched on while video runs */
    if(!exempt)CHECK(all_stopped(m));
    for(unsigned e=0;e<DCN302_AZ_ENDPOINTS;e++){
        if(offset==dcn302_az_index_bytes[e]){INJECT_BEFORE();CHECK(!(value&~DCN302_AZ_INDEX_MASK));m->az_index[e]=value;INJECT_AFTER();}
        if(offset==dcn302_az_data_bytes[e]){
            INJECT_BEFORE();
            unsigned ix=m->az_index[e]&0xff;
            CHECK(ix!=dcn302_az_ix[DCN302_AZ_R_PIN_SENSE]); /* read-only status is never written */
            m->az[e][ix]=value;
            INJECT_AFTER();
        }
    }
    if(offset==DCN302_DTO_SOURCE_BYTES){INJECT_BEFORE();m->dto[0]=value;INJECT_AFTER();}
    if(offset==DCN302_DTO0_PHASE_BYTES){INJECT_BEFORE();m->dto[1]=value;INJECT_AFTER();}
    if(offset==DCN302_DTO0_MODULE_BYTES){INJECT_BEFORE();m->dto[2]=value;INJECT_AFTER();}
    for(unsigned i=0;i<5;i++)if(offset==dcn302_pixel_resync_bytes[i]){INJECT_BEFORE();m->resync[i]=value;INJECT_AFTER();}
    for(unsigned i=0;i<5;i++)for(unsigned r=0;r<DCN302_HDMI_REGISTER_COUNT;r++)if(offset==dcn302_hdmi_register_bytes[i][r]){
        INJECT_BEFORE();
        /* UPDATE strobes self-clear in hardware. */
        if(r==DCN302_HDMI_R_AFMT_PACKET)value&=~DCN302_HDMI_CS_UPDATE_MASK;
        if(r==DCN302_HDMI_R_AFMT_INFO)value&=~DCN302_HDMI_INFO_UPDATE_MASK;
        m->dig[i][r]=value;INJECT_AFTER();
    }
    for(unsigned r=0;r<DCN302_HUBBUB_REGISTER_COUNT;r++)if(offset==dcn302_hubbub_register_bytes[r]){
        CHECK(!((value^m->hubbub[r])&~dcn302_hubbub_owned[r]));
        if(r)CHECK((m->hubbub[0]&0x33)==0x22);
        INJECT_BEFORE();
        m->hubbub[r]=value;
        INJECT_AFTER();
    }
    if(offset==DCN302_DFS_DENTIST_BYTES){
        INJECT_BEFORE();
        m->dfs_dentist=(value&~DCN302_DFS_DISP_READ_MASK)|((value&127)<<8)|DCN302_DFS_DISP_DONE_MASK|DCN302_DFS_DPP_DONE_MASK;INJECT_AFTER();
    }
    if(offset==DCN302_DFS_DTO_CTRL_BYTES){INJECT_BEFORE();m->dfs_control=value;INJECT_AFTER();}
    for(unsigned i=0;i<5;i++)if(offset==dcn302_dfs_dto_bytes[i]){INJECT_BEFORE();m->dfs_dto[i]=value;INJECT_AFTER();}
    for(unsigned i=0;i<5;i++)for(unsigned r=0;r<DCN302_DPP_REGISTER_COUNT;r++)if(offset==dcn302_dpp_register_bytes[i][r]){
        CHECK(r<DCN302_DPP_PROGRAM_COUNT && dcn302_dpp_owned[r] && !(value&dcn302_dpp_readonly[r]) && (m->hubbub[0]&0x33)==0x22);
        CHECK(!((value^m->dpp[i][r])&~(dcn302_dpp_owned[r]|dcn302_dpp_readonly[r])));
        INJECT_BEFORE();
        m->dpp[i][r]=value|(m->dpp[i][r]&dcn302_dpp_readonly[r]);
        INJECT_AFTER();
    }
    for(unsigned i=0;i<5;i++)for(unsigned r=0;r<DCN302_TIMING_REGISTER_COUNT;r++)if(offset==dcn302_timing_register_bytes[i][r]){
        CHECK(dcn302_timing_owned[r] && !(value&dcn302_timing_write_excluded[r]) && (m->hubbub[0]&0x33)==0x22);
        CHECK(!((value^m->otg[i][r])&~(dcn302_timing_owned[r]|dcn302_timing_write_excluded[r])));
        if(r!=DCN302_R_LOCK && r!=DCN302_R_GLOBAL2 && r!=DCN302_TIMING_R_GLOBAL0 && r!=DCN302_TIMING_R_GLOBAL1 && r!=DCN302_TIMING_R_DBUF){
            CHECK((m->otg[i][DCN302_R_LOCK]&0x101)==0x101 &&
                (m->otg[i][DCN302_R_GLOBAL2]&dcn302_timing_owned[DCN302_R_GLOBAL2])==(i<<DCN302_TIMING_LOCK_SELECT_SHIFT));
        }
        INJECT_BEFORE();
        frame_count(m,i);
        m->otg[i][r]=value|(m->otg[i][r]&dcn302_timing_write_excluded[r]);
        if(r==DCN302_R_LOCK){if(value&1)m->otg[i][r]|=DCN302_LOCK_STATUS_MASK;else m->otg[i][r]&=~DCN302_LOCK_STATUS_MASK;}
        INJECT_AFTER();
    }
    for(unsigned i=0;i<5;i++)for(unsigned r=0;r<DCN302_HUBP_REGISTER_COUNT;r++)if(offset==dcn302_hubp_register_bytes[i][r]){
        CHECK(!(value&(dcn302_hubp_forbidden[r]|dcn302_hubp_readonly[r])));
        uint32_t current=m->hubp[i][r];
        for(unsigned sr=0;sr<DCN302_SURFACE_REGISTER_COUNT;sr++)if(offset==dcn302_surface_register_bytes[i][sr])current=m->surface[i][sr];
        bool blank=r==DCN302_HUBP_R_DCHUBP_CNTL && ((current^value)&0x1001u);
        if(!blank)CHECK((m->hubbub[0]&0x33)==0x22);
        INJECT_BEFORE();
        uint32_t next=(current&dcn302_hubp_readonly[r])|value;
        /* NO_OUTSTANDING_REQ follows the real pipeline: set while stopped, cleared while running unless blanked. */
        if(r==DCN302_HUBP_R_DCHUBP_CNTL){
            if((value&0x1001u)==1u)next|=2;
            else if(!(value&1u) && (m->otg[3][DCN302_R_CONTROL]&DCN302_MASTER_ENABLE_MASK))next&=~2u;
        }
        m->hubp[i][r]=next;
        for(unsigned sr=0;sr<DCN302_SURFACE_REGISTER_COUNT;sr++)if(offset==dcn302_surface_register_bytes[i][sr])m->surface[i][sr]=next;
        INJECT_AFTER();
    }
    m->invalid=true;return false;
}
static bool NEXIS_GPU_CALL resource(void *ctx,unsigned bar,nexis_gpu_resource *out){
    model *m=ctx;memset(out,0,sizeof(*out));if(++m->queries==m->fail_query)return false;
    if(bar==0)*out=m->vram;else if(bar==5)*out=m->registers;else return false;return true;
}
static bool NEXIS_GPU_CALL delay(void *ctx,uint32_t us){model *m=ctx;CHECK(us<=250000);m->time+=us;return true;}
static uint64_t NEXIS_GPU_CALL now(void *ctx){return ((model *)ctx)->time;}
/* Base block + CTA-861 extension with 2-channel LPCM (32/44.1/48 kHz, 16/20/24 bit) and front left/right speakers. */
static void build_edid(uint8_t *e){
    memset(e,0,256);
    static const uint8_t header[8]={0,255,255,255,255,255,255,0};
    memcpy(e,header,8);e[8]=0x04;e[9]=0x72;e[10]=0x58;e[11]=0x10;e[18]=1;e[19]=4;
    e[54+3]=0xfc;memcpy(e+54+5,"VG270 W3\n",9); /* monitor name descriptor */
    e[126]=1;
    unsigned sum=0;for(unsigned i=0;i<127;i++)sum+=e[i];e[127]=(uint8_t)(0-sum);
    uint8_t *x=e+128;x[0]=2;x[1]=3;
    unsigned p=4;
    x[p++]=(1<<5)|3;x[p++]=(1<<3)|1;x[p++]=0x07;x[p++]=0x07;  /* audio: LPCM, 2 channels, rates, sizes */
    x[p++]=(4<<5)|3;x[p++]=0x01;x[p++]=0;x[p++]=0;           /* speaker allocation: FL/FR */
    x[2]=(uint8_t)p;
    sum=0;for(unsigned i=0;i<127;i++)sum+=x[i];x[127]=(uint8_t)(0-sum);
}
static void init(model *m,nexis_gpu_services *k,unsigned link,unsigned stream,unsigned pipe,unsigned bus,unsigned hpd,unsigned opp,unsigned mpcc,unsigned hubp,unsigned format){
    memset(m,0,sizeof(*m));memset(k,0,sizeof(*k));rom_init(m,link,bus,hpd);
    m->timer=0x1002;m->pixel_khz=m->pll_khz=558195;m->tx_enabled=true;m->hubp_index=hubp;
    m->smu_response=m->smu_status=1;m->smu_version=0x3a0100;m->smu_header=1;m->smu_interface=0x40;m->smu_features=3;
    m->dfs_pll=36|0x80000000u;m->dfs_dentist=24|(24u<<8)|(24u<<24)|DCN302_DFS_DISP_DONE_MASK|DCN302_DFS_DPP_DONE_MASK;
    *k=(nexis_gpu_services){.abi=2,.size=136,.vendor=0x1002,.device=0x73ff,.width=1920,.height=1080,.pitch=2048,.format=format,
        .framebuffer=0x800200000ULL,.framebuffer_bytes=2048u*4*1080,.rom=m->rom,.rom_bytes=2048,.service_context=m,
        .read32=rd,.write32=wr,.time_us=now,.delay_us=delay,.resource=resource,.log=tlog};
    build_edid(m->edid);k->edid=m->edid;k->edid_bytes=256;
    m->vram=(nexis_gpu_resource){0x800000000ULL,0x200000000ULL,7,0};m->registers=(nexis_gpu_resource){0xb0000000,0x100000,9,0};
    m->hpd[hpd]=DCN302_HPD_SENSE_MASK|DCN302_HPD_DELAYED_MASK;
    m->dig[link][DCN302_HDMI_R_BE]=HSET(HSET(HSET(0,LINK_MODE,3),FE_SOURCE,1u<<stream),HPD,hpd);
    m->dig[link][DCN302_HDMI_R_BE_ENABLE]=DCN302_HDMI_LINK_ENABLE_MASK|DCN302_HDMI_LINK_CLOCK_MASK;
    m->dig[stream][DCN302_HDMI_R_FE]=HSET(0,PIPE,pipe);
    m->dig[stream][DCN302_HDMI_R_AUDIO_CLOCK]=DCN302_HDMI_CLOCK_ENABLE_MASK|DCN302_HDMI_CLOCK_ON_MASK;
    m->dig[stream][DCN302_HDMI_R_CONTROL]=DCN302_HDMI_SCRAMBLE_MASK|DCN302_HDMI_CLOCK_RATIO_MASK; /* GOP lit 240 Hz with scrambling */
    uint32_t *o=m->otg[pipe];o[DCN302_R_CONTROL]=DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK|(3u<<DCN302_DISABLE_POINT_SHIFT);
    o[DCN302_R_CLOCK]=DCN302_CLOCK_ENABLE_MASK|DCN302_CLOCK_ON_MASK;o[DCN302_R_SOURCE]=opp<<DCN302_SEG0_SHIFT;o[DCN302_R_VTG]=DCN302_VTG_ENABLE_MASK;
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
    m->scdc[1]=1;m->scdc[0x20]=3;m->scdc[2]=1;
}
/* ---- the monitor (SCDC at 0x54) and the firmware double ---- */
static model *model_of(rx6600_state *s){return s->services->service_context;}
static bool sink_scrambling(rx6600_state *s,model *m){return (m->dig[s->route.stream][DCN302_HDMI_R_CONTROL]&DCN302_HDMI_SCRAMBLE_MASK)!=0;}
static bool sink_read_test(void *c,uint8_t address,uint8_t offset,uint8_t *value){
    rx6600_state *s=c;model *m=model_of(s);if(++m->sink_ops==m->fail_sink || address!=0x54)return false;
    switch(offset){
        case 0x21:*value=((m->scdc[0x20]&1) && m->tx_enabled && sink_scrambling(s,m))?1:0;break;
        case 0x40:{
            bool ratio=((m->scdc[0x20]&2)!=0)==(m->pixel_khz>340000),scr=((m->scdc[0x20]&1)!=0)==sink_scrambling(s,m);
            *value=(m->tx_enabled && ratio && scr)?15:0;break;
        }
        default:*value=m->scdc[offset];
    }
    return true;
}
static bool sink_write_test(void *c,uint8_t address,uint8_t offset,uint8_t value){
    rx6600_state *s=c;model *m=model_of(s);if(++m->sink_ops==m->fail_sink || address!=0x54)return false;
    m->scdc[offset]=value;return true;
}
static enum atom_vm_error NEXIS_GPU_CALL fw_double(void *context,enum atom_display_command command,uint32_t khz,unsigned action){
    rx6600_state *s=context;model *m=model_of(s);
    /* Same entry conditions as the real guarded command. */
    if(!s || !s->ready || s->busy || s->firmware_poisoned || !(s->fw_baseline?true:s->dpp_transaction.applied) ||
       (khz!=s->clock.pixel_khz && (s->fw_baseline || khz!=s->timing_transaction.timing.pixel_khz)))return ATOM_VM_INPUT;
    if(!(s->fw_baseline?rx6600_baseline_clean(s):rx6600_new_mode_guard(s)))return ATOM_VM_IO;
    CHECK(all_stopped(m)); /* PLL/PHY work only with every pipe stopped */
    if(command!=ATOM_DISPLAY_PIXEL_CLOCK && command!=ATOM_DISPLAY_TRANSMITTER)return ATOM_VM_UNSUPPORTED;
    bool fail=++m->fw_ops==m->fail_fw;
    if(fail && m->fail_fw_kind==0)return ATOM_VM_IO; /* rejected before any register changed */
    if(command==ATOM_DISPLAY_PIXEL_CLOCK){
        m->pll_khz=khz;m->pixel_khz=(uint32_t)((uint64_t)khz*(1000000u+m->pll_error_ppm)/1000000u);
    }else if(action==1){
        m->tx_enabled=true;m->dig[s->route.link][DCN302_HDMI_R_BE_ENABLE]|=DCN302_HDMI_LINK_ENABLE_MASK|DCN302_HDMI_LINK_CLOCK_MASK;
    }else{
        m->tx_enabled=false;m->dig[s->route.link][DCN302_HDMI_R_BE_ENABLE]&=~DCN302_HDMI_LINK_ENABLE_MASK;
    }
    s->firmware_changed=true;
    if(fail){s->firmware_poisoned=true;s->ready=false;s->error=RX6600_CLOCK;return ATOM_VM_IO;} /* effect applied, then an error */
    return ATOM_VM_OK;
}
static void doubles(rx6600_state *s){s->firmware_command=fw_double;s->sink_read=sink_read_test;s->sink_write=sink_write_test;}
/* ---- hardware snapshot (everything a rollback has to restore) ---- */
typedef struct {
    uint32_t otg[5][DCN302_TIMING_REGISTER_COUNT],dig[5][DCN302_HDMI_REGISTER_COUNT],surface[5][DCN302_SURFACE_REGISTER_COUNT],hubp[5][DCN302_HUBP_REGISTER_COUNT];
    uint32_t dpp[5][DCN302_DPP_REGISTER_COUNT],hubbub[DCN302_HUBBUB_REGISTER_COUNT],dfs_pll,dfs_dentist,dfs_control,dfs_dto[5],resync[5],pixel_khz,pll_khz;
    bool tx_enabled;uint8_t scdc20,scdc02;
    uint32_t az[6][0x100],dto[3];
} snap;
static void take(const model *m,snap *s){
    memcpy(s->otg,m->otg,sizeof(s->otg));memcpy(s->dig,m->dig,sizeof(s->dig));memcpy(s->surface,m->surface,sizeof(s->surface));memcpy(s->hubp,m->hubp,sizeof(s->hubp));
    memcpy(s->dpp,m->dpp,sizeof(s->dpp));memcpy(s->hubbub,m->hubbub,sizeof(s->hubbub));s->dfs_pll=m->dfs_pll;s->dfs_dentist=m->dfs_dentist;s->dfs_control=m->dfs_control;
    memcpy(s->dfs_dto,m->dfs_dto,sizeof(s->dfs_dto));memcpy(s->resync,m->resync,sizeof(s->resync));s->pixel_khz=m->pixel_khz;s->pll_khz=m->pll_khz;
    s->tx_enabled=m->tx_enabled;s->scdc20=m->scdc[0x20];s->scdc02=m->scdc[2];
    memcpy(s->az,m->az,sizeof(s->az));memcpy(s->dto,m->dto,sizeof(s->dto));
}
static bool ppm_close(uint32_t a,uint32_t b){uint32_t d=a>b?a-b:b-a;return (uint64_t)d*1000000<=(uint64_t)b*1000;}
static bool same(const model *m,const snap *s){
    /* The frame counter bookkeeping is not hardware configuration. */
    return !memcmp(s->otg,m->otg,sizeof(s->otg)) && !memcmp(s->surface,m->surface,sizeof(s->surface)) && !memcmp(s->hubp,m->hubp,sizeof(s->hubp)) &&
        !memcmp(s->dpp,m->dpp,sizeof(s->dpp)) && !memcmp(s->hubbub,m->hubbub,sizeof(s->hubbub)) && !memcmp(s->dig,m->dig,sizeof(s->dig)) &&
        s->dfs_pll==m->dfs_pll && s->dfs_dentist==m->dfs_dentist && s->dfs_control==m->dfs_control && !memcmp(s->dfs_dto,m->dfs_dto,sizeof(s->dfs_dto)) &&
        /* the old pixel clock is re-programmed from its measured value (<=1000 ppm), not from unknown PLL words */
        !memcmp(s->resync,m->resync,sizeof(s->resync)) && ppm_close(m->pixel_khz,s->pixel_khz) && ppm_close(m->pll_khz,s->pll_khz) && s->tx_enabled==m->tx_enabled &&
        s->scdc20==m->scdc[0x20] && s->scdc02==m->scdc[2] && !memcmp(s->az,m->az,sizeof(s->az)) && !memcmp(s->dto,m->dto,sizeof(s->dto));
}
static void explain(const model *m,const snap *s){
    if(!getenv("RX6600_TRACE"))return;
    for(unsigned i=0;i<5;i++){
        for(unsigned r=0;r<DCN302_TIMING_REGISTER_COUNT;r++)if(s->otg[i][r]!=m->otg[i][r])fprintf(stderr,"  otg[%u][%u] %08x -> %08x\n",i,r,s->otg[i][r],m->otg[i][r]);
        for(unsigned r=0;r<DCN302_HDMI_REGISTER_COUNT;r++)if(s->dig[i][r]!=m->dig[i][r])fprintf(stderr,"  dig[%u][%u] %08x -> %08x\n",i,r,s->dig[i][r],m->dig[i][r]);
        for(unsigned r=0;r<DCN302_SURFACE_REGISTER_COUNT;r++)if(s->surface[i][r]!=m->surface[i][r])fprintf(stderr,"  surface[%u][%u] %08x -> %08x\n",i,r,s->surface[i][r],m->surface[i][r]);
        for(unsigned r=0;r<DCN302_HUBP_REGISTER_COUNT;r++)if(s->hubp[i][r]!=m->hubp[i][r])fprintf(stderr,"  hubp[%u][%u] %08x -> %08x\n",i,r,s->hubp[i][r],m->hubp[i][r]);
        for(unsigned r=0;r<DCN302_DPP_REGISTER_COUNT;r++)if(s->dpp[i][r]!=m->dpp[i][r])fprintf(stderr,"  dpp[%u][%u] %08x -> %08x\n",i,r,s->dpp[i][r],m->dpp[i][r]);
        if(s->resync[i]!=m->resync[i])fprintf(stderr,"  resync[%u] %08x -> %08x\n",i,s->resync[i],m->resync[i]);
        if(s->dfs_dto[i]!=m->dfs_dto[i])fprintf(stderr,"  dfs_dto[%u]\n",i);
    }
    for(unsigned r=0;r<DCN302_HUBBUB_REGISTER_COUNT;r++)if(s->hubbub[r]!=m->hubbub[r])fprintf(stderr,"  hubbub[%u] %08x -> %08x\n",r,s->hubbub[r],m->hubbub[r]);
    if(s->dfs_pll!=m->dfs_pll||s->dfs_dentist!=m->dfs_dentist||s->dfs_control!=m->dfs_control)fprintf(stderr,"  dfs differs\n");
    if(memcmp(s->az,m->az,sizeof(s->az)))fprintf(stderr,"  audio endpoint registers differ\n");
    if(memcmp(s->dto,m->dto,sizeof(s->dto)))fprintf(stderr,"  audio DTO differs\n");
    if(s->pixel_khz!=m->pixel_khz||s->pll_khz!=m->pll_khz||s->tx_enabled!=m->tx_enabled||s->scdc20!=m->scdc[0x20]||s->scdc02!=m->scdc[2])fprintf(stderr,"  clock/tx/scdc differ: %u/%u %u/%u tx %u/%u scdc %02x/%02x %02x/%02x\n",s->pixel_khz,m->pixel_khz,s->pll_khz,m->pll_khz,s->tx_enabled,m->tx_enabled,s->scdc20,m->scdc[0x20],s->scdc02,m->scdc[2]);
}
static nexis_gpu_timing mode_for(const nexis_gpu_timing *base,unsigned vtotal,unsigned hz){
    nexis_gpu_timing t=*base;t.vtotal=vtotal;t.pixel_khz=(uint32_t)((uint64_t)t.htotal*vtotal*hz/1000);return t;
}
static bool running(const model *m){return (m->otg[3][DCN302_R_CONTROL]&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK))==(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK);}
static void start(model *m,nexis_gpu_services *k,rx6600_state *s){
    init(m,k,4,0,3,2,1,2,4,1,1);CHECK(rx6600_probe(s,k)==RX6600_OK);doubles(s);
}
/* ---- scenario 1: complete switches ---- */
static void switches(void){
    model m;nexis_gpu_services k;rx6600_state s;nexis_gpu_scanout out;
    start(&m,&k,&s);
    CHECK(rx6600_read_mode(&s,&out));
    nexis_gpu_timing base=out.timing;
    nexis_gpu_timing t200=mode_for(&base,1111,200),t60=mode_for(&base,1111,60),t144=mode_for(&base,1111,144);
    CHECK(t200.pixel_khz>340000 && t60.pixel_khz<340000);
    /* 240 Hz -> 200 Hz (scrambled): runs the self-test pass first, then the real one. */
    CHECK(!s.sequence_proven && rx6600_set_mode(&s,&t200) && s.sequence_proven && !m.invalid);cases++;
    CHECK(running(&m) && m.tx_enabled && ppm_close(m.pixel_khz,t200.pixel_khz) && m.scdc[0x20]==3 && s.ready && !s.in_modeset);
    CHECK((m.otg[3][DCN302_R_V_TOTAL]&DCN302_V_TOTAL_MASK)==1110 && !(m.surface[1][DCN302_SURFACE_R_HUBP]&1));
    CHECK(rx6600_read_mode(&s,&out) && ppm_close(out.timing.pixel_khz,t200.pixel_khz) && out.timing.vtotal==1111 &&
          out.flags==(NEXIS_GPU_SCANOUT_ACTIVE|NEXIS_GPU_SCANOUT_HDMI|NEXIS_GPU_SCANOUT_CLOCK_MEASURED|NEXIS_GPU_SCANOUT_AUDIO));
    CHECK(nexis_gpu_mode_readback_matches(&out,&t200));cases++;
    /* HDMI audio: endpoint of the stream encoder is configured from the monitor's EDID, enabled, and the wall clock follows the pixel clock. */
    {
        unsigned ep=s.route.stream;
        uint32_t hot=m.az[ep][dcn302_az_ix[DCN302_AZ_R_HOT_PLUG]],d0=m.az[ep][dcn302_az_ix[DCN302_AZ_R_DESCRIPTOR0]];
        CHECK(s.audio_active && (out.flags&NEXIS_GPU_SCANOUT_AUDIO) && (hot&DCN302_AZ_HOT_PLUG_AUDIO_ENABLED_MASK) && !(hot&DCN302_AZ_HOT_PLUG_CLOCK_GATING_DISABLE_MASK));
        CHECK((d0&DCN302_AZ_DESC_MAX_CHANNELS_MASK)==1 && ((d0&DCN302_AZ_DESC_FREQUENCIES_MASK)>>8)==7 && ((d0&DCN302_AZ_DESC_BYTE2_MASK)>>16)==7);
        for(unsigned n=1;n<=13;n++)CHECK(!m.az[ep][dcn302_az_ix[DCN302_AZ_R_DESCRIPTOR0+n]]);
        CHECK((m.az[ep][dcn302_az_ix[DCN302_AZ_R_CHANNEL_SPEAKER]]&DCN302_AZ_HDMI_CONNECTION_MASK) && (m.az[ep][dcn302_az_ix[DCN302_AZ_R_CHANNEL_SPEAKER]]&DCN302_AZ_SPEAKER_ALLOCATION_MASK)==1);
        CHECK(m.dto[1]==240000u && m.dto[2]==t200.pixel_khz*10u && (m.dto[0]&DCN302_AZ_DTO0_SOURCE_SEL_MASK)==3 && !(m.dto[0]&DCN302_AZ_DTO_SEL_MASK));
        CHECK(s.audio_caps.lpcm_rates==7 && s.audio_caps.name_length==8 && !memcmp(s.audio_caps.name,"VG270 W3",8));
        CHECK(dcn302_az_index_bytes[ep] && (m.dig[ep][DCN302_HDMI_R_AFMT_PACKET]&DCN302_HDMI_SAMPLE_SEND_MASK)); /* AFMT sends samples */
    }
    CHECK(!s.hubp_transaction.prepared && !s.hubbub_transaction.prepared && !s.dpp_transaction.prepared && !s.timing_transaction.prepared && !s.dfs_transaction.prepared);
    /* No-op for the running mode. */
    unsigned writes=m.writes;CHECK(rx6600_set_mode(&s,&t200) && m.writes==writes);cases++;
    /* 200 Hz -> 60 Hz (legacy TMDS, scrambling switched off in the monitor). */
    CHECK(rx6600_set_mode(&s,&t60) && running(&m) && ppm_close(m.pixel_khz,t60.pixel_khz) && m.scdc[0x20]==0 && s.ready && s.audio_active && m.dto[2]==t60.pixel_khz*10u);
    CHECK(rx6600_read_mode(&s,&out) && nexis_gpu_mode_readback_matches(&out,&t60));cases++;
    /* 60 Hz -> 144 Hz (still below 340 MHz) -> 200 Hz again. */
    CHECK(rx6600_set_mode(&s,&t144) && m.scdc[0x20]==0 && ppm_close(m.pixel_khz,t144.pixel_khz));cases++;
    CHECK(rx6600_set_mode(&s,&t200) && m.scdc[0x20]==3 && ppm_close(m.pixel_khz,t200.pixel_khz));cases++;
    /* Impossible requests are refused without touching a register. */
    writes=m.writes;nexis_gpu_timing bad=t200;bad.pixel_khz=700000;CHECK(!rx6600_set_mode(&s,&bad) && m.writes==writes && s.ready);
    bad=t200;bad.hactive=1280;CHECK(!rx6600_set_mode(&s,&bad) && m.writes==writes && s.ready);cases++;
    CHECK(!m.invalid);
}
/* ---- scenario 2: a mode that needs scrambling on a monitor without SCDC ---- */
static void no_scdc(void){
    model m;nexis_gpu_services k;rx6600_state s;nexis_gpu_scanout out;
    start(&m,&k,&s);CHECK(rx6600_read_mode(&s,&out));
    nexis_gpu_timing t60=mode_for(&out.timing,1111,60);
    CHECK(rx6600_set_mode(&s,&t60)); /* legacy path works first */
    m.scdc[1]=0; /* sink without SCDC */
    CHECK(rx6600_read_mode(&s,&out));nexis_gpu_timing t200=mode_for(&out.timing,1111,200);
    snap before;take(&m,&before);unsigned writes=m.writes;
    CHECK(!rx6600_set_mode(&s,&t200) && s.ready && m.writes==writes && same(&m,&before) && running(&m) && !m.invalid);cases++;
}
/* ---- scenario 3: failure injection ---- */
static void after_failure(model *m,rx6600_state *s,const snap *before,const snap *mid,const char *what){
    (void)what;
    if(s->sequence_proven && mid)before=mid; /* failure happened in the second pass */
    CHECK(!m->invalid && !s->in_modeset);
    if(s->ready){
        if(!same(m,before))explain(m,before);
        CHECK(same(m,before) && running(m));
        nexis_gpu_scanout out;CHECK(rx6600_read_mode(s,&out));
        CHECK(s->error==RX6600_MODESET_FAILED || s->error==RX6600_OK);restored++;
    }else{
        CHECK(all_stopped(m)); /* never leave a half-restored pipeline running */switched_off++;
    }
    cases++;
}
static void inject(bool proven){
    model m;nexis_gpu_services k;rx6600_state s;nexis_gpu_scanout out;
    /* count the writes/commands/sink accesses of one successful switch */
    start(&m,&k,&s);s.sequence_proven=proven;CHECK(rx6600_read_mode(&s,&out));
    nexis_gpu_timing t200=mode_for(&out.timing,1111,200);
    unsigned w0=m.writes,f0=m.fw_ops,k0=m.sink_ops;CHECK(rx6600_set_mode(&s,&t200));
    unsigned writes=m.writes-w0,commands=m.fw_ops-f0,sink=m.sink_ops-k0;
    if(getenv("RX6600_TRACE"))fprintf(stderr,"switch: %u writes, %u firmware commands, %u monitor accesses\n",writes,commands,sink);
    CHECK(writes>40 && commands>=3 && sink>=4);
    /* With the self-test pass, a failure in the second pass restores the state AFTER the first (successful) pass, not
     * the original one. Obtain that reference state by failing the second pass's first firmware command. */
    snap mid;
    start(&m,&k,&s);s.sequence_proven=proven;CHECK(rx6600_read_mode(&s,&out));
    if(!proven){
        m.fail_fw=m.fw_ops+4;m.fail_fw_kind=0;CHECK(!rx6600_set_mode(&s,&t200) && s.sequence_proven && s.ready);
        take(&m,&mid);
    }
    for(unsigned kind=0;kind<3;kind++)for(unsigned n=1;n<=writes;n++){
        start(&m,&k,&s);s.sequence_proven=proven;CHECK(rx6600_read_mode(&s,&out));
        snap before;take(&m,&before);
        if(kind==2)m.ignore_mmio_write=m.writes+n;else{m.fail_mmio_write=m.writes+n;m.posted=kind==1;}
        bool ok=rx6600_set_mode(&s,&t200);
        if(ok){ /* harmless ignored write, or an audio-only problem: the picture switch is complete, audio is dropped (never the picture) */
            CHECK(s.ready && running(&m) && ppm_close(m.pixel_khz,t200.pixel_khz) && !m.invalid);
            /* With the self-test pass the fault may have hit only the first pass (audio dropped there, restored in the second). */
            cases++;if(s.audio_active)harmless++;else video_only++;continue;
        }
        after_failure(&m,&s,&before,proven?NULL:&mid,"mmio");
    }
    for(unsigned kind=0;kind<2;kind++)for(unsigned n=1;n<=commands;n++){
        start(&m,&k,&s);s.sequence_proven=proven;CHECK(rx6600_read_mode(&s,&out));
        snap before;take(&m,&before);m.fail_fw=m.fw_ops+n;m.fail_fw_kind=kind;
        CHECK(!rx6600_set_mode(&s,&t200));
        after_failure(&m,&s,&before,proven?NULL:&mid,"firmware");
    }
    for(unsigned n=1;n<=sink;n++){
        start(&m,&k,&s);s.sequence_proven=proven;CHECK(rx6600_read_mode(&s,&out));
        snap before;take(&m,&before);m.fail_sink=m.sink_ops+n;
        CHECK(!rx6600_set_mode(&s,&t200));
        after_failure(&m,&s,&before,proven?NULL:&mid,"sink");
    }
    /* the pixel clock comes out 2000 ppm off: measurement rejects it and the old mode returns */
    start(&m,&k,&s);s.sequence_proven=proven;CHECK(rx6600_read_mode(&s,&out));
    snap before;take(&m,&before);m.pll_error_ppm=2000;
    CHECK(!rx6600_set_mode(&s,&t200));
    /* The model applies the same 2000 ppm error to the old clock on relight, so the old picture is not bit-identical
     * to 'before'; the invariant here is only: no violation, and either running or fully off. */
    CHECK(!m.invalid && !s.in_modeset && (s.ready?running(&m):all_stopped(&m)));cases++;
}
int main(void){
    switches();no_scdc();inject(true);inject(false);
    printf("{\"passed\":true,\"cases\":%u,\"failures_restored_with_picture\":%u,\"failures_ended_with_output_off\":%u,\"faulted_switches_that_still_completed_with_audio\":%u,\"switches_completed_video_only_after_audio_fault\":%u,\"modeled_only\":true,\"physical_hardware_verified\":false}\n",cases,restored,switched_off,harmless,video_only);
    return 0;
}
