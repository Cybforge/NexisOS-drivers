/* Hidden, device-matched GPU driver packages.
 *
 * What happens here, in order, once the desktop is up (called from the
 * Store's idle job poll, but there is NO Store listing for any driver):
 *
 *   1. Look at the primary display adapter that UEFI/GOP is using (boot info).
 *   2. Look it up in the generated catalog (pins.h).  No match -> nothing is
 *      downloaded or installed and the firmware framebuffer stays in use.
 *   3. If a verified copy is already cached in /opt/nexis-drivers (installed
 *      system) load it right away - no network needed.
 *   4. Otherwise (live medium, or first boot after installation) download
 *      <name>.ndpk from the GitHub mirrors in the background, verify the
 *      ECDSA signature, cache it and activate it immediately.
 *   5. Also refresh the cache in the background when a newer signed version
 *      exists; a running driver is never hot-swapped, the new one is used
 *      from the next boot.
 *
 * A live session keeps /opt in RAM, so it downloads again on every boot.  An
 * installed system writes the verified package to its persistent /opt, and
 * the installer copies it there (gpu_install_payload).
 *
 * Safety nets for not-yet-hardware-proven drivers:
 *   - "trial" packages wait COUNTDOWN_MS (10 s) with a visible notice before
 *     the module is started; Esc skips the driver for this boot (the package
 *     stays cached).  Without it a driver that hangs the machine on start-up
 *     would hang every live-medium boot, because nothing survives the reset
 *     there that could remember the crash.
 *   - "trial" packages show a prompt after activation: Enter keeps the new
 *     display mode, Esc (or doing nothing for 15 s) restores the firmware
 *     mode.  A blank screen therefore cannot become permanent.
 *   - /opt/nexis-drivers/<name>.try is written before a driver starts and
 *     removed once it ended cleanly.  If the machine hangs inside the driver,
 *     the marker survives the reset and the same package version is skipped
 *     on the next boot (installed systems only; RAM-backed on live media).
 *   - Kernel options: nogpudriver (never load), gpuautokeep (no countdown and
 *     no trial prompt), gpudriverlocal (download from http://10.0.2.2:8930/
 *     for QEMU tests).
 */
#include "packages.h"
#include "catalog.h"
#include "pins.h"
#include "package_verify.h"
#include "runtime.h"
#include "../pci.h"
#include "../framebuffer.h"
#include "../keyboard.h"
#include "../serial.h"
#include "../../net/http.h"
#include "../../net/net.h"
#include "../../fs/vfs.h"
#include "../../mm/heap.h"
#include "../../include/bootinfo.h"
#include "../../include/string.h"
#include "../../sys/cmdline.h"
#include "../../lib/coop.h"
#include "../../audio/audio.h"
#include "../../arch/x86_64/pit.h"
#include "../../../gui/shell/shell.h"
#include "../../../gui/wm/wm.h"

#define DIR "/opt/nexis-drivers"
#define MAX_FILE (32u + 64u + 1024u * 1024u + 64u)
#define TRIAL_MS 15000u
#define COUNTDOWN_MS 10000u

static struct {
    bool selected, finished, polling, refresh;
    bool audio_done;
    unsigned audio_tries;
    uint64_t audio_next;
    const gpu_catalog_entry *entry;
    pci_device_t *device;
    http_request_t *request;
    unsigned mirror;
    bool loaded;
    uint32_t loaded_version, cached_version, marker_version;
    bool trial;
    uint64_t trial_end;
    unsigned trial_stage;
    /* Start-up countdown of trial packages: a private copy of the verified module waits here (the file buffers
     * it came from are freed right after verification). The key hook only sets cancel; gpu_packages_poll acts
     * on it, so the module never starts and gets cancelled from two contexts at once. */
    bool pending, cancel, declined;
    uint8_t *hold;
    size_t hold_bytes;
    uint32_t hold_version;
    uint64_t countdown_end;
    unsigned countdown_stage;
} P;

