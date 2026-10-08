#ifndef NEXIS_GPU_PACKAGE_VERIFY_H
#define NEXIS_GPU_PACKAGE_VERIFY_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* Signed driver package envelope ("NDPK"), see scripts/gpu_package.py.
 * The kernel trusts exactly one ECDSA P-256 public key (generated pins.h).
 * A package is accepted only if (a) the signature over header+module is
 * valid, (b) the embedded name equals the catalog name, (c) its version is
 * not below the catalog floor. The module bytes inside are then handed to the
 * NDRV parser, which performs its own structural checks. */
typedef struct {
    const uint8_t *module;   /* points into the caller's buffer */
    size_t module_bytes;
    uint32_t version;
} gpu_package_view;
bool gpu_package_open(const uint8_t *file, size_t size, const char *name, uint32_t min_version, gpu_package_view *out);
#endif
