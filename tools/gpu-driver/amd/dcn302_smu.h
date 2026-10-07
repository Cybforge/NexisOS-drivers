#ifndef NEXIS_DCN302_SMU_H
#define NEXIS_DCN302_SMU_H
#include "dcn302_otg.h"
/* Navi23 DAL mailbox, absolute MMIO byte addresses (not DCN segment offsets).
 * Linux v6.12 dcn30_clk_mgr_smu_msg.c, dalsmc.h, dcn30_smu11_driver_if.h. */
#define DCN302_SMU_MESSAGE_BYTES (0x1628au*4u)
#define DCN302_SMU_ARGUMENT_BYTES (0x16273u*4u)
#define DCN302_SMU_RESPONSE_BYTES (0x16274u*4u)
#define DCN302_SMU_INTERFACE 0x40u
#define DCN302_SMU_MAX_LEVELS 32u
enum dcn302_smu_clock {DCN302_SMU_SOCCLK=1,DCN302_SMU_UCLK=2,DCN302_SMU_DCEFCLK=8,DCN302_SMU_DISPCLK=9,DCN302_SMU_DPPCLK=10,DCN302_SMU_PHYCLK=11};
enum dcn302_smu_error {DCN302_SMU_OK,DCN302_SMU_INPUT,DCN302_SMU_IO,DCN302_SMU_BUSY,DCN302_SMU_TIMEOUT,
    DCN302_SMU_PROTOCOL,DCN302_SMU_VERSION,DCN302_SMU_UNSUPPORTED,DCN302_SMU_PREREQUISITE,DCN302_SMU_FAILED,
    DCN302_SMU_READBACK,DCN302_SMU_ROLLBACK};
typedef struct {uint32_t features;uint16_t frequency_mhz[DCN302_SMU_MAX_LEVELS];uint8_t count;bool valid,fine_grained;} dcn302_smu_limits;
typedef struct {
    dcn302_io io;uint64_t (*time_us)(void *);
    uint32_t version;bool ready,busy,poisoned,dispatched;
    enum dcn302_smu_error error;
    dcn302_smu_limits clocks[13];
    uint16_t floor_mhz[13];bool floor_known[13];
} dcn302_smu;
/* Caller serializes the DAL mailbox against every other display operation.
 * Initialization waits for any prior command, tests the mailbox, and checks
 * SMU/header/driver protocol versions. Does not reset SMU or load firmware.
 * These queries write the mailbox, but do not change clock/power policy. */
bool dcn302_smu_open(dcn302_smu *,const dcn302_io *,uint64_t (*time_us)(void *));
bool dcn302_smu_clock_limits(dcn302_smu *,enum dcn302_smu_clock,dcn302_smu_limits *);
/* Actual native SetHardMinByFreq, MHz, with acknowledged configured floor.
 * This is NOT a measured display/DPP/PHY clock or complete modeset. Parent
 * supplies bandwidth/DLG, dentist/DTO and PLL/PHY changes and owns rollback.
 * Prior floor is unknown after takeover: never infer it from a DPM table.
 * Only successful owned requests populate floor_known/floor_mhz. On uncertain
 * dispatch/response they become unknown and the mailbox is quarantined.
 * A quarantined mailbox must not receive another command before reopening
 * after the original request has completed. No automatic retry/reset. */
bool dcn302_smu_set_floor(dcn302_smu *,enum dcn302_smu_clock,uint32_t mhz,uint32_t *acknowledged_mhz);
#endif
