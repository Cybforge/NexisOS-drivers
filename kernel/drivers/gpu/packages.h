#ifndef NEXIS_GPU_PACKAGES_H
#define NEXIS_GPU_PACKAGES_H
#include "../../include/types.h"
#include "../keyboard.h"
/* Hidden, device-matched driver job. Binary code is never in the ISO.
 * See packages.c for the complete flow and the safety nets. */
void gpu_packages_poll(void);
void gpu_prepare_install(void);
/* Verified signed package for the running adapter (caller kfree()s it) and
 * its catalog name (<=15 chars + NUL); NULL when no driver was activated. */
void *gpu_install_payload(size_t *size, char name[16]);
bool gpu_driver_active(void);
/* Enter keeps / Esc reverts the display mode during the trial period.
 * Returns true if the key was consumed. */
bool gpu_trial_key(const key_event_t *ev);
#endif
