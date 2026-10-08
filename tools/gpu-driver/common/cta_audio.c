#include "cta_audio.h"

static bool block_ok(const uint8_t *b) {
    unsigned sum = 0;
    for (unsigned i = 0; i < 128; i++) sum += b[i];
    return (sum & 255) == 0;
}

bool nx_audio_caps_stereo48(const nx_audio_caps *c) {
    return c && c->valid && c->lpcm_channels >= 2 && (c->lpcm_rates & 4) && (c->lpcm_sizes & 1);
}

bool nx_audio_caps_from_edid(const uint8_t *edid, size_t bytes, nx_audio_caps *out) {
    if (!out) return false;
    *out = (nx_audio_caps){0};
    out->speakers = 1;
    static const uint8_t header[8] = { 0, 255, 255, 255, 255, 255, 255, 0 };
    if (!edid || bytes < 128) return false;
    for (unsigned i = 0; i < 8; i++) if (edid[i] != header[i]) return false;
    if (!block_ok(edid)) return false;
    out->manufacturer = (uint16_t)(edid[8] << 8 | edid[9]);
    out->product = (uint16_t)(edid[10] | edid[11] << 8);
    /* Monitor name: display descriptor tag 0xfc in one of the four 18-byte descriptors. */
    for (unsigned d = 54; d + 18 <= 126; d += 18) {
        if (edid[d] || edid[d + 1] || edid[d + 2] != 0 || edid[d + 3] != 0xfc) continue;
        unsigned n = 0;
        for (unsigned i = 5; i < 18 && n < sizeof(out->name) - 1; i++) {
            uint8_t c = edid[d + i];
            if (c == 0x0a || c == 0) break;
            out->name[n++] = (c >= 32 && c < 127) ? (char)c : '?';
        }
        out->name[n] = 0;
        out->name_length = (uint8_t)n;
        break;
    }
    unsigned extensions = edid[126];
    for (unsigned e = 1; e <= extensions && (size_t)(e + 1) * 128 <= bytes; e++) {
        const uint8_t *x = edid + (size_t)e * 128;
        if (x[0] != 0x02 || x[1] < 1 || !block_ok(x)) continue; /* CTA-861 extension, revision >= 1 */
        unsigned end = x[2];
        if (end < 4 || end > 127) continue;
        for (unsigned p = 4; p < end;) {
            unsigned tag = x[p] >> 5, length = x[p] & 31;
            if (p + 1 + length > end) break;
            const uint8_t *d = x + p + 1;
            if (tag == 1) { /* audio data block: 3-byte short audio descriptors */
                for (unsigned i = 0; i + 3 <= length; i += 3) {
                    if (((d[i] >> 3) & 15) != 1) continue; /* format code 1 = linear PCM */
                    unsigned channels = (d[i] & 7) + 1;
                    if (!out->valid || channels > out->lpcm_channels) {
                        out->lpcm_channels = (uint8_t)channels;
                        out->lpcm_rates = d[i + 1] & 0x7f;
                        out->lpcm_sizes = d[i + 2] & 7;
                    }
                    out->valid = true;
                }
            } else if (tag == 4 && length >= 3) { /* speaker allocation */
                out->speakers = d[0] ? d[0] : 1;
            }
            p += 1 + length;
        }
    }
    return out->valid;
}
