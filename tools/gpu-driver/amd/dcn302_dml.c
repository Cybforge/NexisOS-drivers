#include "dcn302_dml.h"
#include "dml/dcn302_model.h"
#include "dml/display_rq_dlg_calc_30.h"
void dml30_recalculate(struct display_mode_lib *);
void dml30_ModeSupportAndSystemConfigurationFull(struct display_mode_lib *);

static bool clock_valid(uint32_t khz){return khz>=1000 && khz<=4000000;}
static bool separate_workspace(const dcn302_dml_job *job){
    uintptr_t a=(uintptr_t)job,b=(uintptr_t)job->workspace;
    if(!b || (b&7u) || a>UINTPTR_MAX-sizeof(*job) || b>UINTPTR_MAX-sizeof(*job->workspace))return false;
    return a<b?b-a>=sizeof(*job):a-b>=sizeof(*job->workspace);
}
static bool input_valid(const dcn302_dml_input *i){
    const nexis_gpu_timing *t=&i->timing;
    return t->pixel_khz>=10000 && t->pixel_khz<=600000 && !(t->flags&~3u) &&
        t->hactive>=64 && t->hactive<=4096 && t->hactive<t->hsync_start &&
        t->hsync_start<t->hsync_end && t->hsync_end<t->htotal && t->htotal<=8192 &&
        t->vactive>=64 && t->vactive<=4096 && t->vactive<t->vsync_start &&
        t->vsync_start<t->vsync_end && t->vsync_end<t->vtotal && t->vtotal<=8192 &&
        t->vtotal-t->vactive>=16 && i->pitch_pixels>=t->hactive &&
        i->pitch_pixels<=16384 && !(i->pitch_pixels&63u) && i->pipe<5 &&
        i->channels>=1 && i->channels<=16 &&
        (i->channel_bytes==2 || i->channel_bytes==4 || i->channel_bytes==8) &&
        i->dram_mts>=100 && i->dram_mts<=32000 &&
        clock_valid(i->dcf_khz) && clock_valid(i->soc_khz) && clock_valid(i->fabric_khz) &&
        clock_valid(i->disp_khz) && clock_valid(i->dpp_khz) && clock_valid(i->phy_khz) &&
        i->ref_khz>=10000 && i->ref_khz<=100000 &&
        i->vco_khz_q32>=(UINT64_C(1000000)<<32) && i->vco_khz_q32<=(UINT64_C(5000000)<<32);
}
/* Check finite and range before conversion. NaN fails the first comparison;
 * positive infinity fails the second. Round up so no timing budget shrinks. */
