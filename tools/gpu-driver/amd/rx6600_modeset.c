/* RX6600 (Navi23, DCN3.02) native HDMI mode switch.
 *
 * This file is the orchestrator: it chains the individually tested register
 * transactions of tools/gpu-driver/amd/ (clocks, fetch, color path, timing,
 * pixel resync, ATOM firmware commands, SCDC, HDMI/AFMT, OTG) in the order AMD's
 * Linux driver uses for HDMI (v6.12: link_dpms.c / dcn20_hwseq.c):
 *
 *   plan (display still running, nothing is written to the display path)
 *     1 floors     hard-minimum SMU clocks for memory / fabric / PHY (max level)
 *     2 plan       DFS clock request, DML bandwidth + watermarks, HUBP/HUBBUB/DPP/
 *                  timing/resync transactions are *prepared*, DISPCLK/DPPCLK floors
 *                  raised, sink SCDC capability checked
 *   switch (the screen goes dark here)
 *     3 stop       OTG off                         (undo: relight() below)
 *     4 tx_off     transmitter (old pixel clock)   (undo: relight() below)
 *     5 clocks     display/DPP clock dividers      (undo: restore)
 *     6 fetch      HUBP blank + requestor/DLG/TTU + HUBBUB watermarks (undo: restore)
 *     7 dpp        scaler / color bypass / cursors (undo: restore)
 *     8 pll        ATOM SetPixelClock              (undo: relight())
 *     9 resync     PHYPLL deep-colour resync       (undo: restore)
 *    10 timing     OTG timing + global sync        (undo: restore)
 *    11 scdc       sink scrambling / 1:40 ratio    (undo: restore previous SCDC bytes)
 *    12 hdmi       AFMT/stream attributes, AVMUTE on (undo: restore)
 *    13 audio      AZALIA endpoint descriptors + audio wall-clock DTO (undo: restore; failure = video only)
 *    14 tx_on      ATOM UNIPHY transmitter enable  (undo: transmitter disable)
 *    15 otg        OTG + VTG on                    (undo: OTG off)
 *    16 verify     frame-counter clock measurement + SCDC scrambler/lock status
 *    17 commit     HUBP visible, AVMUTE off, audio packets + endpoint on (undo: blank HUBP, endpoint off)
 *    18 settle     re-prove routing/surface at the new mode, drop old rollback images
 *
 * Any failure undoes the completed steps in reverse (modeset_seq.c) and then
 * "relights" the previous mode: PLL and transmitter with the old pixel clock,
 * OTG on, HUBP visible.  If an undo itself fails, the output stays off and the
 * module reports not-ready (poisoned) - it never turns a half-restored pipeline on.
 *
 * Before the very first change to a target mode, the whole sequence is run once
 * for the CURRENT mode ("self-test").  If the machinery does not work on this
 * board the screen goes dark for a second and the old picture is restored, instead
 * of risking the real target mode.
 *
 * IMPORTANT: none of this has run on physical hardware yet.  Every step logs to
 * the kernel log (see "dmesg") so a failure shows exactly where it stopped.
 */
#include "rx6600.h"
#include "../common/nxlog.h"
#include "../common/modeset_seq.h"
#include "../common/cta_audio.h"
#include <string.h>

#define SCDC_ADDRESS 0x54
#define TX_DISABLE 0u
#define TX_ENABLE 1u

typedef struct {
    rx6600_state *s;
    nexis_gpu_timing target;          /* requested timing (pixel_khz = requested) */
    uint32_t new_khz, old_khz;        /* requested / currently running (measured) pixel clock */
    uint8_t scdc_config;              /* 0 = legacy TMDS, 3 = scrambling + 1:40 (above 340 MHz) */
    bool audio;                       /* send HDMI audio packets */
    bool stopped;                     /* OTG was stopped: failure must relight the old mode */
    bool tx_disabled;                 /* old transmitter was disabled: failure must re-enable it */
    bool scdc_changed;
    bool audio_was_on;                /* the previous mode played audio; a failed switch puts it back */
    dcn302_clock_measurement measured;
} ms;

