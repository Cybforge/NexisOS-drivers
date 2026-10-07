#ifndef NEXIS_GPU_RUNTIME_H
#define NEXIS_GPU_RUNTIME_H
#include "../pci.h"
/* Only a hash-pinned package for the GOP-associated physical PCI device may
 * enter this API. No unimplemented card is placed in the download catalog. */
bool gpu_runtime_load(const uint8_t *,size_t,const uint8_t sha256[32],pci_device_t *);
void gpu_runtime_poll(void);
bool gpu_runtime_active(void);
#endif
