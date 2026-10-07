#ifndef NEXIS_DML_PORT_H
#define NEXIS_DML_PORT_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../../include/nexis_gpu_v2.h"
/* Private DML routines must run through the serialized FP/abort scope below.
 * A failed hardware-math assertion aborts calculation before any MMIO writes. */
_Noreturn void nexis_dml_assert_failure(unsigned line);
#define ASSERT(x) do{if(!(x))nexis_dml_assert_failure(__LINE__);}while(0)
#define noinline_for_stack __attribute__((noinline))
typedef void (NEXIS_GPU_CALL *nexis_dml_calculation)(void *);
enum nexis_dml_scope_result {NEXIS_DML_SCOPE_OK,NEXIS_DML_SCOPE_INPUT,NEXIS_DML_SCOPE_BUSY,NEXIS_DML_SCOPE_ASSERT};
/* x86-64 SSE2 baseline only, no AVX. Preserves x87/XMM0..15/MXCSR, masks FP
 * exceptions and calculates in nearest-even/default mode. Does not enable CPU
 * FPU features or alter CR0/CR4. Kernel already initializes x86-64 FX state.
 * Output is a calculation result, never a physical hardware test. */
enum nexis_dml_scope_result NEXIS_GPU_CALL nexis_dml_scope(nexis_dml_calculation,void *,unsigned *fault_line);
#endif