static uint64_t ms_now(void *context) {
    rx6600_state *s = context;
    return s->services->time_us(s->services->service_context);
}

/* ------------------------------------------------------------------ helpers */

static bool smu_max_level(const rx6600_state *s, enum dcn302_smu_clock clock, uint32_t *mhz) {
    const dcn302_smu_limits *l = &s->smu.clocks[clock];
    uint32_t best = 0;
    if (!l->valid || !l->count) return false;
    for (unsigned i = 0; i < l->count && i < DCN302_SMU_MAX_LEVELS; i++) if (l->frequency_mhz[i] > best) best = l->frequency_mhz[i];
    *mhz = best;
    return best != 0;
}

static bool raise_floor(rx6600_state *s, enum dcn302_smu_clock clock, uint32_t mhz) {
    uint32_t acknowledged = 0;
    if (s->smu.floor_known[clock] && s->smu.floor_mhz[clock] >= mhz) return true;
    if (!s->clock_floor(s, clock, mhz, &acknowledged) || acknowledged < mhz) {
        nx_logf("rx6600: SMU hard minimum %u MHz for clock %u refused (acknowledged %u)", mhz, (unsigned)clock, acknowledged);
        return false;
    }
    return true;
}

/* Firmware (ATOM) command through the guarded path. On success the orchestrator takes over the
 * responsibility for rolling the change back, which lifts the "firmware touched" freeze of the
 * clock/fetch/color/timing callbacks; a failed command stays poisoned (state unknown). */
static bool fw_run(ms *m, enum atom_display_command command, uint32_t khz, unsigned action, bool baseline, const char *what) {
    rx6600_state *s = m->s;
    s->fw_baseline = baseline;
    enum atom_vm_error e = s->firmware_command(s, command, khz, action);
    s->fw_baseline = false;
    if (e != ATOM_VM_OK) {
        /* atom_vm_error_string() is a pointer table and would need runtime relocations: log the number. */
        nx_logf("rx6600: firmware command %s (%u kHz) failed: ATOM error %u (poisoned %u)", what, khz, (unsigned)e, (unsigned)s->firmware_poisoned);
        return false;
    }
    s->firmware_changed = false;
    return true;
}

/* hdmi_scdc_io over the connector's hardware DDC engine. */
static bool scdc_read(void *c, uint8_t address, uint8_t offset, uint8_t *value) { rx6600_state *s = c; return s->sink_read(s, address, offset, value); }
static bool scdc_write(void *c, uint8_t address, uint8_t offset, uint8_t value) { rx6600_state *s = c; return s->sink_write(s, address, offset, value); }
static bool scdc_delay(void *c, uint32_t us) { return ((rx6600_state *)c)->services->delay_us(((rx6600_state *)c)->services->service_context, us); }
static hdmi_scdc_io scdc_io(rx6600_state *s) {
    hdmi_scdc_io io = { s, scdc_read, scdc_write, scdc_delay };
    return io;
}

/* ------------------------------------------------------------------ plan */

static bool step_floors(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    const enum dcn302_smu_clock clocks[] = { DCN302_SMU_UCLK, DCN302_SMU_DCEFCLK, DCN302_SMU_SOCCLK, DCN302_SMU_PHYCLK };
    for (unsigned i = 0; i < 4; i++) {
        uint32_t mhz;
        if (!smu_max_level(s, clocks[i], &mhz)) { nx_logf("rx6600: no SMU level table for clock %u", (unsigned)clocks[i]); return false; }
        if (!raise_floor(s, clocks[i], mhz)) return false;
    }
    return true;
}

