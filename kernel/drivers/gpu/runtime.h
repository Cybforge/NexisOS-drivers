#ifndef NEXIS_GPU_RUNTIME_H
#define NEXIS_GPU_RUNTIME_H
#include "../pci.h"
/* Only a signature-verified package for the GOP-associated physical PCI device may
 * enter this API. No unimplemented card is placed in the download catalog. */
/* data = verified NDRV v2 module (signature already checked by packages.c). */
bool gpu_runtime_load(const uint8_t *,size_t,pci_device_t *);
/* Restore the firmware display mode and unload the module (trial rollback). */
bool gpu_runtime_revert(void);
void gpu_runtime_poll(void);
bool gpu_runtime_active(void);
/* The running native mode sends HDMI audio packets (the module's audio endpoint is on). */
bool gpu_runtime_audio(void);
#endif
