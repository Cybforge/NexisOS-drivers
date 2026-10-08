#ifndef NEXIS_RX6600_H
#define NEXIS_RX6600_H
#include "dcn302_surface.h"
#include "dcn302_clock.h"
#include "dcn302_smu.h"
#include "dcn302_dfs.h"
#include "atom_memory.h"
#include "dcn302_dml.h"
#include "dcn302_hubp.h"
#include "dcn302_hubbub.h"
#include "dcn302_timing.h"
#include "dcn302_dpp.h"
#include "atom_display_commands.h"
enum rx6600_dfs_operation {RX6600_DFS_PREPARE,RX6600_DFS_APPLY,RX6600_DFS_RESTORE};
enum rx6600_hubp_operation {RX6600_HUBP_PREPARE,RX6600_HUBP_BLANK,RX6600_HUBP_APPLY,RX6600_HUBP_RESTORE,RX6600_HUBP_CANCEL};
enum rx6600_timing_operation {RX6600_TIMING_APPLY,RX6600_TIMING_RESTORE};
enum rx6600_dpp_operation {RX6600_DPP_APPLY,RX6600_DPP_RESTORE};
enum rx6600_error {RX6600_OK,RX6600_INPUT,RX6600_RESOURCE,RX6600_ROM,RX6600_BOARD,RX6600_ROUTE,RX6600_SURFACE,RX6600_CLOCK,RX6600_CHANGED,RX6600_MODESET_PENDING,RX6600_SMU,RX6600_MEMORY,RX6600_BANDWIDTH};
typedef struct {
    const nexis_gpu_services *services;
    dcn302_io io;
    atom_board board;
    atom_memory memory;
    dcn302_route route;
    dcn302_surface surface;
    dcn302_clock_measurement clock;
    dcn302_smu smu;
    dcn302_dfs_snapshot dfs;
    dcn302_reference reference;
    dcn302_dfs_transaction dfs_transaction;
    dcn302_dml_workspace dml_workspace;
    dcn302_dml_job dml_job;
    dcn302_hubp_transaction hubp_transaction;
    dcn302_hubbub_transaction hubbub_transaction;
    dcn302_timing_transaction timing_transaction;
    dcn302_dpp_transaction dpp_transaction;
    dcn302_dfs_snapshot hubp_clock_target;
    uint32_t hubp_floor_mhz[4];
    atom_rom firmware_rom;
    atom_vm firmware_vm;
    uint8_t firmware_scratch[65536],firmware_parameters[60];
    unsigned firmware_parameter_bytes;
    bool firmware_changed,firmware_poisoned;
    enum atom_vm_error firmware_error;
    /* Retained native clock operation for the modeset transaction. The module
     * entry/probe does not invoke it; it is not a terminal/Store control. */
    bool (NEXIS_GPU_CALL *clock_floor)(void *,enum dcn302_smu_clock,uint32_t,uint32_t *);
    bool (NEXIS_GPU_CALL *display_clocks)(void *,const dcn302_dfs_request *,enum rx6600_dfs_operation);
    /* Explicit choice: current clocks or an un-applied prepared DFS target. */
    bool (NEXIS_GPU_CALL *bandwidth_plan)(void *,const nexis_gpu_timing *,bool,dcn302_dml_output *);
    /* PREPARE calculates and binds the successful DML result to the selected
     * clocks/floors/reference. HUBBUB policy/watermarks apply before HUBP;
     * rollback restores HUBP first, then watermarks and old policy last.
     * Other operations take NULL timing and false. */
    bool (NEXIS_GPU_CALL *bandwidth_registers)(void *,const nexis_gpu_timing *,bool,enum rx6600_hubp_operation);
    /* PREPARE above also binds native global sync to the same DML result.
     * Apply only after fetch/WM and DPP are installed. Restore timing before DPP/fetch,
     * policy or clocks; every timing write re-proves the parent state. */
    bool (NEXIS_GPU_CALL *timing_registers)(void *,enum rx6600_timing_operation);
    /* Native float linebuffer/scaler and both physical cursor enables.
     * Apply after fetch/WM; restore after timing and before fetch/WM/clocks. */
    bool (NEXIS_GPU_CALL *dpp_registers)(void *,enum rx6600_dpp_operation);
    /* Board-selected HDMI PLL/stream/PHY command parameters, native MMIO/IIO
     * bytecode transport. No display activation or completed PLL readback.
     * After writes, dependencies remain retained until the future full
     * modeset/old-mode rollback proves hardware state; no silent release. */
    enum atom_vm_error (NEXIS_GPU_CALL *firmware_command)(void *,enum atom_display_command,uint32_t,unsigned);
    nexis_gpu_resource vram,registers;
    uint32_t fixed_rate[3]; /* V_TOTAL_CONTROL, V_TOTAL_MIN, V_TOTAL_MAX */
    uint64_t sampled_us;uint32_t sampled_frame;
    bool ready,busy;
    enum rx6600_error error;
} rx6600_state;
/* Native RX6600 retained backend under construction. Probe/read/poll use real
 * PCI/ATOM/DCN state. Probe queries the native SMU mailbox and actual supported
 * clock levels and completed DFS/DTO clocks; it does not change display or
 * clock/power policy. Retained internal clock operations execute native SMU
 * floor and disabled-pipeline DFS/DTO transactions. A changed mode currently
 * fails explicitly until the PHY/bandwidth/pixel-PLL transaction is implemented. This
 * Pure AMD DCN30 bandwidth/RQ/DLG/TTU math is retained and uses ROM memory
 * geometry, owned SMU floors and current/explicitly prepared DFS clocks. It
 * programs native stopped/blanked HUBP RQ/DLG/TTU and HUBBUB watermarks with
 * actual divided reference and verified rollback. It forces off self refresh
 * and memory clock change while this fixed-floor plan is installed.
 * It binds native stopped timing/global sync to that DML result and retains
 * the DCN3 update-lock/buffer transaction with guarded reverse rollback.
 * It installs the native float-format 1:1 scaler/linebuffer and disables
 * both physical cursors under those guards. Pixel PLL/PHY/audio and actual
 * activation remain incomplete. This is not a completed card driver and
 * must not enter the download catalog yet. */
enum rx6600_error rx6600_probe(rx6600_state *,const nexis_gpu_services *);
bool rx6600_read_mode(rx6600_state *,nexis_gpu_scanout *);
bool rx6600_set_mode(rx6600_state *,const nexis_gpu_timing *);
void rx6600_poll(rx6600_state *);
void rx6600_shutdown(rx6600_state *);
#endif
