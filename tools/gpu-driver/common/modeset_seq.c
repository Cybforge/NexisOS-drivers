#include "modeset_seq.h"

nx_seq_result nx_seq_run(const nx_seq_step *steps, unsigned count, void *ctx,
                         bool (*relight)(void *ctx), void (*log)(const char *event, const char *step)) {
    nx_seq_result r = { NX_SEQ_OK, ~0u, ~0u };
    if (!steps || count == 0 || count > NX_SEQ_MAX_STEPS) {
        r.status = NX_SEQ_FAILED_RESTORED;
        r.failed_step = 0;
        return r;
    }
    unsigned done = 0;      /* steps whose apply() returned true */
    bool attempted_failed = false;
    unsigned n;
    for (n = 0; n < count; n++) {
        if (log) log("apply", steps[n].name);
        if (!steps[n].apply || !steps[n].apply(ctx)) {
            if (log) log("FAILED", steps[n].name);
            r.failed_step = n;
            attempted_failed = true;
            break;
        }
        done = n + 1;
    }
    if (!attempted_failed) return r;
    /* Undo in reverse: the failing step first (if it asks for it), then every completed step. */
    bool undo_ok = true;
    if (steps[r.failed_step].undo_on_fail && steps[r.failed_step].undo) {
        if (log) log("undo", steps[r.failed_step].name);
        if (!steps[r.failed_step].undo(ctx)) {
            undo_ok = false;
            r.undo_failed_step = r.failed_step;
            if (log) log("UNDO FAILED", steps[r.failed_step].name);
        }
    }
    for (n = done; n > 0; n--) {
        const nx_seq_step *s = &steps[n - 1];
        if (!s->undo) continue;
        if (log) log("undo", s->name);
        if (!s->undo(ctx)) {
            if (log) log("UNDO FAILED", s->name);
            if (undo_ok) r.undo_failed_step = n - 1;
            undo_ok = false;
            /* Keep undoing the remaining steps: leaving more hardware modified makes it worse,
             * and each undo() re-proves its own preconditions before writing. */
        }
    }
    if (!undo_ok) {
        r.status = NX_SEQ_FAILED_POISONED;
        return r;
    }
    if (relight) {
        if (log) log("relight", "previous mode");
        if (relight(ctx)) { r.status = NX_SEQ_FAILED_RESTORED; return r; }
        if (log) log("RELIGHT FAILED", "previous mode");
    }
    r.status = NX_SEQ_FAILED_RESTORED_DARK;
    return r;
}