static bool step_plan(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    const nexis_gpu_timing *t = &m->target;
    if (!rx6600_still_valid(s)) { nx_logf("rx6600: plan: routing/surface proof failed before the switch"); return false; }
    uint32_t max_disp = 0, max_dpp = 0;
    if (!smu_max_level(s, DCN302_SMU_DISPCLK, &max_disp) || !smu_max_level(s, DCN302_SMU_DPPCLK, &max_dpp)) return false;
    max_disp *= 1000; max_dpp *= 1000;
    /* Display and DPP clocks: start at 115 % of the pixel clock (one pixel per clock, no scaling) and let the
     * AMD bandwidth model (DML) decide; widen until it accepts or the SMU maximum is reached. */
    static const unsigned percent[] = { 115, 130, 150, 175, 200 };
    bool planned = false;
    for (unsigned i = 0; i < sizeof(percent) / sizeof(*percent) && !planned; i++) {
        uint64_t want = (uint64_t)t->pixel_khz * percent[i] / 100;
        dcn302_dfs_request r;
        memset(&r, 0, sizeof(r));
        r.disp_khz = (uint32_t)(want > max_disp ? max_disp : want);
        r.dpp_khz = (uint32_t)(want > max_dpp ? max_dpp : want);
        if (r.disp_khz < s->dfs.disp_khz) r.disp_khz = s->dfs.disp_khz;
        if (r.dpp_khz < s->dfs.dpp_khz) r.dpp_khz = s->dfs.dpp_khz;
        for (unsigned p = 0; p < 5; p++) r.pipe_khz[p] = s->dfs.pipe_khz[p];
        r.pipe_khz[s->surface.hubp] = r.dpp_khz;
        if (!s->display_clocks(s, &r, RX6600_DFS_PREPARE)) {
            nx_logf("rx6600: plan: clock request %u/%u kHz rejected (error %u)", r.disp_khz, r.dpp_khz, (unsigned)s->dfs_transaction.error);
            break;
        }
        if (!raise_floor(s, DCN302_SMU_DISPCLK, s->dfs_transaction.required_disp_floor_mhz) ||
            !raise_floor(s, DCN302_SMU_DPPCLK, s->dfs_transaction.required_dpp_floor_mhz)) break;
        if (s->bandwidth_registers(s, t, true, RX6600_HUBP_PREPARE)) {
            planned = true;
            nx_logf("rx6600: plan: display clock %u kHz, DPP clock %u kHz, VSTARTUP %u", s->dfs_transaction.after.disp_khz,
                    s->dfs_transaction.after.dpp_khz, s->dml_job.output.vstartup);
        } else {
            nx_logf("rx6600: plan: bandwidth model/registers refused %u%% clocks (error %u)", percent[i], (unsigned)s->error);
            if (r.disp_khz >= max_disp && r.dpp_khz >= max_dpp) break;
        }
    }
    if (!planned) return false;
    return true;
}

static bool step_plan_resync(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    unsigned phy = s->board.paths[s->route.path].phy;
    enum dcn302_error e = dcn302_pixel_resync_prepare(&s->io, phy, DCN302_HDMI_RGB8, false, rx6600_new_mode_guard, &s->resync);
    if (e) { nx_logf("rx6600: plan: pixel resync preparation failed (%u)", (unsigned)e); return false; }
    /* The sink must offer SCDC when the mode needs scrambling; find out before the screen goes dark. */
    if (m->scdc_config) {
        uint8_t version = 0;
        if (!s->sink_read(s, SCDC_ADDRESS, 0x01, &version) || !version) {
            nx_logf("rx6600: plan: mode needs scrambling but the sink exposes no SCDC (version %u)", version);
            return false;
        }
    }
    return true;
}

/* HDMI audio: only when the monitor accepts 48 kHz / 16-bit stereo LPCM and the whole audio chain can be prepared.
 * Anything else leaves video-only; audio never blocks the video switch. */
