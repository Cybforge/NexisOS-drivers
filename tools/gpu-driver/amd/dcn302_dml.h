#ifndef NEXIS_DCN302_DML_H
#define NEXIS_DCN302_DML_H
#include "dml/display_mode_lib.h"
/* Native DCN3.02 single independent RGB8 HDMI pipe, linear 32-bit scanout,
 * VMID0, no scaling/DSC/ODM/MPC split or hardware cursor. Input clocks are
 * explicit guaranteed floors, not DPM maxima or inferred current clocks.
 * This pure calculation performs no MMIO and does not authorize a modeset. */
typedef struct {
    nexis_gpu_timing timing;
    uint32_t pitch_pixels,pipe,channels,channel_bytes;
    uint32_t dram_mts,dcf_khz,soc_khz,fabric_khz;
    uint32_t disp_khz,dpp_khz,phy_khz,ref_khz;
    uint64_t vco_khz_q32;
} dcn302_dml_input;
enum dcn302_dml_error {DCN302_DML_OK,DCN302_DML_INPUT,DCN302_DML_UNSUPPORTED,DCN302_DML_OUTPUT};
typedef struct {
    uint32_t disp_khz,dpp_khz,urgent_ns,memory_trip_ns;
    uint32_t stutter_exit_ns,stutter_enter_exit_ns,dram_change_ns;
    uint32_t vstartup,vupdate_offset,vupdate_width,vready_offset;
    display_rq_regs_st rq;
    display_dlg_regs_st dlg;
    display_ttu_regs_st ttu;
    uint32_t frac_urg_nom,frac_urg_flip; /* DML urgent-bandwidth fractions *1000, rounded up. */
} dcn302_dml_output;
typedef struct {struct display_mode_lib lib;display_e2e_pipe_params_st pipe;dcn302_dml_output result;} dcn302_dml_workspace;
typedef struct {
    dcn302_dml_input input;
    dcn302_dml_workspace *workspace;
    dcn302_dml_output output;
    enum dcn302_dml_error error;
} dcn302_dml_job;
/* Enter the FP scope before any C prologue can touch vector state. On scope
 * ASSERT/BUSY/INPUT the output is not usable; only OK+job.error==OK succeeds.
 * Caller supplies an entire writable workspace, separate from job storage.
 * Every invocation starts from a cleared workspace; no stale cache is reused. */
enum nexis_dml_scope_result NEXIS_GPU_CALL dcn302_dml_calculate(dcn302_dml_job *,unsigned *fault_line);
#endif
