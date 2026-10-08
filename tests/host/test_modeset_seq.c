/* Host test of the generic apply/undo/relight runner (tools/gpu-driver/common/modeset_seq.c).
 * Every step can be scripted to fail in apply() or undo(); the test checks the exact
 * call order, that failing steps with undo_on_fail get reverted, that nothing is
 * relit after a failed undo, and that a poisoned result is reported. */
#include "../../tools/gpu-driver/common/modeset_seq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 6u
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases;
typedef struct {
    unsigned fail_apply, fail_undo, fail_relight; /* 1-based step index, 0 = never */
    char trace[256]; unsigned length;
} script;
static void add(script *s, char kind, unsigned step) {
    CHECK(s->length + 2 < sizeof(s->trace));
    s->trace[s->length++] = kind;
    s->trace[s->length++] = (char)('0' + step);
    s->trace[s->length] = 0;
}
#define STEP(i) \
    static bool apply##i(void *c) { script *s = c; add(s, 'a', i); return s->fail_apply != i; } \
    static bool undo##i(void *c) __attribute__((unused)); static bool undo##i(void *c)  { script *s = c; add(s, 'u', i); return s->fail_undo  != i; }
STEP(1) STEP(2) STEP(3) STEP(4) STEP(5) STEP(6)
static bool relight(void *c) { script *s = c; s->trace[s->length++] = 'R'; s->trace[s->length] = 0; return !s->fail_relight; }
static const nx_seq_step steps[N] = {
    {"one", apply1, undo1, false}, {"two", apply2, NULL, false}, {"three", apply3, undo3, true},
    {"four", apply4, undo4, false}, {"five", apply5, undo5, true}, {"six", apply6, undo6, false},
};
int main(void) {
    script s;
    /* all steps succeed: no undo, no relight */
    memset(&s, 0, sizeof s);
    nx_seq_result r = nx_seq_run(steps, N, &s, relight, NULL);
    CHECK(r.status == NX_SEQ_OK && r.failed_step == ~0u && !strcmp(s.trace, "a1a2a3a4a5a6")); cases++;
    /* a failure at step k: steps before it are undone in reverse; the failing step only when undo_on_fail */
    struct { unsigned fail; const char *trace; } ok_cases[] = {
        {1, "a1"},                                  /* nothing to undo: step one has undo_on_fail=false */
        {2, "a1a2u1R"},                             /* step two has no undo at all */
        {3, "a1a2a3u3u1R"},                         /* failing step three has undo_on_fail */
        {4, "a1a2a3a4u3u1R"},                       /* step four fails without undo_on_fail; 3 and 1 are undone */
        {5, "a1a2a3a4a5u5u4u3u1R"},                 /* failing step five has undo_on_fail */
        {6, "a1a2a3a4a5a6u5u4u3u1R"},               /* six has an undo but no undo_on_fail */
    };
    for (unsigned i = 0; i < sizeof(ok_cases) / sizeof(*ok_cases); i++) {
        memset(&s, 0, sizeof s); s.fail_apply = ok_cases[i].fail;
        r = nx_seq_run(steps, N, &s, relight, NULL);
        /* a1 alone: nothing completed so nothing is undone, and the previous picture is relit as well */
        char want[64]; strcpy(want, ok_cases[i].trace);
        if (ok_cases[i].fail == 1) strcpy(want, "a1R");
        CHECK(r.status == NX_SEQ_FAILED_RESTORED && r.failed_step == ok_cases[i].fail - 1 && r.undo_failed_step == ~0u);
        if (strcmp(s.trace, want)) { fprintf(stderr, "case %u: trace %s want %s\n", i, s.trace, want); exit(1); }
        cases++;
    }
    /* an undo that fails poisons the result, later undos still run, and relight is NOT attempted */
    memset(&s, 0, sizeof s); s.fail_apply = 6; s.fail_undo = 4;
    r = nx_seq_run(steps, N, &s, relight, NULL);
    CHECK(r.status == NX_SEQ_FAILED_POISONED && r.failed_step == 5 && r.undo_failed_step == 3);
    CHECK(!strcmp(s.trace, "a1a2a3a4a5a6u5u4u3u1")); cases++;
    /* undo of the failing step itself fails */
    memset(&s, 0, sizeof s); s.fail_apply = 5; s.fail_undo = 5;
    r = nx_seq_run(steps, N, &s, relight, NULL);
    CHECK(r.status == NX_SEQ_FAILED_POISONED && r.undo_failed_step == 4 && !strcmp(s.trace, "a1a2a3a4a5u5u4u3u1")); cases++;
    /* relight failure with clean undo: output dark but state clean */
    memset(&s, 0, sizeof s); s.fail_apply = 3; s.fail_relight = 1;
    r = nx_seq_run(steps, N, &s, relight, NULL);
    CHECK(r.status == NX_SEQ_FAILED_RESTORED_DARK && !strcmp(s.trace, "a1a2a3u3u1R")); cases++;
    /* no relight callback */
    memset(&s, 0, sizeof s); s.fail_apply = 2;
    r = nx_seq_run(steps, N, &s, NULL, NULL);
    CHECK(r.status == NX_SEQ_FAILED_RESTORED_DARK && !strcmp(s.trace, "a1a2u1")); cases++;
    /* invalid arguments fail closed without touching anything */
    memset(&s, 0, sizeof s);
    r = nx_seq_run(NULL, N, &s, relight, NULL);
    CHECK(r.status != NX_SEQ_OK && !s.length); cases++;
    r = nx_seq_run(steps, 0, &s, relight, NULL);
    CHECK(r.status != NX_SEQ_OK && !s.length); cases++;
    r = nx_seq_run(steps, NX_SEQ_MAX_STEPS + 1, &s, relight, NULL);
    CHECK(r.status != NX_SEQ_OK && !s.length); cases++;
    printf("{\"cases\":%u}\n", cases);
    return 0;
}