static bool step_plan_audio(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    const nexis_gpu_services *k = s->services;
    m->audio = false;
    nx_audio_caps caps;
    if (!nx_audio_caps_from_edid(k->edid, k->edid_bytes, &caps) || !nx_audio_caps_stereo48(&caps)) {
        nx_logf("rx6600: no usable HDMI audio descriptor in the monitor EDID: video only");
        return true;
    }
    enum dcn302_error e = dcn302_audio_prepare(&s->io, s->route.stream, s->route.otg, m->new_khz, &caps, rx6600_new_mode_guard, &s->audio_tx);
    if (e) { nx_logf("rx6600: audio endpoint %u could not be prepared (%u): video only", s->route.stream, (unsigned)e); return true; }
    s->audio_caps = caps;
    m->audio = true;
    nx_logf("rx6600: HDMI audio planned: endpoint %u, LPCM rates %02x, sizes %x, speakers %02x, monitor '%s'", s->route.stream,
            caps.lpcm_rates, caps.lpcm_sizes, caps.speakers, caps.name);
    return true;
}

static bool plan_undo(void *c) {
    rx6600_state *s = ((ms *)c)->s;
    bool ok = s->bandwidth_registers(s, NULL, false, RX6600_HUBP_CANCEL);
    if (!s->dfs_transaction.dirty && !s->dfs_transaction.applied && !s->dfs_transaction.poisoned) memset(&s->dfs_transaction, 0, sizeof(s->dfs_transaction));
    else ok = false;
    if (!s->resync.dirty && !s->resync.applied && !s->resync.poisoned) memset(&s->resync, 0, sizeof(s->resync));
    else ok = false;
    return ok;
}

/* ------------------------------------------------------------------ switch */

static bool step_stop(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    m->stopped = true; /* set first: a failed disable may already have stopped scan-out */
    if (s->audio_active) { /* Linux: disable_audio_stream before the link goes down */
        m->audio_was_on = true;
        s->audio_active = false;
        if (dcn302_audio_disable(&s->io, s->route.stream)) nx_logf("rx6600: audio endpoint could not be switched off cleanly");
    }
    enum dcn302_error e = dcn302_otg_disable(&s->io, s->route.otg);
    if (e) { nx_logf("rx6600: OTG %u did not stop cleanly (%u)", s->route.otg, (unsigned)e); return false; }
    return true;
}

static bool step_tx_off(void *c) {
    ms *m = c;
    m->tx_disabled = true;
    return fw_run(m, ATOM_DISPLAY_TRANSMITTER, m->old_khz, TX_DISABLE, true, "transmitter disable");
}

static bool step_clocks(void *c) {
    rx6600_state *s = ((ms *)c)->s;
    bool ok = s->display_clocks(s, NULL, RX6600_DFS_APPLY);
    if (!ok) nx_logf("rx6600: display/DPP clock dividers could not be applied (error %u)", (unsigned)s->dfs_transaction.error);
    return ok;
}
static bool undo_clocks(void *c) { rx6600_state *s = ((ms *)c)->s; return s->display_clocks(s, NULL, RX6600_DFS_RESTORE); }

static bool step_fetch(void *c) {
    rx6600_state *s = ((ms *)c)->s;
    if (!s->bandwidth_registers(s, NULL, false, RX6600_HUBP_BLANK)) { nx_logf("rx6600: HUBP did not drain/blank"); return false; }
    if (!s->bandwidth_registers(s, NULL, false, RX6600_HUBP_APPLY)) { nx_logf("rx6600: fetch/watermark registers refused"); return false; }
    return true;
}
static bool undo_fetch(void *c) { rx6600_state *s = ((ms *)c)->s; return s->bandwidth_registers(s, NULL, false, RX6600_HUBP_RESTORE); }

static bool step_dpp(void *c) {
    rx6600_state *s = ((ms *)c)->s;
    bool ok = s->dpp_registers(s, RX6600_DPP_APPLY);
    if (!ok) nx_logf("rx6600: color/scaler path refused (error %u)", (unsigned)s->dpp_transaction.error);
    return ok;
}
static bool undo_dpp(void *c) { rx6600_state *s = ((ms *)c)->s; return s->dpp_registers(s, RX6600_DPP_RESTORE); }

static bool step_pll(void *c) {
    ms *m = c;
    return fw_run(m, ATOM_DISPLAY_PIXEL_CLOCK, m->new_khz, 0, false, "pixel clock");
}

