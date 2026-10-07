#include "dml_pic_module.h"
static dcn302_dml_workspace workspace;
static dcn302_dml_job job;
int NEXIS_GPU_CALL driver_init_v2(dml_pic_export *e){
    if(!e)return -1;
    job.workspace=&workspace;e->bytes=sizeof(*e);e->reserved=0;e->job=&job;
    e->calculate=dcn302_dml_calculate;return 0;
}