/* A driver is "engaged" while it runs, waits in the countdown, or was skipped by the user this boot:
 * in all three cases no second load attempt may start. */
static bool engaged(void) { return P.loaded || P.pending || P.declined; }

static void path_for(char *out, size_t cap, const char *suffix) {
    snprintf(out, cap, DIR "/%s.%s", P.entry->name, suffix);
}

/* Reads a whole file from the VFS under the read lock. Caller frees. */
static uint8_t *read_file(const char *path, size_t *size) {
    vfs_node_t *node = vfs_lookup(path);
    if (!node || node->flags != VFS_FILE || node->length < 100 || node->length > MAX_FILE || !vfs_read_lock(node)) return NULL;
    uint8_t *data = vfs_read_file(path, size);
    vfs_read_unlock(node);
    return data;
}

static void write_synced(const char *path, const void *data, size_t size) {
    if (!vfs_write_file(path, data, size)) { kprintf("[GPU] Cannot write %s\n", path); return; }
    vfs_node_t *n = vfs_lookup(path);
    if (n) { vfs_chmod_node(n, 0644); vfs_sync_node(n); }
}

static void marker_set(uint32_t version) {
    char path[96], text[24];
    path_for(path, sizeof path, "try");
    snprintf(text, sizeof text, "%u\n", version);
    write_synced(path, text, strlen(text));
}
static void marker_clear(void) {
    char path[96];
    path_for(path, sizeof path, "try");
    if (vfs_lookup(path)) vfs_remove(path);
}
static uint32_t marker_read(void) {
    char path[96];
    path_for(path, sizeof path, "try");
    vfs_node_t *n = vfs_lookup(path);
    if (!n || n->flags != VFS_FILE || n->length == 0 || n->length > 32) return 0;
    size_t size = 0;
    uint8_t *d = vfs_read_file(path, &size);
    if (!d) return 0;
    uint32_t v = 0;
    for (size_t i = 0; i < size && d[i] >= '0' && d[i] <= '9'; i++) v = v * 10 + (d[i] - '0');
    kfree(d);
    return v ? v : 0xffffffffu; /* unreadable marker still blocks */
}

/* ---- trial prompt ------------------------------------------------------- */

static void trial_finish(bool keep) {
    P.trial = false;
    if (keep) {
        marker_clear();
        kprintf("[GPU] Display mode confirmed by the user\n");
        notify_post("Grafiktreiber", "Anzeigemodus behalten", "Der native Treiber bleibt aktiv.", NOTIFY_SUCCESS);
        return;
    }
    bool ok = gpu_runtime_revert();
    marker_clear(); /* a clean, user-visible revert is not a crash */
    P.loaded = false;
    kprintf("[GPU] Display mode reverted to firmware output (%s)\n", ok ? "ok" : "restore failed");
    notify_post("Grafiktreiber", ok ? "Zurueckgesetzt" : "Zuruecksetzen fehlgeschlagen",
                ok ? "Firmware-Anzeige wieder aktiv." : "Bitte neu starten (Option nogpudriver).", ok ? NOTIFY_WARNING : NOTIFY_ERROR);
}

bool gpu_trial_key(const key_event_t *ev) {
    if (!ev || !ev->pressed) return false;
    if (P.pending) { /* countdown before start: only Esc is meaningful, other keys keep working for the desktop */
        if (ev->key != KEY_ESC) return false;
        P.cancel = true;
        return true;
    }
    if (!P.trial) return false;
    if (ev->key == KEY_ENTER || ev->key == KEY_KP_ENTER) { trial_finish(true); return true; }
    if (ev->key == KEY_ESC) { trial_finish(false); return true; }
    return false;
}

