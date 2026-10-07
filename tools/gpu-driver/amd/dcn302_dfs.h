#ifndef NEXIS_DCN302_DFS_H
#define NEXIS_DCN302_DFS_H
#include "dcn302_smu.h"
#include "dcn302_dfs_regs.h"
enum dcn302_dfs_error {DCN302_DFS_OK,DCN302_DFS_INPUT,DCN302_DFS_IO,DCN302_DFS_BUSY,DCN302_DFS_UNSUPPORTED,
    DCN302_DFS_TIMEOUT,DCN302_DFS_READBACK,DCN302_DFS_PREREQUISITE,DCN302_DFS_ROLLBACK};
typedef struct {
    uint32_t pll,dentist,dto_control,dto[5];
    uint32_t vco_khz,disp_khz,dpp_khz,pipe_khz[5];
    uint32_t disp_floor_mhz,dpp_floor_mhz;bool valid;
} dcn302_dfs_snapshot;
typedef struct {uint32_t disp_khz,dpp_khz,pipe_khz[5];} dcn302_dfs_request;
typedef struct {
    dcn302_dfs_snapshot before,after;
    dcn302_dfs_request request;
    uint32_t required_disp_floor_mhz,required_dpp_floor_mhz;
    bool prepared,dirty,applied,poisoned;
    enum dcn302_dfs_error error;
} dcn302_dfs_transaction;
/* Real MMIO clock source and completed divider/DTO readback. No guessed boot
 * VCO or requested pixel clock. These are programmed DFS clocks, not a pixel
 * PLL/frame-counter measurement. Pending divider or buffered DTO is rejected. */
enum dcn302_dfs_error dcn302_dfs_read(const dcn302_io *,dcn302_dfs_snapshot *);
/* Pure divider quantization (never returns a clock below the request).
 * A zero pipe clock disables that pipe's DTO; it does not power off its DPP.
 * Parent owns native bandwidth/DLG, pipeline shutdown and clock-floor policy. */
enum dcn302_dfs_error dcn302_dfs_prepare(const dcn302_io *,const dcn302_dfs_request *,dcn302_dfs_transaction *);
/* All five OTGs/VTGs must be stopped, including actual active/busy state.
 * SMU-owned acknowledged floors must cover both old and new global clocks,
 * so a verified rollback remains powered. No firmware floor is inferred.
 * Takes no SMU action itself. DID127 display transitions need FIFO calibration
 * and are explicitly unsupported here. Caller serializes MMIO and DAL SMU.
 * Preserves reserved bits. Failed posted/pending writes are never blindly
 * replayed: rollback is allowed only after a completed stable native snapshot.
 * Failure to prove/restore leaves poisoned/dirty set; parent keeps pipes off. */
enum dcn302_dfs_error dcn302_dfs_apply_disabled(const dcn302_io *,uint64_t (*time_us)(void *),const dcn302_smu *,dcn302_dfs_transaction *);
enum dcn302_dfs_error dcn302_dfs_restore_disabled(const dcn302_io *,uint64_t (*time_us)(void *),const dcn302_smu *,dcn302_dfs_transaction *);
#endif
