#ifndef NEXIS_RX6600_H
#define NEXIS_RX6600_H
#include "dcn302_surface.h"
#include "dcn302_clock.h"
#include "dcn302_smu.h"
#include "dcn302_dfs.h"
#include "atom_memory.h"
#include "dcn302_dml.h"
enum rx6600_dfs_operation {RX6600_DFS_PREPARE,RX6600_DFS_APPLY,RX6600_DFS_RESTORE};
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
    dcn302_dfs_transaction dfs_transaction;
    dcn302_dml_workspace dml_workspace;
    dcn302_dml_job dml_job;
    /* Retained native clock operation for the modeset transaction. The module
     * entry/probe does not invoke it; it is not a terminal/Store control. */
    bool (NEXIS_GPU_CALL *clock_floor)(void *,enum dcn302_smu_clock,uint32_t,uint32_t *);
    bool (NEXIS_GPU_CALL *display_clocks)(void *,const dcn302_dfs_request *,enum rx6600_dfs_operation);
    /* Explicit choice: current clocks or an un-applied prepared DFS target. */
    bool (NEXIS_GPU_CALL *bandwidth_plan)(void *,const nexis_gpu_timing *,bool,dcn302_dml_output *);
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
 * does not yet program HUBP/HUBBUB/global-sync, prove no native scaler/cursor,
 * or complete pixel PLL/PHY/audio. This is not a completed card driver and
 * must not enter the download catalog yet. */
enum rx6600_error rx6600_probe(rx6600_state *,const nexis_gpu_services *);
bool rx6600_read_mode(rx6600_state *,nexis_gpu_scanout *);
bool rx6600_set_mode(rx6600_state *,const nexis_gpu_timing *);
void rx6600_poll(rx6600_state *);
void rx6600_shutdown(rx6600_state *);
#endif
