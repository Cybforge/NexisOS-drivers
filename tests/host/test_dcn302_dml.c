#include "../../tools/gpu-driver/amd/dcn302_dml.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "dml_pic_module.h"
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned checks;
static unsigned cases;
static dcn302_dml_workspace workspace;
static bool zero_output(const dcn302_dml_output *o){dcn302_dml_output zero={0};return !memcmp(o,&zero,sizeof(zero));}
static uint32_t random_state=0x30573ffu;
static uint32_t random_word(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static dcn302_dml_input fixture(void){
    return (dcn302_dml_input){
        .timing={148500,1920,2008,2052,2200,1080,1084,1089,1125,3},
        .pitch_pixels=1920,.pipe=0,.channels=8,.channel_bytes=2,
        .dram_mts=14000,.dcf_khz=600000,.soc_khz=1000000,.fabric_khz=1000000,
        .disp_khz=1000000,.dpp_khz=1000000,.phy_khz=600000,.ref_khz=100000,
        .vco_khz_q32=UINT64_C(3600000)<<32
    };
}
static void pic(const char *path){
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long bytes=ftell(file);CHECK(bytes>64);rewind(file);
    uint8_t *data=malloc((size_t)bytes);CHECK(data && fread(data,1,(size_t)bytes,file)==(size_t)bytes);CHECK(!fclose(file));
    nexis_gpu_image image;CHECK(nexis_gpu_image_parse(data,(size_t)bytes,0x1002,0x73ff,&image));
    void *bases[2];dml_pic_export exports[2];dcn302_dml_job direct={.input=fixture(),.workspace=&workspace};unsigned line=0;
    CHECK(dcn302_dml_calculate(&direct,&line)==NEXIS_DML_SCOPE_OK && direct.error==DCN302_DML_OK);cases++;
    for(unsigned n=0;n<2;n++){
        bases[n]=VirtualAlloc(NULL,image.memory_bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        uint8_t *base=bases[n];CHECK(base && (!n || base!=bases[0]));memcpy(base,data+64,image.image_bytes);DWORD old;
        unsigned code=(image.text_bytes+4095)&~4095u;CHECK(VirtualProtect(base,code,PAGE_EXECUTE_READ,&old));
        if(code<image.writable_offset)CHECK(VirtualProtect(base+code,image.writable_offset-code,PAGE_READONLY,&old));
        CHECK(((dml_pic_entry)(base+image.entry))(&exports[n])==0 && exports[n].bytes==sizeof(exports[n]));
        CHECK(nexis_gpu_image_code_pointer(&image,(uintptr_t)base,(uintptr_t)exports[n].calculate));
        CHECK(nexis_gpu_image_state_pointer(&image,(uintptr_t)base,(uintptr_t)exports[n].job,sizeof(dcn302_dml_job)));
        CHECK(nexis_gpu_image_state_pointer(&image,(uintptr_t)base,(uintptr_t)exports[n].job->workspace,sizeof(workspace)));
        dcn302_dml_job *loaded=exports[n].job;loaded->input=fixture();
        CHECK(exports[n].calculate(loaded,&line)==NEXIS_DML_SCOPE_OK && loaded->error==DCN302_DML_OK && !line);cases++;
        CHECK(!memcmp(&loaded->output,&direct.output,sizeof(direct.output)));
        loaded->input.timing.pixel_khz=0;
        CHECK(exports[n].calculate(loaded,&line)==NEXIS_DML_SCOPE_OK && loaded->error==DCN302_DML_INPUT && zero_output(&loaded->output));cases++;
        loaded->input=fixture();loaded->input.pitch_pixels=16384;
        enum nexis_dml_scope_result r=exports[n].calculate(loaded,&line);cases++;
        CHECK(r==NEXIS_DML_SCOPE_OK || r==NEXIS_DML_SCOPE_ASSERT);
        loaded->input=fixture();
        CHECK(exports[n].calculate(loaded,&line)==NEXIS_DML_SCOPE_OK && loaded->error==DCN302_DML_OK && !line);cases++;
        CHECK(!memcmp(&loaded->output,&direct.output,sizeof(direct.output)));
    }
    CHECK(exports[0].job!=exports[1].job && exports[0].job->workspace!=exports[1].job->workspace);
    for(unsigned n=0;n<2;n++){CHECK(VirtualFree(bases[n],0,MEM_RELEASE));}
    free(data);
}
int main(int argc,char **argv){
    CHECK(argc==2);
    dcn302_dml_job j={.input=fixture(),.workspace=&workspace};unsigned line=0;
    enum nexis_dml_scope_result result=dcn302_dml_calculate(&j,&line);cases++;
    CHECK(result==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_OK && !line);
    CHECK(j.output.disp_khz>=148500 && j.output.dpp_khz>0 && j.output.vstartup>0);
    dcn302_dml_output first=j.output;
    CHECK(dcn302_dml_calculate(&j,&line)==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_OK);cases++;
    CHECK(!memcmp(&j.output,&first,sizeof(first)));
    nexis_gpu_timing modes[]={
        {25175,640,656,752,800,480,490,492,525,0},
        {74250,1280,1390,1430,1650,720,725,730,750,3},
        {297000,1920,2008,2052,2200,1080,1084,1089,1125,3},
        {558100,1920,1968,2032,2080,1080,1083,1088,1118,3},
        {594000,3840,4016,4104,4400,2160,2168,2178,2250,3}
    };
    for(unsigned m=0;m<sizeof(modes)/sizeof(modes[0]);m++){
        j.input=fixture();j.input.timing=modes[m];j.input.pitch_pixels=(modes[m].hactive+63)&~63u;
        result=dcn302_dml_calculate(&j,&line);cases++;
        CHECK(result==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_OK && !line);
        CHECK(j.output.disp_khz>=modes[m].pixel_khz && j.output.urgent_ns>=4000);
        CHECK(j.output.vstartup>=1 && j.output.vstartup<modes[m].vtotal-modes[m].vactive);
    }
    /* Independent units: 100 MHz reference over the exact pixel clock yields
     * fixed-point RQ/DLG reference/pixel ratio and reference cycles per line. */
    CHECK(j.output.dlg.ref_freq_to_pix_freq==(uint32_t)(100000.0/594000.0*(1u<<19)));
    CHECK(j.output.dlg.refcyc_per_htotal==(uint32_t)(100000.0/594000.0*4400*(1u<<8)));
    /* Reject malformed/unbounded input before touching the workspace. */
#define BAD(field,value) do{j.input=fixture();j.input.field=(value);memset(&j.output,0xff,sizeof(j.output)); \
    CHECK(dcn302_dml_calculate(&j,&line)==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_INPUT && !line && zero_output(&j.output));cases++;}while(0)
    BAD(timing.pixel_khz,9999);BAD(timing.pixel_khz,600001);BAD(timing.flags,4);
    BAD(timing.hactive,0);BAD(timing.hactive,4097);BAD(timing.hsync_start,1920);
    BAD(timing.hsync_start,2052);BAD(timing.hsync_end,2200);BAD(timing.htotal,8193);
    BAD(timing.vactive,0);BAD(timing.vactive,4097);BAD(timing.vsync_start,1080);
    BAD(timing.vsync_start,1089);BAD(timing.vsync_end,1125);BAD(timing.vtotal,8193);BAD(timing.vtotal,1095);
    BAD(pitch_pixels,1856);BAD(pitch_pixels,1921);BAD(pitch_pixels,16448);BAD(pipe,5);
    BAD(channels,0);BAD(channels,17);BAD(channel_bytes,0);BAD(channel_bytes,1);BAD(channel_bytes,16);
    BAD(dram_mts,99);BAD(dram_mts,32001);
    BAD(dcf_khz,999);BAD(dcf_khz,4000001);BAD(soc_khz,999);BAD(soc_khz,4000001);
    BAD(fabric_khz,999);BAD(fabric_khz,4000001);BAD(disp_khz,999);BAD(disp_khz,4000001);
    BAD(dpp_khz,999);BAD(dpp_khz,4000001);BAD(phy_khz,999);BAD(phy_khz,4000001);
    BAD(ref_khz,9999);BAD(ref_khz,100001);
    BAD(vco_khz_q32,(UINT64_C(1000000)<<32)-1);BAD(vco_khz_q32,(UINT64_C(5000000)<<32)+1);
#undef BAD
    j.input=fixture();j.workspace=NULL;
    CHECK(dcn302_dml_calculate(&j,&line)==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_INPUT && zero_output(&j.output));cases++;
    j.workspace=(void *)((uintptr_t)&workspace+1);
    CHECK(dcn302_dml_calculate(&j,&line)==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_INPUT && zero_output(&j.output));cases++;
    j.workspace=(void *)&j;
    CHECK(dcn302_dml_calculate(&j,&line)==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_INPUT && zero_output(&j.output));cases++;
    j.workspace=(void *)((uintptr_t)&j-sizeof(workspace)+8);
    CHECK(dcn302_dml_calculate(&j,&line)==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_INPUT && zero_output(&j.output));cases++;
    j.workspace=(void *)(UINTPTR_MAX-7);
    CHECK(dcn302_dml_calculate(&j,&line)==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_INPUT && zero_output(&j.output));cases++;
    CHECK(dcn302_dml_calculate(NULL,&line)==NEXIS_DML_SCOPE_INPUT && !line);cases++;
    CHECK(dcn302_dml_calculate(&j,NULL)==NEXIS_DML_SCOPE_INPUT);cases++;
    j.workspace=&workspace;j.input=fixture();j.input.dram_mts=100;j.input.channels=1;j.input.channel_bytes=2;
    CHECK(dcn302_dml_calculate(&j,&line)==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_UNSUPPORTED && zero_output(&j.output));cases++;
    /* Plausible mode/clock variations exercise the complete upstream VBA/RQ/
     * DLG engine. Unsupported mathematical corners must fail without output;
     * every failure is followed by a normal calculation to check recovery. */
    for(unsigned n=0;n<200;n++){
        j.input=fixture();j.input.timing=modes[random_word()%5];j.input.pitch_pixels=(j.input.timing.hactive+63)&~63u;
        j.input.channels=1+random_word()%16;j.input.channel_bytes=2u<<(random_word()%3);
        j.input.dram_mts=100+random_word()%15901;j.input.pipe=random_word()%5;
        j.input.dcf_khz=100000+random_word()%900001;j.input.soc_khz=100000+random_word()%1900001;
        j.input.fabric_khz=100000+random_word()%1900001;
        j.input.disp_khz=100000+random_word()%1900001;j.input.dpp_khz=100000+random_word()%1900001;
        j.input.ref_khz=10000+random_word()%90001;
        j.input.vco_khz_q32=(UINT64_C(3000000)<<32)+random_word();
        result=dcn302_dml_calculate(&j,&line);cases++;
        CHECK(result==NEXIS_DML_SCOPE_OK || result==NEXIS_DML_SCOPE_ASSERT);
        if(result==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_OK){
            CHECK(!line && j.output.disp_khz<=j.input.disp_khz && j.output.dpp_khz<=j.input.dpp_khz);
        }else CHECK(zero_output(&j.output));
        j.input=fixture();CHECK(dcn302_dml_calculate(&j,&line)==NEXIS_DML_SCOPE_OK && j.error==DCN302_DML_OK && !line);cases++;
        CHECK(!memcmp(&j.output,&first,sizeof(first)));
    }
    pic(argv[1]);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"workspace_bytes\":%zu,\"pure_native_dcn302_bandwidth_math\":true,\"tested_1080p_240hz_math\":true,\"real_pic_calculation_executed\":true,\"distinct_pic_bases_verified\":true,\"physical_hardware_verified\":false}\n",cases,checks,sizeof(workspace));return 0;
}
