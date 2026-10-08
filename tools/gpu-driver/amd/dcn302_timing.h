#ifndef NEXIS_DCN302_TIMING_H
#define NEXIS_DCN302_TIMING_H
#include "dcn302_otg.h"
#include "dcn302_timing_regs.h"
typedef struct {
    dcn302_io owner;
    bool (*guard)(void *); /* Parent proves resource/reference/DFS/floors/fetch/WM. */
    uint64_t (*now)(void *);
    nexis_gpu_timing timing;
    dcn302_sync sync;
    uint32_t before[DCN302_TIMING_REGISTER_COUNT],after[DCN302_TIMING_REGISTER_COUNT];
    uint32_t touched;unsigned pipe;
    bool prepared,dirty,applied,poisoned;
    enum dcn302_error error;
} dcn302_timing_transaction;
/* Native DCN3 timing + global sync, no PLL/PHY/enable. Pure prepare can sample
 * the current active mode; parent-controlled enable/disable and RO status
 * are excluded from configuration identity and never replayed in writes.
 * Parent serializes all operations and provides a complete native guard. */
enum dcn302_error dcn302_timing_prepare(const dcn302_io *,unsigned pipe,
    const nexis_gpu_timing *,const dcn302_sync *,bool (*guard)(void *),
    uint64_t (*now)(void *),dcn302_timing_transaction *);
/* All five OTGs/VTGs must be stopped; selected OTG powered/clocked. Native
 * DCN3 GLOBAL2 lock selector, bounded update-lock acknowledgment, disabled
 * double buffering and checked pending drain. No status/instant-trigger
 * replay. Possibly posted failures are tracked before issuance. Rollback
 * restores timing before the original buffering/global-lock policy last.
 * Dirty/poisoned means parent keeps scanout off and retains dependent fetch,
 * WM and clocks until explicit native restoration succeeds. */
enum dcn302_error dcn302_timing_apply_disabled(const dcn302_io *,dcn302_timing_transaction *);
enum dcn302_error dcn302_timing_restore_disabled(const dcn302_io *,dcn302_timing_transaction *);
#endif