static void trial_tick(void) {
    if (!P.trial) return;
    uint64_t now = pit_get_ticks();
    if (now >= P.trial_end) { trial_finish(false); return; }
    uint64_t left = P.trial_end - now;
    if (P.trial_stage == 1 && left < 10000) { P.trial_stage = 2; notify_post("Grafiktreiber", "Enter = behalten, Esc = zurueck", "Noch 10 s bis zum Rueckfall", NOTIFY_WARNING); }
    else if (P.trial_stage == 2 && left < 5000) { P.trial_stage = 3; notify_post("Grafiktreiber", "Enter = behalten, Esc = zurueck", "Noch 5 s bis zum Rueckfall", NOTIFY_WARNING); }
}

static void trial_begin(void) {
    P.trial = true;
    P.trial_end = pit_get_ticks() + TRIAL_MS;
    P.trial_stage = 1;
    notify_post("Grafiktreiber", "Enter = behalten, Esc = zurueck", "Ohne Eingabe: Rueckfall in 15 s", NOTIFY_WARNING);
}

/* ---- package selection / loading --------------------------------------- */

static bool entry_matches(const gpu_catalog_entry *e, uint16_t vendor, uint16_t device) {
    if (e->vendor != vendor) return false;
    for (unsigned i = 0; i < e->device_count; i++) if (e->devices[i] == device) return true;
    return false;
}

static void select_package(void) {
    P.selected = true;
    if (cmdline_has("nogpudriver")) { kprintf("[GPU] Driver loading disabled (nogpudriver)\n"); P.finished = true; return; }
    const nexis_boot_info_t *boot = bootinfo_get();
    if (!boot || !boot->gpu_vendor) { P.finished = true; return; }
    uint16_t vendor = boot->gpu_vendor, device = boot->gpu_device;
    uint8_t bus = boot->gpu_bus, slot = boot->gpu_slot, func = boot->gpu_func;
    for (pci_device_t *d = pci_get_device_list(); d; d = d->next)
        if (d->class_id == 3 && d->vendor_id == vendor && d->device_id == device && d->bus == bus && d->slot == slot && d->func == func) { P.device = d; break; }
    for (unsigned i = 0; i < GPU_CATALOG_COUNT; i++) if (entry_matches(&gpu_catalog[i], vendor, device)) { P.entry = &gpu_catalog[i]; break; }
    if (!P.device || !P.entry) {
        kprintf("[GPU] No driver package for PCI %04x:%04x; firmware framebuffer retained, nothing downloaded\n", vendor, device);
        P.finished = true;
        return;
    }
    kprintf("[GPU] Primary adapter PCI %04x:%04x -> package '%s' (hidden background job, no Store entry)\n", vendor, device, P.entry->name);
}

/* Starts the verified module for the selected adapter (crash marker first, trial prompt after). */
static bool start_module(const uint8_t *module, size_t bytes, uint32_t version) {
    marker_set(version);
    bool ok = gpu_runtime_load(module, bytes, P.device);
    if (ok && P.entry->trial && !cmdline_has("gpuautokeep")) trial_begin();
    else marker_clear();
    if (ok) { P.loaded = true; P.loaded_version = version; }
    return ok;
}

static void countdown_release(void) {
    if (P.hold) kfree(P.hold);
    P.hold = NULL;
    P.hold_bytes = 0;
    P.pending = false;
}

/* Runs from every poll while a trial package waits: Esc skips it for this boot, the end of the countdown starts it. */
static void countdown_tick(void) {
    if (!P.pending) return;
    if (P.cancel) {
        countdown_release();
        P.cancel = false;
        P.declined = true;
        kprintf("[GPU] Driver '%s' skipped by the user; firmware display retained for this boot\n", P.entry->name);
        notify_post("Grafiktreiber", "Uebersprungen", "Firmware-Anzeige bleibt bis zum naechsten Start.", NOTIFY_WARNING);
        return;
    }
    uint64_t now = pit_get_ticks();
    if (now >= P.countdown_end) {
        uint8_t *module = P.hold;
        size_t bytes = P.hold_bytes;
        uint32_t version = P.hold_version;
        P.hold = NULL; /* ownership moves to this frame; pending drops first so no key or poll can touch the copy */
        P.pending = false;
        if (!start_module(module, bytes, version)) {
            kprintf("[GPU] Package '%s' could not be activated\n", P.entry->name);
            notify_post("Grafiktreiber", "Konnte nicht gestartet werden", "Firmware-Anzeige bleibt. Details: Terminal, dmesg", NOTIFY_ERROR);
        }
        kfree(module);
        return;
    }
    uint64_t left = P.countdown_end - now;
    if (P.countdown_stage == 1 && left < 5000) {
        P.countdown_stage = 2;
        notify_post("Grafiktreiber", "Start in 5 s", "Esc = ueberspringen", NOTIFY_WARNING);
    }
}

