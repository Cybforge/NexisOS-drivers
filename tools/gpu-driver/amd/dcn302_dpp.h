#ifndef NEXIS_DCN302_DPP_H
#define NEXIS_DCN302_DPP_H
#include "dcn302_otg.h"
#include "dcn302_dpp_regs.h"
typedef struct {
    dcn302_io owner;
    bool (*guard)(void *); /* Same PCI/DFS/owned floors/installed fetch+WM. */
    nexis_gpu_timing timing;
    uint32_t before[DCN302_DPP_REGISTER_COUNT],after[DCN302_DPP_REGISTER_COUNT],touched;
    unsigned hubp;bool prepared,dirty,applied,poisoned;
    enum dcn302_error error;
} dcn302_dpp_transaction;
/* Native DCN3 float-format RGB8 1:1 scaler: autocal/boundary off, full recout
 * and MPC size, interleave/alpha off, maximum native LB configuration0/63
 * partitions, mode0 (444 scaling bypass, NOT mode6 full DSCL bypass).
 * Native RGB8888 conversion, identity CNV crossbar, unity pre-degamma, CSC/CM
 * bypass and disabled alpha/color key avoid unrelated old color transforms.
 * Both physical cursor enables are disabled: shared HUBP/DPP CURSOR_CONTROL
 * and CNVC CURSOR0_CONTROL. No guessed third cursor register. Existing native
 * viewport must be full size at0,0. Parent establishes native LB power/clocks,
 * including local DPP enable with the native clock test mux off.
 * Pure prepare may sample active firmware mode; all writes/rollback require
 * stopped OTG/VTGs, blanked/drained fetch and retained native parent guard.
 * RO update/coefficient/partition status is never replayed. Configuration
 * readback is not active physical latch evidence. */
enum dcn302_error dcn302_dpp_prepare(const dcn302_io *,unsigned hubp,
    const nexis_gpu_timing *,bool (*guard)(void *),dcn302_dpp_transaction *);
enum dcn302_error dcn302_dpp_apply_disabled(const dcn302_io *,dcn302_dpp_transaction *);
enum dcn302_error dcn302_dpp_restore_disabled(const dcn302_io *,dcn302_dpp_transaction *);
enum dcn302_error dcn302_dpp_verify_installed(const dcn302_io *,const dcn302_dpp_transaction *);
#endif
