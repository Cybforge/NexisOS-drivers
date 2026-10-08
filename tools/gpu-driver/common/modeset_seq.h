#ifndef NEXIS_GPU_MODESET_SEQ_H
#define NEXIS_GPU_MODESET_SEQ_H
#include <stdbool.h>
#include <stdint.h>
/* Generic "apply in order, undo in reverse" runner shared by every native
 * display backend (AMD DCN, Intel, ...).
 *
 * A mode switch on real hardware is a long chain of register transactions.
 * Each step has an apply() and an undo(); if step N fails, the undo() of every
 * step that ran (N included when undo_on_fail is set, because a failing step may
 * already have written something) is executed in reverse order.  When all
 * undo() calls succeed the optional relight() restores the previous picture;
 * if any undo() fails the output stays OFF (state is poisoned) and relight() is
 * NOT attempted: a half-restored pipeline must never be switched on.
 *
 * The runner contains no hardware knowledge, so its ordering and rollback
 * behaviour is unit-tested on the host with scripted steps
 * (tests/host/test_modeset_seq.c).
 */
#define NX_SEQ_MAX_STEPS 32u
typedef struct {
    const char *name;
    bool (*apply)(void *ctx);   /* true on success */
    bool (*undo)(void *ctx);    /* true when reverted; NULL = nothing to revert */
    bool undo_on_fail;          /* run undo() even if apply() itself failed */
} nx_seq_step;
typedef enum {
    NX_SEQ_OK = 0,
    NX_SEQ_FAILED_RESTORED,     /* a step failed, everything was undone and the old picture relit */
    NX_SEQ_FAILED_RESTORED_DARK,/* a step failed and was undone, but relight() failed or is absent */
    NX_SEQ_FAILED_POISONED      /* a step failed and at least one undo() failed: output stays off */
} nx_seq_status;
typedef struct {
    nx_seq_status status;
    unsigned failed_step;       /* index of the failing step, ~0u when OK */
    unsigned undo_failed_step;  /* index of the first failing undo, ~0u when none */
} nx_seq_result;
/* log may be NULL. relight may be NULL. */
nx_seq_result nx_seq_run(const nx_seq_step *steps, unsigned count, void *ctx,
                         bool (*relight)(void *ctx), void (*log)(const char *event, const char *step));
#endif