/* Verifies the package. Trial packages wait in a countdown (see top of file); all others start at once.
 * Returns false only when the package is unusable - a pending or skipped start still counts as handled. */
static bool load_package(const uint8_t *file, size_t size, uint32_t *version) {
    gpu_package_view view;
    if (!gpu_package_open(file, size, P.entry->name, P.entry->min_version, &view)) return false;
    *version = view.version;
    if (P.marker_version && view.version <= P.marker_version) {
        kprintf("[GPU] Package '%s' v%u did not finish starting last time; skipped until a newer version exists\n", P.entry->name, view.version);
        return false;
    }
    if (!P.entry->trial || cmdline_has("gpuautokeep")) return start_module(view.module, view.module_bytes, view.version);
    P.hold = kmalloc(view.module_bytes);
    if (!P.hold) return false;
    memcpy(P.hold, view.module, view.module_bytes);
    P.hold_bytes = view.module_bytes;
    P.hold_version = view.version;
    P.cancel = false;
    P.pending = true;
    P.countdown_stage = 1;
    P.countdown_end = pit_get_ticks() + COUNTDOWN_MS;
    kprintf("[GPU] Package '%s' v%u verified; starting in %u s unless Esc is pressed\n", P.entry->name, view.version, COUNTDOWN_MS / 1000u);
    notify_post("Grafiktreiber", "Start in 10 s", "Esc = ueberspringen", NOTIFY_WARNING);
    return true;
}

static void start_mirror(void) {
    char url[160];
    if (cmdline_has("gpudriverlocal")) snprintf(url, sizeof url, "http://10.0.2.2:8930/%s.ndpk", P.entry->name);
    else snprintf(url, sizeof url, "%s%s.ndpk", gpu_mirrors[P.mirror], P.entry->name);
    http_options_t opts = {0};
    opts.url = url; opts.max_body = MAX_FILE; opts.timeout_ms = 15000; opts.no_cookies = true; opts.identity_encoding = true;
    P.request = http_request_start(&opts);
    if (P.request) kprintf("[GPU] Background download of package '%s' (%s)\n", P.entry->name, P.refresh ? "update check" : "driver needed");
}

static void next_mirror_or_finish(void) {
    if (!cmdline_has("gpudriverlocal") && ++P.mirror < GPU_MIRROR_COUNT) return; /* try the next mirror on the next poll */
    P.finished = true;
    if (!P.loaded && !P.refresh) kprintf("[GPU] Driver unavailable; firmware display retained\n");
}

static void download_done(http_state_t state) {
    size_t size = 0;
    const uint8_t *data = http_body(P.request, &size);
    uint32_t version = 0;
    gpu_package_view view;
    if (state == HTTP_DONE && http_status(P.request) == 200 && gpu_package_open(data, size, P.entry->name, P.entry->min_version, &view)) {
        version = view.version;
        char path[96];
        path_for(path, sizeof path, "ndpk");
        if (version > P.cached_version || !engaged()) write_synced(path, data, size);
        P.cached_version = version > P.cached_version ? version : P.cached_version;
        if (!engaged()) {
            uint32_t v;
            if (!load_package(data, size, &v)) kprintf("[GPU] Downloaded package v%u could not be activated\n", version);
        } else if (P.loaded && version > P.loaded_version) {
            kprintf("[GPU] Newer package v%u cached; it is used from the next boot\n", version);
        }
        P.finished = true;
    } else {
        kprintf("[GPU] Download failed or signature rejected (HTTP %d, %u bytes)\n", http_status(P.request), (unsigned)size);
        http_request_free(P.request);
        P.request = NULL;
        next_mirror_or_finish();
        return;
    }
    http_request_free(P.request);
    P.request = NULL;
}

