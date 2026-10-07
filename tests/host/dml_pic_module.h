#ifndef NEXIS_DML_PIC_TEST_H
#define NEXIS_DML_PIC_TEST_H
#include "../../tools/gpu-driver/amd/dcn302_dml.h"
/* Pure math harness only. This is not nexis_gpu_entry_v2 and the kernel must
 * never load it as a driver. It exposes no mode/readback/audio callbacks. */
typedef struct {
    uint32_t bytes,reserved;
    dcn302_dml_job *job;
    enum nexis_dml_scope_result (NEXIS_GPU_CALL *calculate)(dcn302_dml_job *,unsigned *);
} dml_pic_export;
typedef int (NEXIS_GPU_CALL *dml_pic_entry)(dml_pic_export *);
#endif
