#include "dcn302_clock.h"
#include <string.h>
enum dcn302_error dcn302_clock_measure(const dcn302_io *io,unsigned pipe,uint64_t (*time_us)(void *),unsigned frames,dcn302_clock_measurement *out){
    if(!out)return DCN302_INPUT;
    memset(out,0,sizeof(*out));
    if(!io || !io->read || !io->delay_us || !time_us || pipe>=5 || frames<8 || frames>32)return DCN302_INPUT;
    nexis_gpu_timing first,last;bool active;
    if(!dcn302_otg_read_shape(io,pipe,&first,&active))return DCN302_READBACK;
    if(!active)return DCN302_BUSY;
    dcn302_snapshot initial;if(!dcn302_otg_snapshot(io,pipe,&initial))return DCN302_IO;
    uint32_t vtotal=initial.registers[DCN302_R_V_TOTAL]&DCN302_V_TOTAL_MASK;
    uint32_t vcontrol=initial.registers[DCN302_R_V_CONTROL];
    if(((vcontrol&DCN302_V_MIN_SELECT_MASK) && (initial.registers[DCN302_R_V_MIN]&DCN302_V_MIN_MASK)!=vtotal) ||
       ((vcontrol&DCN302_V_MAX_SELECT_MASK) && (initial.registers[DCN302_R_V_MAX]&DCN302_V_MAX_MASK)!=vtotal) ||
       (vcontrol&(DCN302_V_MID_MIN_MASK|DCN302_V_MID_MAX_MASK)))return DCN302_UNSUPPORTED; /* Fixed-rate proof only. */
    uint64_t start=time_us(io->context),previous_before=start;
    uint32_t previous;if(!dcn302_otg_frame_count(io,pipe,&previous))return DCN302_IO;
    uint64_t first_edge=0,last_edge=0,first_width=0,last_width=0,minimum_period=UINT64_MAX,maximum_period=0;
    unsigned edges=0;
    /* Both wall time and iteration count bound a broken/stopped time source. */
    for(unsigned samples=0;samples<1000000;samples++){
        if(!io->delay_us(io->context,1))return DCN302_IO;
        uint64_t before=time_us(io->context);uint32_t count;
        if(!dcn302_otg_frame_count(io,pipe,&count))return DCN302_IO;
        uint64_t after=time_us(io->context);
        if(before<previous_before || after<before || after<start)return DCN302_READBACK;
        if(after-start>2000000)return DCN302_TIMEOUT;
        unsigned delta=(count-previous)&DCN302_FRAME_COUNT_MASK;
        if(delta){
            if(delta!=1 || after-previous_before>16)return DCN302_READBACK;
            uint64_t width=after-previous_before,edge=previous_before+width/2;
            if(!edges){first_edge=edge;first_width=width;}
            else {
                uint64_t period=edge-last_edge;
                if(!period || period<1000 || period>50000)return DCN302_READBACK;
                if(period<minimum_period)minimum_period=period;
                if(period>maximum_period)maximum_period=period;
            }
            last_edge=edge;last_width=width;edges++;
            if(edges==frames+1)break;
        }
        previous=count;previous_before=before;
    }
    if(edges!=frames+1)return DCN302_TIMEOUT;
    if(!dcn302_otg_read_shape(io,pipe,&last,&active) || !active || memcmp(&first,&last,sizeof(first)))return DCN302_READBACK;
    dcn302_snapshot final;
    if(!dcn302_otg_snapshot(io,pipe,&final))return DCN302_IO;
    if(final.registers[DCN302_R_V_MIN]!=initial.registers[DCN302_R_V_MIN] ||
       final.registers[DCN302_R_V_MAX]!=initial.registers[DCN302_R_V_MAX] ||
       final.registers[DCN302_R_V_CONTROL]!=vcontrol ||
       final.registers[DCN302_R_SOURCE]!=initial.registers[DCN302_R_SOURCE])return DCN302_READBACK;
    if(maximum_period-minimum_period>16+minimum_period/5000)return DCN302_READBACK;
    uint64_t elapsed=last_edge-first_edge;
    if(!elapsed)return DCN302_READBACK;
    uint64_t uncertainty=((first_width+last_width+2)*1000000+elapsed-1)/elapsed;
    if(uncertainty>1000)return DCN302_READBACK;
    uint64_t pixels=(uint64_t)first.htotal*first.vtotal;
    uint64_t clock=(pixels*frames*1000+elapsed/2)/elapsed;
    uint64_t refresh=((uint64_t)frames*1000000000+elapsed/2)/elapsed;
    if(!clock || clock>4000000 || refresh<20000 || refresh>1000000)return DCN302_READBACK;
    out->pixel_khz=(uint32_t)clock;out->refresh_millihz=(uint32_t)refresh;
    out->uncertainty_ppm=(uint32_t)uncertainty;out->frames=frames;out->elapsed_us=elapsed;
    return DCN302_OK;
}