/* Once the driver reports that its HDMI audio endpoint is on, move the HD-Audio driver to the HDMI codec.
 * The codec needs a moment to see the new sink, so retry a few times; analog output keeps working meanwhile. */
static void audio_follow(void) {
    if (P.audio_done || !P.loaded || !gpu_runtime_audio()) return;
    uint64_t now = pit_get_ticks();
    if (now < P.audio_next) return;
    P.audio_next = now + 500;
    if (audio_rescan_hdmi()) { P.audio_done = true; kprintf("[GPU] HDMI audio output active\n"); return; }
    if (++P.audio_tries >= 20) {
        P.audio_done = true;
        kprintf("[GPU] HDMI audio endpoint is on but the HD-Audio codec reports no usable sink; analog output retained\n");
    }
}

void gpu_packages_poll(void) {
    gpu_runtime_poll();
    countdown_tick();
    trial_tick();
    audio_follow();
    if (P.polling || P.finished) return;
    P.polling = true;
    if (!P.selected) {
        select_package();
        if (P.finished) { P.polling = false; return; }
        P.marker_version = marker_read();
        size_t size = 0;
        uint8_t *cache = NULL;
        char path[96];
        path_for(path, sizeof path, "ndpk");
        cache = read_file(path, &size);
        if (cache) {
            gpu_package_view view;
            if (gpu_package_open(cache, size, P.entry->name, P.entry->min_version, &view)) {
                P.cached_version = view.version;
                uint32_t v;
                if (!load_package(cache, size, &v)) kprintf("[GPU] Cached package not usable; downloading\n");
            } else kprintf("[GPU] Cached package rejected (old format or invalid signature); downloading\n");
            kfree(cache);
        }
        /* With a running cached driver the download is only an update check. */
        P.refresh = engaged();
    }
    if (!P.request) {
        if (!net_is_configured()) { P.polling = false; return; }
        start_mirror();
        if (!P.request) { next_mirror_or_finish(); P.polling = false; return; }
    }
    http_state_t state = http_request_poll(P.request);
    if (state != HTTP_PENDING) download_done(state);
    P.polling = false;
}

bool gpu_driver_active(void) { return P.loaded || gpu_runtime_active(); }

/* The installer copies the verified, cached package into the new system's
 * persistent /opt so the first boot after installation needs no network. */
void *gpu_install_payload(size_t *size, char name[16]) {
    /* Any verified cached package for the matching adapter is persisted, even
     * if activation failed here; a newer version is fetched on the next boot. */
    if (!P.entry) return NULL;
    char path[96];
    path_for(path, sizeof path, "ndpk");
    uint8_t *data = read_file(path, size);
    gpu_package_view view;
    if (data && !gpu_package_open(data, *size, P.entry->name, P.entry->min_version, &view)) { kfree(data); return NULL; }
    if (data) { strncpy(name, P.entry->name, 15); name[15] = 0; }
    return data;
}

void gpu_prepare_install(void) {
    /* Let the selected download finish before the installer copies the cache.
     * Installation stays usable offline; a missing driver is fetched on the
     * first boot of the installed system. */
    uint64_t end = pit_get_ticks() + 5000;
    do { gpu_packages_poll(); if (P.finished) break; net_poll(); coop_yield_check(); } while (pit_get_ticks() < end);
    if (P.request) { http_request_free(P.request); P.request = NULL; P.finished = true; kprintf("[GPU] Installer: download deferred until next boot\n"); }
}