static bool step_resync(void *c) {
    rx6600_state *s = ((ms *)c)->s;
    enum dcn302_error e = dcn302_pixel_resync_apply_disabled(&s->io, &s->resync);
    if (e) nx_logf("rx6600: pixel resync failed (%u)", (unsigned)e);
    return !e;
}
static bool undo_resync(void *c) { rx6600_state *s = ((ms *)c)->s; return !s->resync.prepared || !dcn302_pixel_resync_restore_disabled(&s->io, &s->resync); }

static bool step_timing(void *c) {
    rx6600_state *s = ((ms *)c)->s;
    bool ok = s->timing_registers(s, RX6600_TIMING_APPLY);
    if (!ok) nx_logf("rx6600: OTG timing refused (error %u)", (unsigned)s->timing_transaction.error);
    return ok;
}
static bool undo_timing(void *c) { rx6600_state *s = ((ms *)c)->s; return s->timing_registers(s, RX6600_TIMING_RESTORE); }

static bool step_scdc(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    hdmi_scdc_io io = scdc_io(s);
    enum hdmi_scdc_error e = hdmi_scdc_configure(&io, m->new_khz, false, &s->scdc);
    if (e == HDMI_SCDC_VERSION && !m->scdc_config) { s->scdc.valid = false; return true; } /* legacy sink, nothing to configure */
    if (e) { nx_logf("rx6600: SCDC configuration for %u kHz failed (%u)", m->new_khz, (unsigned)e); return false; }
    m->scdc_changed = true;
    return true;
}
static bool undo_scdc(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    if (!m->scdc_changed || !s->scdc.valid) return true;
    hdmi_scdc_io io = scdc_io(s);
    return hdmi_scdc_restore(&io, &s->scdc) == HDMI_SCDC_OK;
}

static bool step_hdmi(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    enum dcn302_hdmi_error e = dcn302_hdmi_prepare(&s->io, s->route.stream, s->route.link, m->new_khz, m->scdc_config, m->audio, s->route.stream, &s->hdmi);
    if (e) nx_logf("rx6600: HDMI stream/AFMT setup failed (%u)", (unsigned)e);
    return !e;
}
static bool undo_hdmi(void *c) { rx6600_state *s = ((ms *)c)->s; return !s->hdmi.valid || dcn302_hdmi_restore(&s->io, &s->hdmi) == DCN302_HDMI_OK; }

static bool step_audio(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    if (!m->audio) return true;
    enum dcn302_error e = dcn302_audio_apply(&s->io, &s->audio_tx);
    if (e) {
        /* Audio problems must not cost the picture: fall back to video only (the audio transaction rolled itself back). */
        nx_logf("rx6600: audio endpoint/DTO programming failed (%u): video only", (unsigned)e);
        if (s->audio_tx.dirty || s->audio_tx.poisoned) return false; /* could not even restore: abort the whole switch */
        m->audio = false;
        /* AFMT was prepared for audio; redo it video-only. */
        if (s->hdmi.valid && dcn302_hdmi_restore(&s->io, &s->hdmi) != DCN302_HDMI_OK) return false;
        if (dcn302_hdmi_prepare(&s->io, s->route.stream, s->route.link, m->new_khz, m->scdc_config, false, s->route.stream, &s->hdmi) != DCN302_HDMI_OK) return false;
    }
    return true;
}
static bool undo_audio(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    if (!m->audio || !s->audio_tx.prepared || !(s->audio_tx.dirty || s->audio_tx.applied)) return true;
    return dcn302_audio_restore(&s->io, &s->audio_tx) == DCN302_OK;
}

static bool step_tx_on(void *c) {
    ms *m = c;
    return fw_run(m, ATOM_DISPLAY_TRANSMITTER, m->new_khz, TX_ENABLE, false, "transmitter enable");
}
static bool undo_tx_on(void *c) {
    ms *m = c;
    return fw_run(m, ATOM_DISPLAY_TRANSMITTER, m->new_khz, TX_DISABLE, false, "transmitter disable (rollback)");
}

