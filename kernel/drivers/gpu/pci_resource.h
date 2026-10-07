#ifndef NEXIS_GPU_PCI_RESOURCE_H
#define NEXIS_GPU_PCI_RESOURCE_H
#include "../../../tools/gpu-driver/include/nexis_gpu_v2.h"
/* Decode a type-0 display function's BARs without probing/writing PCI config.
 * Firmware size is independently bounded and must match the live BAR base.
 * This returns metadata only, not access permission or a virtual mapping. */
bool gpu_pci_resource_decode(const uint32_t raw[6],const uint64_t base[6],const uint64_t bytes[6],unsigned bar,nexis_gpu_resource *);
#endif
