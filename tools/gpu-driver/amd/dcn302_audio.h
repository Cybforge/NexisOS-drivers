#ifndef NEXIS_DCN302_AUDIO_H
#define NEXIS_DCN302_AUDIO_H
#include "dcn302_otg.h"
#include "dcn302_audio_regs.h"
#include "../common/cta_audio.h"
/* Native Navi23 HDMI-audio endpoint (AZALIA) and audio wall-clock (DCCG DTO) programming, following
 * AMD Linux v6.12 dce_audio.c (az_configure / az_enable / wall_dto_setup).  This is the display
 * engine side of HDMI audio: it publishes the monitor's audio capabilities to the GPU's HD-Audio codec
 * (which the kernel's HDA driver reads through the codec pin verbs) and provides the 128*Fs audio
 * clock derived from the pixel clock.  AFMT packet/ACR programming is dcn302_hdmi.c; the PCM data
 * path is the kernel's HDA driver.
 *
 * Stereo linear PCM only: descriptor 0 carries the monitor's LPCM capabilities, all other format
 * descriptors are cleared so the codec never advertises compressed formats.  Registers are
 * accessed indirectly (INDEX/DATA per endpoint).  prepare() is read-only; apply()/restore() are
 * guarded read-modify-write transactions with readback; enable()/disable() flip AUDIO_ENABLED. */
typedef struct {
    dcn302_io owner;
    bool (*guard)(void *);      /* parent proves its own state before every write */
    unsigned endpoint,otg;
    uint32_t before[DCN302_AZ_REGISTER_COUNT],after[DCN302_AZ_REGISTER_COUNT];
    uint32_t dto_before[3],dto_after[3]; /* source select, phase, module */
    bool prepared,dirty,applied,enabled,poisoned;
    enum dcn302_error error;
} dcn302_audio_transaction;
enum dcn302_error dcn302_audio_prepare(const dcn302_io *,unsigned endpoint,unsigned otg,uint32_t pixel_khz,
    const nx_audio_caps *,bool (*guard)(void *),dcn302_audio_transaction *);
enum dcn302_error dcn302_audio_apply(const dcn302_io *,dcn302_audio_transaction *);
enum dcn302_error dcn302_audio_restore(const dcn302_io *,dcn302_audio_transaction *);
/* Switch the endpoint on after video and AFMT run (Linux: dce110_enable_audio_stream -> az_enable). */
enum dcn302_error dcn302_audio_enable(const dcn302_io *,dcn302_audio_transaction *);
/* Stateless variant of enable(): puts the previous mode's audio back after an aborted switch. */
enum dcn302_error dcn302_audio_enable_endpoint(const dcn302_io *,unsigned endpoint);
/* Endpoint off (Linux: az_disable). Stateless: usable before a mode switch for the previous mode. */
enum dcn302_error dcn302_audio_disable(const dcn302_io *,unsigned endpoint);
#endif