static bool step_otg(void *c) {
    rx6600_state *s = ((ms *)c)->s;
    enum dcn302_error e = dcn302_otg_enable(&s->io, s->route.otg);
    if (e) nx_logf("rx6600: OTG %u did not start (%u)", s->route.otg, (unsigned)e);
    return !e;
}
static bool undo_otg(void *c) { rx6600_state *s = ((ms *)c)->s; return dcn302_otg_disable(&s->io, s->route.otg) == DCN302_OK; }

static bool step_verify(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    enum dcn302_error e = dcn302_clock_measure(&s->io, s->route.otg, ms_now, 16, &m->measured);
    if (e) { nx_logf("rx6600: running mode could not be measured (%u)", (unsigned)e); return false; }
    uint32_t want = m->new_khz, got = m->measured.pixel_khz;
    uint32_t diff = got > want ? got - want : want - got;
    nx_logf("rx6600: measured %u kHz (%u.%03u Hz, +-%u ppm), requested %u kHz", got, m->measured.refresh_millihz / 1000,
            m->measured.refresh_millihz % 1000, m->measured.uncertainty_ppm, want);
    if ((uint64_t)diff * 1000000u > (uint64_t)want * 1000u) { nx_logf("rx6600: measured clock is more than 1000 ppm away from the request"); return false; }
    if (m->scdc_config) { /* only scrambled modes have a sink-side lock to verify; legacy sinks may not implement the status bytes */
        hdmi_scdc_io io = scdc_io(s);
        enum hdmi_scdc_error v = hdmi_scdc_verify_link(&io, want, false);
        if (v) { nx_logf("rx6600: sink does not report scrambler/clock/channel lock (%u)", (unsigned)v); return false; }
    }
    bool connected = false;
    if (dcn302_route_connected(&s->io, &s->board.paths[s->route.path], s->route.hpd, &connected) != DCN302_ROUTE_OK || !connected) {
        nx_logf("rx6600: hot-plug sense lost after the switch");
        return false;
    }
    return true;
}

static bool step_commit(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    enum dcn302_hubp_error h = dcn302_hubp_unblank(&s->io, s->surface.hubp);
    if (h) { nx_logf("rx6600: plane could not be made visible (%u)", (unsigned)h); return false; }
    enum dcn302_hdmi_error e = dcn302_hdmi_commit(&s->io, &s->hdmi);
    if (e) { nx_logf("rx6600: HDMI unmute/audio commit failed (%u)", (unsigned)e); return false; }
    if (m->audio) {
        e = dcn302_audio_enable(&s->io, &s->audio_tx) == DCN302_OK ? DCN302_HDMI_OK : DCN302_HDMI_IO;
        s->audio_active = !e;
        if (e) nx_logf("rx6600: audio endpoint did not switch on (%u): picture only", (unsigned)e);
        else nx_logf("rx6600: HDMI audio endpoint %u enabled", s->route.stream);
    } else s->audio_active = false;
    return true;
}
static bool undo_commit(void *c) {
    rx6600_state *s = ((ms *)c)->s;
    if (s->audio_active) (void)dcn302_audio_disable(&s->io, s->route.stream);
    s->audio_active = false;
    return dcn302_hubp_blank(&s->io, s->surface.hubp, ms_now) == DCN302_HUBP_OK;
}

static bool step_settle(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    nexis_gpu_timing shape = m->target;
    shape.pixel_khz = m->measured.pixel_khz;
    if (!rx6600_refresh_after_modeset(s, &shape)) { nx_logf("rx6600: routing/surface proof failed at the new mode"); return false; }
    s->clock = m->measured;
    return true;
}

/* Previous mode again: PLL + transmitter with the old pixel clock, OTG on, plane visible. All transactions
 * are back at their original values by now, so the guarded firmware path runs in baseline mode. */
