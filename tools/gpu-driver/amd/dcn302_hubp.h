#ifndef NEXIS_DCN302_HUBP_H
#define NEXIS_DCN302_HUBP_H
#include "dcn302_otg.h"
#include "dcn302_hubp_regs.h"

enum dcn302_hubp_error {DCN302_HUBP_OK,DCN302_HUBP_INPUT,DCN302_HUBP_RANGE,
    DCN302_HUBP_IO,DCN302_HUBP_BUSY,DCN302_HUBP_POWER,DCN302_HUBP_TIMEOUT,
    DCN302_HUBP_READBACK,DCN302_HUBP_ROLLBACK};
typedef struct {
    dcn302_io owner;
    nexis_gpu_timing timing;
    dcn302_dml_output request;
    uint32_t before[DCN302_HUBP_REGISTER_COUNT],after[DCN302_HUBP_REGISTER_COUNT];
    uint64_t touched;
    unsigned hubp;
    bool prepared,dirty,applied,poisoned;
    enum dcn302_hubp_error error;
} dcn302_hubp_transaction;
/* Prepare performs no writes. Caller supplies a successful RGB8/linear/VMID0
 * DML result for this timing and serializes the native GPU configuration.
 * Snapshot preserves unrelated RW bits; status/W1C bits are never replayed.
 * Field overflow is rejected before any register is changed. */
enum dcn302_hubp_error dcn302_hubp_prepare(const dcn302_io *,unsigned,
    const nexis_gpu_timing *,const dcn302_dml_output *,dcn302_hubp_transaction *);
/* DCN3 inherits hubp2_set_blank_regs: drain NO_OUTSTANDING_REQ before blank,
 * bounded to 100ms, then HUBP_BLANK_EN=1 and HUBP_TTU_DISABLE=0. Never unblanks
 * on failure. Does not confuse OTG IN_BLANK with the drain flag.
 * Requires a powered/clocked HUBP, not an all-zero gated register bank. */
enum dcn302_hubp_error dcn302_hubp_blank(const dcn302_io *,unsigned,uint64_t (*time_us)(void *));
/* All OTGs/VTGs must be stopped. The selected HUBP must be blanked, drained,
 * enabled and clocked, with native DCN3 TTU-disable=0. Parent owns clocks, scaler/cursor proof, HUBBUB/global
 * sync and final latch/scanout validation; shadow readback is not a modeset.
 * Posted failures are tracked before writes. Only ordinary owned RW fields
 * are restored, in reverse order, after re-proving the native stopped state.
 * Poisoned/dirty means parent must keep the pipeline off. */
enum dcn302_hubp_error dcn302_hubp_apply_disabled(const dcn302_io *,dcn302_hubp_transaction *);
enum dcn302_hubp_error dcn302_hubp_restore_disabled(const dcn302_io *,dcn302_hubp_transaction *);
#endif
