#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/gpu-driver/amd/dcn302_otg.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t regs[5][DCN302_REGISTER_COUNT],original[5][DCN302_REGISTER_COUNT];
static unsigned operations,fail_at,writes,cases;
static bool posted_failure,persistent_failure,stuck_lock,simulate_running,stuck_running,stuck_counter;
static int ignore_reg=-1;
static bool find(uint32_t address,unsigned *pipe,unsigned *reg){
    for(unsigned p=0;p<5;p++)for(unsigned r=0;r<DCN302_REGISTER_COUNT;r++)if(dcn302_register_bytes[p][r]==address){*pipe=p;*reg=r;return true;}
    return false;
}
static bool failed(void){return fail_at && (operations==fail_at || (persistent_failure && operations>=fail_at));}
static bool read_reg(void *ctx,uint32_t address,uint32_t *value){
    CHECK(ctx==(void *)17);unsigned p,r;CHECK(find(address,&p,&r));operations++;if(failed())return false;
    if(simulate_running && !stuck_running && r==DCN302_R_CONTROL){
        if(regs[p][r]&1)regs[p][r]|=DCN302_MASTER_ACTIVE_MASK;else regs[p][r]&=~DCN302_MASTER_ACTIVE_MASK;
    }
    if(simulate_running && !stuck_counter && r==DCN302_R_FRAME_COUNT && (regs[p][DCN302_R_CONTROL]&1))regs[p][r]++;
    *value=regs[p][r];if(r==DCN302_R_LOCK && !stuck_lock && (regs[p][r]&1))*value|=0x100;return true;
}
static bool write_reg(void *ctx,uint32_t address,uint32_t value){
    CHECK(ctx==(void *)17);unsigned p,r;CHECK(find(address,&p,&r));operations++;writes++;
    CHECK(r!=DCN302_R_CLOCK && r!=DCN302_R_H_DIV && r!=DCN302_R_FORMAT && r!=DCN302_R_FRAME_COUNT && r!=DCN302_R_V_CONTROL && r!=DCN302_R_SOURCE);
    if(failed() && !posted_failure)return false;
    if((int)r!=ignore_reg)regs[p][r]=value;
    return !failed();
}
static bool delay(void *ctx,uint32_t us){CHECK(ctx==(void *)17 && (us==1 || us==10));return true;}
static dcn302_io io={(void *)17,read_reg,write_reg,delay};
static const nexis_gpu_timing old_mode={148500,1920,2008,2052,2200,1080,1084,1089,1125,3};
/* From the user's actual Acer EDID; this is not a physical clock test. */
static const nexis_gpu_timing acer_240={558100,1920,1968,2032,2080,1080,1083,1088,1118,3};
static const dcn302_sync sync_parameters={10,100,80,40,false};
static void fixture(void){
    memset(regs,0,sizeof(regs));operations=fail_at=writes=0;posted_failure=persistent_failure=stuck_lock=false;ignore_reg=-1;
    simulate_running=stuck_running=stuck_counter=false;
    for(unsigned p=0;p<5;p++){
        for(unsigned r=0;r<DCN302_REGISTER_COUNT;r++)regs[p][r]=0x80008000;
        regs[p][DCN302_R_CONTROL]=0x80000000;regs[p][DCN302_R_CLOCK]=0x101;regs[p][DCN302_R_H_DIV]=0;
        regs[p][DCN302_R_FORMAT]=0;regs[p][DCN302_R_V_CONTROL]=0;regs[p][DCN302_R_LOCK]=0;
        regs[p][DCN302_R_SOURCE]=p<<8|15u<<12;
        regs[p][DCN302_R_GLOBAL2]=0x80000000|3u<<25;regs[p][DCN302_R_INTERLACE]=0x80000000;
        regs[p][DCN302_R_H_TOTAL]=0x80008000|2199;regs[p][DCN302_R_V_TOTAL]=0x80008000|1124;
        regs[p][DCN302_R_H_BLANK]=0x80008000|2112u|192u<<16;regs[p][DCN302_R_V_BLANK]=0x80008000|1121u|41u<<16;
        regs[p][DCN302_R_H_SYNC]=0x80008000|44u<<16;regs[p][DCN302_R_V_SYNC]=0x80008000|5u<<16;
        regs[p][DCN302_R_H_POL]=regs[p][DCN302_R_V_POL]=0x80008000;
    }
    memcpy(original,regs,sizeof(regs));
}
static void same_geometry(nexis_gpu_timing actual,const nexis_gpu_timing *expected){actual.pixel_khz=expected->pixel_khz;CHECK(!memcmp(&actual,expected,sizeof(actual)));}
static void successful_pipes(void){
    for(unsigned p=0;p<5;p++){
        fixture();dcn302_snapshot saved;CHECK(dcn302_otg_snapshot(&io,p,&saved) && saved.valid);bool active;nexis_gpu_timing shape;
        CHECK(dcn302_otg_read_shape(&io,p,&shape,&active) && !active && !shape.pixel_khz);same_geometry(shape,&old_mode);
        CHECK(dcn302_otg_program_disabled(&io,p,&acer_240,&sync_parameters)==DCN302_OK);
        CHECK(dcn302_otg_read_shape(&io,p,&shape,&active) && !active && !shape.pixel_khz);same_geometry(shape,&acer_240);
        CHECK((regs[p][DCN302_R_V_MIN]&0x7fff)==1117 && (regs[p][DCN302_R_V_MAX]&0x7fff)==1117);
        CHECK((regs[p][DCN302_R_VTG]&0x7fff)==0 && ((regs[p][DCN302_R_VTG]>>16)&0x7fff)==1115 && !(regs[p][DCN302_R_VTG]&0x80000000));
        for(unsigned q=0;q<5;q++)if(q!=p)CHECK(!memcmp(regs[q],original[q],sizeof(regs[q])));
        CHECK(regs[p][DCN302_R_LOCK]==0 && regs[p][DCN302_R_GLOBAL2]==original[p][DCN302_R_GLOBAL2]);
        CHECK((regs[p][DCN302_R_H_BLANK]&0x80008000)==0x80008000);cases++;
        CHECK(dcn302_otg_restore_disabled(&io,&saved)==DCN302_OK);CHECK(!memcmp(regs,original,sizeof(regs)));cases++;
        regs[p][DCN302_R_CONTROL]|=0x10001;CHECK(dcn302_otg_read_shape(&io,p,&shape,&active) && active);cases++;
        dcn302_io read_only={io.context,read_reg,NULL,NULL};CHECK(dcn302_otg_read_shape(&read_only,p,&shape,&active) && active);cases++;
        regs[p][DCN302_R_FRAME_COUNT]=0xf1234567;uint32_t count;CHECK(dcn302_otg_frame_count(&read_only,p,&count) && count==0x234567);cases++;
    }
}
static void errors(void){
    fixture();CHECK(dcn302_otg_program_disabled(&io,0,&acer_240,&sync_parameters)==DCN302_OK);unsigned successful_operations=operations;
    for(unsigned p=0;p<5;p++)for(unsigned posted=0;posted<2;posted++)for(unsigned n=1;n<=successful_operations;n++){
        fixture();fail_at=n;posted_failure=posted!=0;
        enum dcn302_error result=dcn302_otg_program_disabled(&io,p,&acer_240,&sync_parameters);
        CHECK(result==DCN302_IO);CHECK(!memcmp(regs,original,sizeof(regs)));cases++;
    }
    fixture();ignore_reg=DCN302_R_H_TOTAL;CHECK(dcn302_otg_program_disabled(&io,0,&acer_240,&sync_parameters)==DCN302_READBACK);
    CHECK(!memcmp(regs,original,sizeof(regs)));cases++;
    fixture();stuck_lock=true;CHECK(dcn302_otg_program_disabled(&io,0,&acer_240,&sync_parameters)==DCN302_TIMEOUT);
    CHECK(!memcmp(regs,original,sizeof(regs)));cases++;
    fixture();fail_at=28;persistent_failure=true;CHECK(dcn302_otg_program_disabled(&io,0,&acer_240,&sync_parameters)==DCN302_ROLLBACK);cases++;
    fixture();fail_at=5;dcn302_snapshot incomplete;CHECK(!dcn302_otg_snapshot(&io,0,&incomplete) && !incomplete.valid);
    unsigned before=operations;CHECK(dcn302_otg_restore_disabled(&io,&incomplete)==DCN302_INPUT && operations==before);cases++;
}
static void rejected_modes(void){
    for(unsigned n=0;n<12;n++){
        fixture();nexis_gpu_timing mode=acer_240;
        switch(n){case 0:mode.htotal=32769;break;case 1:mode.vtotal=32769;break;case 2:mode.hactive=0;break;case 3:mode.vactive=0;break;
            case 4:mode.hsync_start=mode.hactive;break;case 5:mode.hsync_end=mode.hsync_start+3;break;case 6:mode.vsync_start=mode.vactive;break;
            case 7:mode.vsync_end=mode.vsync_start;break;case 8:mode.flags=4;break;case 9:mode.pixel_khz=0;break;case 10:mode.pixel_khz=4000001;break;case 11:mode.hsync_end=mode.htotal+1;break;}
        CHECK(dcn302_otg_program_disabled(&io,0,&mode,&sync_parameters)==DCN302_INPUT && !operations && !writes);cases++;
    }
    for(unsigned n=0;n<8;n++){
        fixture();dcn302_sync s=sync_parameters;
        switch(n){case 0:s.vstartup=0;break;case 1:s.vstartup=1024;break;case 2:s.vready=65536;break;case 3:s.vupdate_offset=65536;break;
            case 4:s.vupdate_width=0;break;case 5:s.vupdate_width=1024;break;case 6:s.vstartup=1119;break;case 7:s.vstartup=65535;break;}
        CHECK(dcn302_otg_program_disabled(&io,0,&acer_240,&s)==DCN302_INPUT && !operations && !writes);cases++;
    }
    for(unsigned n=0;n<9;n++){
        fixture();enum dcn302_error expected=DCN302_BUSY;
        switch(n){case 0:regs[0][DCN302_R_CONTROL]|=1;break;case 1:regs[0][DCN302_R_CONTROL]|=0x10000;break;
            case 2:regs[0][DCN302_R_CLOCK]=0;break;case 3:regs[0][DCN302_R_LOCK]=1;break;case 4:regs[0][DCN302_R_LOCK]=0x100;break;
            case 5:regs[0][DCN302_R_INTERLACE]|=1;expected=DCN302_UNSUPPORTED;break;
            case 6:regs[0][DCN302_R_H_DIV]=1;expected=DCN302_UNSUPPORTED;break;
            case 7:regs[0][DCN302_R_FORMAT]=0x10;expected=DCN302_UNSUPPORTED;break;
            case 8:regs[0][DCN302_R_V_CONTROL]=4;expected=DCN302_UNSUPPORTED;break;}
        CHECK(dcn302_otg_program_disabled(&io,0,&acer_240,&sync_parameters)==expected && !writes);cases++;
    }
    fixture();CHECK(dcn302_otg_program_disabled(&io,5,&acer_240,&sync_parameters)==DCN302_INPUT && !operations);cases++;
    /* DP start-point, negative polarities and VTG FP2 when VSTARTUP crosses sync. */
    fixture();nexis_gpu_timing mode=acer_240;mode.flags=0;dcn302_sync s=sync_parameters;s.display_port=true;s.vstartup=100;
    CHECK(dcn302_otg_program_disabled(&io,4,&mode,&s)==DCN302_OK);CHECK((regs[4][DCN302_R_CONTROL]&0x1000) && (regs[4][DCN302_R_VTG]&0x7fff)==64);
    bool active;nexis_gpu_timing shape;CHECK(dcn302_otg_read_shape(&io,4,&shape,&active));same_geometry(shape,&mode);cases++;
}
static void scanout_control(void){
    for(unsigned p=0;p<5;p++){
        fixture();simulate_running=true;
        CHECK(dcn302_otg_enable(&io,p)==DCN302_OK);
        CHECK((regs[p][DCN302_R_CONTROL]&0x10001)==0x10001 && (regs[p][DCN302_R_VTG]&DCN302_VTG_ENABLE_MASK));
        CHECK((regs[p][DCN302_R_SOURCE])==original[p][DCN302_R_SOURCE]);cases++;
        CHECK(dcn302_otg_disable(&io,p)==DCN302_OK && !(regs[p][DCN302_R_CONTROL]&0x10001) && !(regs[p][DCN302_R_VTG]&DCN302_VTG_ENABLE_MASK));cases++;
        fixture();simulate_running=stuck_running=true;CHECK(dcn302_otg_enable(&io,p)==DCN302_TIMEOUT);cases++;
        fixture();simulate_running=stuck_counter=true;CHECK(dcn302_otg_enable(&io,p)==DCN302_TIMEOUT);cases++;
        fixture();regs[p][DCN302_R_SOURCE]=15u<<8;CHECK(dcn302_otg_enable(&io,p)==DCN302_UNSUPPORTED && !writes);cases++;
        fixture();regs[p][DCN302_R_SOURCE]=(1u<<0)|(p<<8)|(p<<12);CHECK(dcn302_otg_program_disabled(&io,p,&acer_240,&sync_parameters)==DCN302_UNSUPPORTED && !writes);cases++;
        fixture();regs[p][DCN302_R_LOCK]=1;CHECK(dcn302_otg_disable(&io,p)==DCN302_BUSY && !writes);cases++;
        fixture();regs[p][DCN302_R_CLOCK]|=DCN302_BUSY_MASK;CHECK(dcn302_otg_program_disabled(&io,p,&acer_240,&sync_parameters)==DCN302_BUSY && !writes);cases++;
        fixture();simulate_running=true;regs[p][DCN302_R_VTG]&=~DCN302_VTG_ENABLE_MASK;ignore_reg=DCN302_R_VTG;CHECK(dcn302_otg_enable(&io,p)==DCN302_READBACK);cases++;
    }
}
int main(void){
    successful_pipes();errors();rejected_modes();scanout_control();
    printf("{\"passed\":true,\"cases\":%u,\"native_dcn302_register_transaction\":true,\"physical_modesetting_verified\":false,\"full_rx6600_driver_complete\":false}\n",cases);return 0;
}