static bool relight(void *c) {
    ms *m = c;
    rx6600_state *s = m->s;
    if (!m->stopped) return true; /* display was never touched */
    bool ok = true;
    /* The stop itself may have failed without effect (or an ignored write): the old picture is then still running. */
    {
        nexis_gpu_timing shape;
        bool active = false;
        if (!m->tx_disabled && dcn302_otg_read_shape(&s->io, s->route.otg, &shape, &active) && active) {
            dcn302_clock_measurement running_now;
            if (rx6600_still_valid(s) && dcn302_clock_measure(&s->io, s->route.otg, ms_now, 16, &running_now) == DCN302_OK) {
                nx_logf("rx6600: relight: the previous mode never stopped (%u kHz)", running_now.pixel_khz);
                s->clock = running_now;
                if (m->audio_was_on) { /* the audio endpoint was switched off at the start of the attempt */
                    if (dcn302_audio_enable_endpoint(&s->io, s->route.stream)) nx_logf("rx6600: relight: previous audio endpoint could not be switched on again");
                    else s->audio_active = true;
                }
                return true;
            }
            /* Registers say 'running' but frames do not advance (a half-finished stop): stop it properly first. */
            if (dcn302_otg_disable(&s->io, s->route.otg) != DCN302_OK) { nx_logf("rx6600: relight: half-stopped OTG could not be stopped"); return false; }
        }
    }
    if (m->tx_disabled || s->firmware_changed) {
        s->fw_baseline = true;
        ok = rx6600_baseline_clean(s);
        s->fw_baseline = false;
        if (!ok) { nx_logf("rx6600: relight: state is not back at the baseline"); return false; }
        ok = fw_run(m, ATOM_DISPLAY_PIXEL_CLOCK, m->old_khz, 0, true, "pixel clock (old mode)") &&
             fw_run(m, ATOM_DISPLAY_TRANSMITTER, m->old_khz, TX_ENABLE, true, "transmitter enable (old mode)");
    }
    if (ok) {
        enum dcn302_error e = dcn302_otg_enable(&s->io, s->route.otg);
        if (e) { nx_logf("rx6600: relight: OTG did not start (%u)", (unsigned)e); ok = false; }
    }
    if (ok && dcn302_hubp_unblank(&s->io, s->surface.hubp) != DCN302_HUBP_OK) ok = false;
    if (ok && m->audio_was_on) {
        if (dcn302_audio_enable_endpoint(&s->io, s->route.stream)) nx_logf("rx6600: relight: previous audio endpoint could not be switched on again");
        else s->audio_active = true;
    }
    if (ok) {
        dcn302_clock_measurement again;
        if (dcn302_clock_measure(&s->io, s->route.otg, ms_now, 16, &again) == DCN302_OK) {
            nx_logf("rx6600: previous mode is back: %u kHz", again.pixel_khz);
            s->clock = again;
        } else nx_logf("rx6600: previous mode relit but could not be measured");
    }
    return ok;
}

static void seq_log(const char *event, const char *step) {
    /* One line per transition: "apply plan", "FAILED tx_on", "undo clocks", ... */
    if (event[0] == 'a') nx_logf("rx6600: > %s", step);
    else nx_logf("rx6600: %s %s", event, step);
}

static void put_step(nx_seq_step *step, const char *name, bool (*apply)(void *), bool (*undo)(void *), bool undo_on_fail) {
    volatile nx_seq_step *d = step;
    d->name = name; d->apply = apply; d->undo = undo; d->undo_on_fail = undo_on_fail;
}

