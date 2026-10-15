#ifndef NEXIS_DCN302_SMU_H
#define NEXIS_DCN302_SMU_H
#include "dcn302_otg.h"
/* Navi23 DAL mailbox, absolute MMIO byte addresses (not DCN segment offsets).
 * Linux v6.12 dcn30_clk_mgr_smu_msg.c, dalsmc.h, dcn30_smu11_driver_if.h. */
#define DCN302_SMU_MESSAGE_BYTES (0x1628au*4u)
#define DCN302_SMU_ARGUMENT_BYTES (0x16273u*4u)
#define DCN302_SMU_RESPONSE_BYTES (0x16274u*4u)
/* Linux hard-codes the three registers above (Navi21 layout, MP1 aperture base 0x16000). The RX 6600 (Navi23, Dimgrey Cavefish) has its
 * MP1 aperture at 0x16200 (Linux dimgrey_cavefish_ip_offset.h: MP1_BASE__INST0_SEG0), so the same three mailbox registers (MP1 C2PMSG_74,
 * 51, 52) sit 0x200 dwords higher. The first real card never answered at the Navi21 addresses, so the probe tries both sets. */
#define DCN302_SMU_ALT_MESSAGE_BYTES ((0x1628au+0x200u)*4u)
#define DCN302_SMU_ALT_ARGUMENT_BYTES ((0x16273u+0x200u)*4u)
#define DCN302_SMU_ALT_RESPONSE_BYTES ((0x16274u+0x200u)*4u)
#define DCN302_SMU_INTERFACE 0x40u
#define DCN302_SMU_MAX_LEVELS 32u
enum dcn302_smu_clock {DCN302_SMU_SOCCLK=1,DCN302_SMU_UCLK=2,DCN302_SMU_DCEFCLK=8,DCN302_SMU_DISPCLK=9,DCN302_SMU_DPPCLK=10,DCN302_SMU_PHYCLK=11};
enum dcn302_smu_error {DCN302_SMU_OK,DCN302_SMU_INPUT,DCN302_SMU_IO,DCN302_SMU_BUSY,DCN302_SMU_TIMEOUT,
    DCN302_SMU_PROTOCOL,DCN302_SMU_VERSION,DCN302_SMU_UNSUPPORTED,DCN302_SMU_PREREQUISITE,DCN302_SMU_FAILED,
    DCN302_SMU_READBACK,DCN302_SMU_ROLLBACK};
typedef struct {uint32_t features;uint16_t frequency_mhz[DCN302_SMU_MAX_LEVELS];uint8_t count;bool valid,fine_grained;} dcn302_smu_limits;
typedef struct {
    dcn302_io io;uint64_t (*time_us)(void *);
    /* Mailbox registers in use (byte offsets) and which of the two candidate sets answered (0 = Linux/Navi21 addresses, 1 = MP1 base 0x16200). */
    uint32_t message_bytes,argument_bytes,response_bytes;uint8_t mailbox_set;bool probing;
    uint32_t version;bool ready,busy,poisoned,dispatched;
    /* absent: the very first command was never answered (response register stayed 0 for 2 s). The power-management
     * firmware of a card that was only POSTed by the video BIOS does not service the DAL mailbox yet. Linux then keeps
     * smu_present=false and skips ALL clock management (dcn30_clk_mgr.c); so does this driver: no further command is ever
     * sent, the display clocks stay as the BIOS left them, and the memory/fabric/PHY clocks are replaced by conservative
     * stand-ins installed with dcn302_smu_assume() (see rx6600.c, which logs them). */
    bool absent;
    enum dcn302_smu_error error;
    /* Raw replies kept for diagnosis (logged by the owner on any failure). Header and driver-interface numbers are
     * only reported: Linux reads them and ignores a mismatch (dcn30_clk_mgr: "TODO: Check we get what we expect"). */
    uint32_t raw_test,raw_header,raw_interface,raw_response,raw_value;bool header_mismatch,interface_mismatch;
    dcn302_smu_limits clocks[13];
    uint16_t floor_mhz[13];bool floor_known[13];
} dcn302_smu;
/* Caller serializes the DAL mailbox against every other display operation.
 * Initialization waits for any prior command, tests the mailbox, and checks
 * SMU/header/driver protocol versions. Does not reset SMU or load firmware.
 * These queries write the mailbox, but do not change clock/power policy. */
bool dcn302_smu_open(dcn302_smu *,const dcn302_io *,uint64_t (*time_us)(void *));
/* A usable power-management state: either a working mailbox or the documented "absent" mode above. */
static inline bool dcn302_smu_usable(const dcn302_smu *s){return s && (s->ready || s->absent) && !s->poisoned;}
/* Absent mode only: stand-in level for one clock (a single level, floor "known" = assumed). Never talks to hardware. */
bool dcn302_smu_assume(dcn302_smu *,enum dcn302_smu_clock,uint32_t mhz);
/* Read-only diagnosis of the other PMFW mailbox (amdgpu "swsmu", C2PMSG_66/82/90): sends GetSmuVersion only. Returns true
 * when the firmware answered OK. *response is the last response register value (0 = no answer). */
/* set 0: MP1 aperture 0x16000 (Navi21), set 1: 0x16200 (Navi23 per Linux dimgrey_cavefish_ip_offset.h): C2PMSG_66 / 82 / 90. */
#define DCN302_PMFW_MESSAGE_BYTES(set) ((0x16282u+(set)*0x200u)*4u)
#define DCN302_PMFW_ARGUMENT_BYTES(set) ((0x16292u+(set)*0x200u)*4u)
#define DCN302_PMFW_RESPONSE_BYTES(set) ((0x1629au+(set)*0x200u)*4u)
bool dcn302_pmfw_version(const dcn302_io *,uint64_t (*time_us)(void *),unsigned set,uint32_t *response,uint32_t *version);
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
