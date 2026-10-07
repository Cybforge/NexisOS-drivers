#ifndef NEXIS_DCN302_HUBBUB_H
#define NEXIS_DCN302_HUBBUB_H
#include "dcn302_hubp.h"
#include "dcn302_hubbub_regs.h"
enum dcn302_hubbub_error {DCN302_HUBBUB_OK,DCN302_HUBBUB_INPUT,DCN302_HUBBUB_RANGE,
    DCN302_HUBBUB_IO,DCN302_HUBBUB_BUSY,DCN302_HUBBUB_REFERENCE,DCN302_HUBBUB_READBACK,DCN302_HUBBUB_ROLLBACK};
typedef struct {uint32_t ref_control,timer,crystal_khz,khz;bool valid;} dcn302_reference;
bool dcn302_reference_equal(const dcn302_reference *,const dcn302_reference *);
/* Native DCN3 -> DCN2 clock selection: REFCLK_CLOCK_EN=0 uses ROM XTAL;
 * enabled HUBBUB timer with REFDIV=2 divides by two, otherwise direct.
 * Reject unknown enabled alternative sources, disabled timer and clocks
 * outside AMD's 40..60MHz HUBBUB range. No guessed/default frequency. */
enum dcn302_hubbub_error dcn302_reference_read(const dcn302_io *,uint32_t crystal_khz,dcn302_reference *);
typedef struct {
    uint32_t urgent_ns,memory_trip_ns,stutter_enter_exit_ns,stutter_exit_ns,dram_change_ns;
    uint32_t frac_urg_nom,frac_urg_flip,sat_cycles,min_outstanding;
    uint32_t sr_value,sr_force,pstate_value,pstate_force;
} dcn302_hubbub_values;
typedef struct {
    dcn302_io owner;
    dcn302_reference reference;
    dcn302_dml_output request;
    uint32_t before[DCN302_HUBBUB_REGISTER_COUNT],after[DCN302_HUBBUB_REGISTER_COUNT];
    uint64_t touched;unsigned hubp;
    bool prepared,dirty,applied,poisoned;
    enum dcn302_hubbub_error error;
} dcn302_hubbub_transaction;
/* One minimum-floor RGB8 plan copied to all four selectable watermark sets.
 * Current model permits neither memory-clock switching nor self refresh:
 * native SR/PSTATE allow signals are forced low before changing watermarks.
 * This programs policy/configuration, not a measured memory clock or proof
 * that a firmware transition has completed. Parent owns SMU sequencing and
 * must not submit UCLK changes while this force policy is active. */
enum dcn302_hubbub_error dcn302_hubbub_prepare(const dcn302_io *,unsigned hubp,
    const dcn302_reference *,const dcn302_dml_output *,dcn302_hubbub_transaction *);
/* Requires all OTGs/VTGs stopped, selected HUBP clocked/blanked/drained and
 * unchanged real reference. Preserve reserved/unowned RW fields. Posted
 * failures are tracked; reverse rollback restores the original policy last.
 * Dirty/poisoned means parent must keep scanout off. This is one native step
 * of the full modeset, not a completed physical card driver. */
enum dcn302_hubbub_error dcn302_hubbub_apply_disabled(const dcn302_io *,dcn302_hubbub_transaction *);
enum dcn302_hubbub_error dcn302_hubbub_restore_disabled(const dcn302_io *,dcn302_hubbub_transaction *);
#endif