static bool modeset_once(rx6600_state *s, const nexis_gpu_timing *target) {
    ms m;
    memset(&m, 0, sizeof(m));
    m.s = s;
    m.target = *target;
    m.new_khz = target->pixel_khz;
    m.old_khz = s->clock.pixel_khz;
    m.scdc_config = m.new_khz > 340000 ? 3 : 0;
    m.audio = false;
    if (!s->ready || s->busy || s->firmware_poisoned) return false;
    /* Built at run time with volatile stores: a constant table of function/string pointers would need runtime
     * relocations, which a position-independent retained module must not have. */
    nx_seq_step steps[20];
    unsigned count = 0;
    put_step(&steps[count++], "floors", step_floors, NULL, false);
    put_step(&steps[count++], "plan", step_plan, plan_undo, true);
    put_step(&steps[count++], "plan-resync", step_plan_resync, NULL, false);
    put_step(&steps[count++], "plan-audio", step_plan_audio, NULL, false);
    put_step(&steps[count++], "stop", step_stop, NULL, false);
    put_step(&steps[count++], "tx_off", step_tx_off, NULL, false);
    put_step(&steps[count++], "clocks", step_clocks, undo_clocks, true);
    put_step(&steps[count++], "fetch", step_fetch, undo_fetch, true);
    put_step(&steps[count++], "dpp", step_dpp, undo_dpp, true);
    put_step(&steps[count++], "pll", step_pll, NULL, false);
    put_step(&steps[count++], "resync", step_resync, undo_resync, true);
    put_step(&steps[count++], "timing", step_timing, undo_timing, true);
    put_step(&steps[count++], "scdc", step_scdc, undo_scdc, true);
    put_step(&steps[count++], "hdmi", step_hdmi, undo_hdmi, true);
    put_step(&steps[count++], "audio", step_audio, undo_audio, true);
    put_step(&steps[count++], "tx_on", step_tx_on, undo_tx_on, true);
    put_step(&steps[count++], "otg", step_otg, undo_otg, true);
    put_step(&steps[count++], "verify", step_verify, NULL, false);
    put_step(&steps[count++], "commit", step_commit, undo_commit, true);
    put_step(&steps[count++], "settle", step_settle, NULL, false);
    nx_logf("rx6600: mode switch %u kHz -> %u kHz (%ux%u, scrambling %s)", m.old_khz, m.new_khz, target->hactive, target->vactive, m.scdc_config ? "on" : "off");
    s->in_modeset = true;
    nx_seq_result r = nx_seq_run(steps, count, &m, relight, seq_log);
    s->in_modeset = false;
    if (r.status == NX_SEQ_OK) {
        rx6600_commit_baseline(s);
        s->modeset_count++;
        s->error = RX6600_OK;
        nx_logf("rx6600: mode switch complete, running %u kHz", s->clock.pixel_khz);
        return true;
    }
    nx_logf("rx6600: mode switch failed at step %u, result %u", r.failed_step, (unsigned)r.status);
    if (r.status == NX_SEQ_FAILED_POISONED || r.status == NX_SEQ_FAILED_RESTORED_DARK) {
        s->ready = false;
        s->error = RX6600_MODESET_POISONED;
        /* A half-switched pipeline must not keep scanning out: make 'off' the defined end state (best effort). */
        (void)dcn302_otg_disable(&s->io, s->route.otg);
        nx_logf("rx6600: output left OFF; driver disabled until the next boot");
    } else {
        s->error = RX6600_MODESET_FAILED;
        /* the prepared plan was cancelled by plan_undo; clear any remaining rollback images */
        rx6600_commit_baseline(s);
        rx6600_resample(s);
    }
    return false;
}

bool rx6600_modeset(rx6600_state *s, const nexis_gpu_timing *target) {
    if (!s || !target || !s->ready || s->busy) return false;
    /* Reject impossible requests before anything is touched - including the self-test below. */
    if (target->hactive != s->route.shape.hactive || target->vactive != s->route.shape.vactive) { nx_logf("rx6600: resolution changes are not supported"); return false; }
    if (target->pixel_khz < 25000 || target->pixel_khz > s->route.max_tmds_khz) { nx_logf("rx6600: %u kHz outside the connector range 25000..%u", target->pixel_khz, s->route.max_tmds_khz); return false; }
    if (!s->sequence_proven) {
        nexis_gpu_timing same = s->route.shape;
        same.pixel_khz = s->clock.pixel_khz;
        nx_logf("rx6600: self-test: running the full native sequence once for the CURRENT mode first");
        if (!modeset_once(s, &same)) return false;
        s->sequence_proven = true;
    }
    return modeset_once(s, target);
}
