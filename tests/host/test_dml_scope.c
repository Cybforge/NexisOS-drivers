#include "../../tools/gpu-driver/amd/dml/nexis_dml_port.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {unsigned called,inner_line;enum nexis_dml_scope_result inner;uint32_t mxcsr;bool fail;} context;
typedef struct {unsigned char bytes[512];} __attribute__((aligned(16))) fx;
__attribute__((used,noinline)) static void NEXIS_GPU_CALL calculate(void *p){
    context *c=p;c->called++;__asm__ volatile("stmxcsr %0":"=m"(c->mxcsr));
    unsigned line=99;c->inner=nexis_dml_scope(calculate,c,&line);c->inner_line=line;
    __asm__ volatile("pxor %%xmm0,%%xmm0\npxor %%xmm8,%%xmm8\npxor %%xmm15,%%xmm15\nfld1\nfldpi":::"xmm0","xmm8","xmm15","memory");
    ASSERT(!c->fail);
}
/* One asm block captures both sides so compiler vectorized copies cannot
 * change a live XMM register between the saved snapshots and the scope call. */
__attribute__((naked)) static enum nexis_dml_scope_result NEXIS_GPU_CALL invoke(context *c __attribute__((unused)),unsigned *line __attribute__((unused)),fx *before __attribute__((unused)),fx *after __attribute__((unused))){
    __asm__ volatile("push %rbx\npush %r12\npush %r13\nmov %rdx,%r12\nmov %rcx,%r13\n"
        "mov %rsi,%rdx\nmov %rdi,%rsi\nlea calculate(%rip),%rdi\n"
        "fxsave64 (%r12)\ncall nexis_dml_scope\nfxsave64 (%r13)\n"
        "pop %r13\npop %r12\npop %rbx\nret\n");
}
int main(void){
    /* C call exercises both SysV->MS bridges. Exact assembly capture below
     * uses the correct explicit SysV argument registers via a separate block. */
    context c={0};unsigned line=0;CHECK(nexis_dml_scope(calculate,&c,&line)==NEXIS_DML_SCOPE_OK && c.called==1 && c.mxcsr==0x1f80 && c.inner==NEXIS_DML_SCOPE_BUSY && !c.inner_line);
    c.fail=true;CHECK(nexis_dml_scope(calculate,&c,&line)==NEXIS_DML_SCOPE_ASSERT && line && c.called==2);
    c.fail=false;CHECK(nexis_dml_scope(calculate,&c,&line)==NEXIS_DML_SCOPE_OK && !line && c.called==3);
    CHECK(nexis_dml_scope(NULL,&c,&line)==NEXIS_DML_SCOPE_INPUT && !line);
    uint32_t original;__asm__ volatile("stmxcsr %0":"=m"(original));
    for(unsigned mode=0;mode<4;mode++)for(unsigned fail=0;fail<2;fail++){
        fx before={{0}},after={{0}};uint32_t control=0x9f80|(mode<<13)|1;
        __asm__ volatile("ldmxcsr %0\npcmpeqb %%xmm0,%%xmm0\npcmpeqb %%xmm8,%%xmm8\npcmpeqb %%xmm15,%%xmm15"::"m"(control):"xmm0","xmm8","xmm15","memory");
        c.fail=fail;enum nexis_dml_scope_result expected=fail?NEXIS_DML_SCOPE_ASSERT:NEXIS_DML_SCOPE_OK;
        CHECK(invoke(&c,&line,&before,&after)==expected && c.mxcsr==0x1f80 && c.inner==NEXIS_DML_SCOPE_BUSY);
        CHECK(!memcmp(&before,&after,sizeof(before)));
    }
    __asm__ volatile("ldmxcsr %0"::"m"(original):"memory");
    puts("{\"passed\":true,\"scoped_assert_unwind\":true,\"nested_scope_rejected\":true,\"x87_xmm_mxcsr_exactly_restored\":true}");return 0;
}
