#ifndef NEXIS_GPU_PCI_RESOURCE_H
#define NEXIS_GPU_PCI_RESOURCE_H
#include "../../../tools/gpu-driver/include/nexis_gpu_v2.h"
/* Decode a type-0 display function's BARs without probing/writing PCI config.
 * Firmware size is independently bounded and must match the live BAR base.
 * This returns metadata only, not access permission or a virtual mapping. */
bool gpu_pci_resource_decode(const uint32_t raw[6],const uint64_t base[6],const uint64_t bytes[6],unsigned bar,nexis_gpu_resource *);
/* The UEFI handoff stores the PCI I/O protocol's resources by its own BAR index. EDK2 numbers the BAR *entries* of the
 * header: a 64-bit BAR (two config registers) and an I/O BAR take one entry each. So on an RX 6600 (BAR0 64-bit VRAM, BAR2
 * 64-bit doorbell, BAR4 I/O ports, BAR5 registers) the registers are entry 3, not 5. This turns those entries into arrays
 * indexed by the config register number, which gpu_pci_resource_decode expects. A firmware that indexes by register number
 * instead is still accepted: per BAR the entry whose base equals the live BAR address is used. Reads nothing. */
void gpu_pci_firmware_ranges(const uint32_t raw[6],const uint64_t entry_base[6],const uint64_t entry_bytes[6],uint64_t base[6],uint64_t bytes[6]);
#endif
