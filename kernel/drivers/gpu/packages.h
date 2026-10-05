#ifndef NEXIS_GPU_PACKAGES_H
#define NEXIS_GPU_PACKAGES_H
#include "../../include/types.h"
/* Hidden, device-matched driver job. Binary code is never in the ISO. */
void gpu_packages_poll(void);
void gpu_prepare_install(void);
void *gpu_install_payload(size_t *size);
bool gpu_driver_active(void);
#endif
