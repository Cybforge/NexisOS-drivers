#include "nexis_dml_port.h"
/* Explicit SysV context: compiler builtin setjmp uses the Windows SEH frame
 * offset under Clang, which is not the saved RBP after a checked prologue.
 * Save the six nonvolatile GPRs, caller RSP and return address ourselves. */
typedef struct {uintptr_t environment[8];volatile unsigned line;} dml_guard;
__attribute__((naked,noinline,returns_twice))
static int NEXIS_GPU_CALL dml_setjmp(uintptr_t *environment __attribute__((unused))){
    __asm__ volatile("mov %rbx,0(%rdi)\nmov %rbp,8(%rdi)\n"
        "mov %r12,16(%rdi)\nmov %r13,24(%rdi)\n"
        "mov %r14,32(%rdi)\nmov %r15,40(%rdi)\n"
        "lea 8(%rsp),%rax\nmov %rax,48(%rdi)\n"
        "mov (%rsp),%rax\nmov %rax,56(%rdi)\nxor %eax,%eax\nret\n");
}
__attribute__((naked,noinline,noreturn))
static void NEXIS_GPU_CALL dml_longjmp(uintptr_t *environment __attribute__((unused))){
    __asm__ volatile("mov 56(%rdi),%rdx\nmov 48(%rdi),%rsp\n"
        "mov 0(%rdi),%rbx\nmov 8(%rdi),%rbp\n"
        "mov 16(%rdi),%r12\nmov 24(%rdi),%r13\n"
        "mov 32(%rdi),%r14\nmov 40(%rdi),%r15\n"
        "mov $1,%eax\njmp *%rdx\n");
}
static dml_guard *active_guard;
_Noreturn void nexis_dml_assert_failure(unsigned line){
    dml_guard *g=__atomic_load_n(&active_guard,__ATOMIC_ACQUIRE);
    /* Private upstream entry points are never callable outside our scope. */
    if(!g)__builtin_trap();
    g->line=line;dml_longjmp(g->environment);
}
__attribute__((used,noinline))
enum nexis_dml_scope_result NEXIS_GPU_CALL nexis_dml_guard_body(nexis_dml_calculation calculate,void *context,unsigned *line){
    if(line)*line=0;
    if(!calculate || !context || !line)return NEXIS_DML_SCOPE_INPUT;
    dml_guard guard;guard.line=0;dml_guard *expected=NULL;
    if(!__atomic_compare_exchange_n(&active_guard,&expected,&guard,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return NEXIS_DML_SCOPE_BUSY;
    enum nexis_dml_scope_result result=NEXIS_DML_SCOPE_OK;
    if(dml_setjmp(guard.environment))result=NEXIS_DML_SCOPE_ASSERT;
    else calculate(context);
    *line=guard.line;__atomic_store_n(&active_guard,NULL,__ATOMIC_RELEASE);return result;
}
/* Save before any C prologue, vectorized copy or calculation can touch FP
 * registers. SysV outer frame stays 16-byte aligned across the C body call.
 * Only baseline x87/SSE instructions; no AVX upper state is modified. */
__attribute__((naked,used))
enum nexis_dml_scope_result NEXIS_GPU_CALL nexis_dml_scope(nexis_dml_calculation calculate __attribute__((unused)),void *context __attribute__((unused)),unsigned *line __attribute__((unused))){
    __asm__ volatile(
        "push %rbp\n"
        "mov %rsp,%rbp\n"
        "sub $528,%rsp\n"
        "fxsave64 (%rsp)\n"
        "fninit\n"
        "movl $0x1f80,512(%rsp)\n"
        "ldmxcsr 512(%rsp)\n"
        "call nexis_dml_guard_body\n"
        "fxrstor64 (%rsp)\n"
        "add $528,%rsp\n"
        "pop %rbp\n"
        "ret\n");
}
