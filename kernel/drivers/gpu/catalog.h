#ifndef NEXIS_GPU_CATALOG_H
#define NEXIS_GPU_CATALOG_H
#include <stdint.h>
/* One row per hidden driver package. The table lives in the generated pins.h
 * (see scripts/build_gpu_catalog.py); the driver binaries themselves are NOT
 * part of the OS image. A row says which PCI device a package serves and the
 * lowest signed package version this kernel will still accept. */
typedef struct {
    const char *name;            /* package id, also the file name <name>.ndpk and the cache name */
    uint16_t vendor;
    const uint16_t *devices;     /* PCI device ids served by this package */
    uint8_t device_count;
    uint32_t min_version;        /* anti-rollback floor for signed packages */
    uint8_t trial;               /* 1: ask for confirmation, auto-revert if not confirmed */
} gpu_catalog_entry;
#endif
