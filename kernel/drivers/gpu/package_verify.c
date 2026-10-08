#include "package_verify.h"
#include "pins.h"
#include "../../include/string.h"
#include "../../../third_party/bearssl/inc/bearssl.h"
/* Envelope layout is documented in scripts/gpu_package.py (all little endian):
 *   0 "NDPK" | 4 format=1 | 8 module_bytes | 12 version | 16 name[16] | 32 module | +64 signature */
#define HEADER 32u
#define SIGNATURE 64u
#define MAX_MODULE (64u + 1024u * 1024u)
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
bool gpu_package_open(const uint8_t *file, size_t size, const char *name, uint32_t min_version, gpu_package_view *out) {
    if (out) { out->module = NULL; out->module_bytes = 0; out->version = 0; }
    if (!out || !file || !name || size <= HEADER + SIGNATURE || size > (size_t)HEADER + MAX_MODULE + SIGNATURE) return false;
    if (memcmp(file, "NDPK", 4) || u32(file + 4) != 1) return false;
    uint32_t module_bytes = u32(file + 8), version = u32(file + 12);
    if (module_bytes != size - HEADER - SIGNATURE || !module_bytes || version < min_version) return false;
    /* Name field: ASCII, NUL padded, equal to the catalog id. */
    size_t length = strlen(name);
    if (!length || length > 15 || memcmp(file + 16, name, length)) return false;
    for (size_t n = length; n < 16; n++) if (file[16 + n]) return false;
    uint8_t digest[32];
    br_sha256_context context;
    br_sha256_init(&context);
    br_sha256_update(&context, file, HEADER + module_bytes);
    br_sha256_out(&context, digest);
    br_ec_public_key key = { BR_EC_secp256r1, (unsigned char *)(uintptr_t)gpu_signing_key, sizeof(gpu_signing_key) };
    if (br_ecdsa_i31_vrfy_raw(&br_ec_prime_i31, digest, sizeof(digest), &key, file + HEADER + module_bytes, SIGNATURE) != 1) return false;
    out->module = file + HEADER;
    out->module_bytes = module_bytes;
    out->version = version;
    return true;
}