static bool ceil_u32(double value,uint32_t *out){
    if(!(value>=0.0 && value<=4294967295.0))return false;
    uint32_t n=(uint32_t)value;
    if((double)n<value){if(n==UINT32_MAX)return false;n++;}
    *out=n;return true;
}
__attribute__((used,noinline)) static void NEXIS_GPU_CALL calculate(void *context){
    dcn302_dml_job *j=context;j->error=DCN302_DML_INPUT;memset(&j->output,0,sizeof(j->output));
    if(!separate_workspace(j) || !input_valid(&j->input))return;
    dcn302_dml_workspace *w=j->workspace;memset(w,0,sizeof(*w));
    const dcn302_dml_input *i=&j->input;const nexis_gpu_timing *t=&i->timing;
    struct display_mode_lib *l=&w->lib;display_e2e_pipe_params_st *p=&w->pipe;
    l->ip=dcn302_ip_template;l->soc=dcn302_soc_template;l->project=DML_PROJECT_DCN30;
    l->soc.num_states=1;l->soc.num_chans=i->channels;l->soc.dram_channel_width_bytes=i->channel_bytes;
    l->soc.min_dcfclk=i->dcf_khz/1000.0;
    l->soc.dispclk_dppclk_vco_speed_mhz=(double)i->vco_khz_q32/4294967296.0/1000.0;
    /* Keep memory/self-refresh policy separate from a pure scanout plan. */
    l->soc.allow_dram_self_refresh_or_dram_clock_change_in_vblank=dm_neither_self_refresh_nor_mclk_switch;
    l->vba.WhenToDoMPCCombine=dm_mpc_never;
    voltage_scaling_st *s=&l->soc.clock_limits[0];
    s->state=0;s->dram_speed_mts=i->dram_mts;s->dcfclk_mhz=i->dcf_khz/1000.0;
    s->socclk_mhz=i->soc_khz/1000.0;s->fabricclk_mhz=i->fabric_khz/1000.0;
    s->dispclk_mhz=i->disp_khz/1000.0;s->dppclk_mhz=i->dpp_khz/1000.0;s->phyclk_mhz=i->phy_khz/1000.0;
    l->soc.clock_limits[1]=*s; /* Upstream uses num_states as a sentinel. */
    /* Volatile stores prohibit a constant table of absolute PIC pointers. */
    volatile struct dml_funcs *f=&l->funcs;
    f->validate=dml30_ModeSupportAndSystemConfigurationFull;f->recalculate=dml30_recalculate;
    f->rq_dlg_get_dlg_reg=dml30_rq_dlg_get_dlg_reg;f->rq_dlg_get_rq_reg=dml30_rq_dlg_get_rq_reg;
    display_pipe_source_params_st *src=&p->pipe.src;display_pipe_dest_params_st *dst=&p->pipe.dest;
    src->source_format=dm_444_32;src->sw_mode=dm_sw_linear;src->source_scan=dm_horz;
    src->surface_width_y=i->pitch_pixels;src->surface_height_y=t->vactive;
    src->viewport_width=t->hactive;src->viewport_height=t->vactive;src->viewport_stationary=true;
    src->data_pitch=i->pitch_pixels;src->dcc_rate=1;src->dcc_rate_chroma=1;
    dst->recout_width=dst->full_recout_width=dst->hactive=t->hactive;
    dst->recout_height=dst->full_recout_height=dst->vactive=t->vactive;
    dst->htotal=t->htotal;dst->vtotal=t->vtotal;dst->vfront_porch=t->vsync_start-t->vactive;
    /* OTG blank coordinates are relative to the end of sync/back porch. */
    dst->hblank_end=t->htotal-t->hsync_start;dst->hblank_start=dst->hblank_end+t->hactive;
    dst->vblank_end=t->vtotal-t->vsync_start;dst->vblank_start=dst->vblank_end+t->vactive;
    dst->vblank_nom=t->vtotal-t->vactive;dst->pixel_rate_mhz=t->pixel_khz/1000.0;dst->otg_inst=i->pipe;
    p->pipe.scale_ratio_depth.hscl_ratio=p->pipe.scale_ratio_depth.vscl_ratio=1;
    p->pipe.scale_ratio_depth.hscl_ratio_c=p->pipe.scale_ratio_depth.vscl_ratio_c=1;
    /* DCN302 inherits dcn30_populate_dml_pipes_from_context: its float-format
     * line buffer is modeled at 16 bits/channel, even for RGB8 scanout. Using
     * the source depth (8) understated native LB consumption by a factor of2. */
    p->pipe.scale_ratio_depth.lb_depth=dm_lb_16;p->pipe.scale_taps.htaps=p->pipe.scale_taps.vtaps=1;
    p->pipe.scale_taps.htaps_c=p->pipe.scale_taps.vtaps_c=1;
    p->dout.output_type=dm_hdmi;p->dout.output_format=dm_444;p->dout.output_bpp=24;p->dout.output_bpc=8;p->dout.dsc_input_bpc=8;
    p->clks_cfg.refclk_mhz=i->ref_khz/1000.0;p->clks_cfg.dcfclk_mhz=s->dcfclk_mhz;p->clks_cfg.socclk_mhz=s->socclk_mhz;
    /* First validation must not enter recalculate with uninitialized ReturnBW. */
    unsigned level=dml_get_voltage_level(l,p,1);
    j->error=DCN302_DML_UNSUPPORTED;
    if(level!=0 || !l->vba.ModeIsSupported || l->vba.DPPPerPlane[0]!=1 || l->vba.MPCCombineEnable[0])return;
    p->clks_cfg.voltage=0;p->clks_cfg.dispclk_mhz=s->dispclk_mhz;p->clks_cfg.dppclk_mhz=s->dppclk_mhz;
    dcn302_dml_output *o=&w->result;j->error=DCN302_DML_OUTPUT;
    if(!ceil_u32(get_dispclk_calculated(l,p,1)*1000,&o->disp_khz) ||
       !ceil_u32(get_dppclk_calculated(l,p,1,0)*1000,&o->dpp_khz) ||
       !o->disp_khz || !o->dpp_khz || o->disp_khz>i->disp_khz || o->dpp_khz>i->dpp_khz ||
       !ceil_u32(get_wm_urgent(l,p,1)*1000,&o->urgent_ns) ||
       !ceil_u32(get_wm_memory_trip(l,p,1)*1000,&o->memory_trip_ns) ||
       !ceil_u32(get_wm_stutter_exit(l,p,1)*1000,&o->stutter_exit_ns) ||
       !ceil_u32(get_wm_stutter_enter_exit(l,p,1)*1000,&o->stutter_enter_exit_ns) ||
       !ceil_u32(get_wm_dram_clock_change(l,p,1)*1000,&o->dram_change_ns) ||
       !ceil_u32(get_fraction_of_urgent_bandwidth(l,p,1)*1000,&o->frac_urg_nom) ||
       !ceil_u32(get_fraction_of_urgent_bandwidth_imm_flip(l,p,1)*1000,&o->frac_urg_flip) ||
       o->frac_urg_nom>1000 || o->frac_urg_flip>1000 ||
       !ceil_u32(get_vstartup(l,p,1,0),&o->vstartup) ||
       !ceil_u32(get_vupdate_offset(l,p,1,0),&o->vupdate_offset) ||
       !ceil_u32(get_vupdate_width(l,p,1,0),&o->vupdate_width) ||
       !ceil_u32(get_vready_offset(l,p,1,0),&o->vready_offset) ||
       !o->vstartup || o->vstartup>=t->vtotal-t->vactive)return;
    dst->vstartup_start=o->vstartup;dst->vupdate_offset=o->vupdate_offset;
    dst->vupdate_width=o->vupdate_width;dst->vready_offset=o->vready_offset;
    /* RQ/DLG units use the supplied effective programmed clocks. The minimum
     * required clocks above are a constraint, not a claim that DFS was set. */
    dml30_rq_dlg_get_rq_reg(l,&o->rq,&p->pipe);
    dml30_rq_dlg_get_dlg_reg(l,&o->dlg,&o->ttu,p,1,0,false,false,false,false,false);
    j->output=*o;j->error=DCN302_DML_OK;
}
__attribute__((naked))
enum nexis_dml_scope_result NEXIS_GPU_CALL dcn302_dml_calculate(dcn302_dml_job *job __attribute__((unused)),unsigned *line __attribute__((unused))){
    __asm__ volatile("mov %rsi,%rdx\nmov %rdi,%rsi\nlea calculate(%rip),%rdi\njmp nexis_dml_scope\n");
}
