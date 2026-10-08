#ifndef NEXIS_GPU_CTA_AUDIO_H
#define NEXIS_GPU_CTA_AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* HDMI audio capabilities of the connected monitor, extracted from its EDID (CTA-861 extension
 * blocks).  Only what a stereo-LPCM HDMI audio path needs: the monitor's linear-PCM short audio
 * descriptor, the speaker allocation and the identification the GPU forwards to the audio codec.
 * Freestanding, no allocation; shared by every display backend. */
typedef struct {
    bool valid;            /* CTA data blocks parsed and at least one LPCM short audio descriptor found */
    uint8_t lpcm_channels; /* maximum LPCM channels the monitor accepts (>= 2 for stereo) */
    uint8_t lpcm_rates;    /* CTA rate bits: 0 32k, 1 44.1k, 2 48k, 3 88.2k, 4 96k, 5 176.4k, 6 192k */
    uint8_t lpcm_sizes;    /* CTA bit-depth bits: 0 16-bit, 1 20-bit, 2 24-bit */
    uint8_t speakers;      /* CTA speaker allocation byte 0; 1 (front left/right) when the block is absent */
    uint16_t manufacturer; /* EDID bytes 8..9 as stored (big endian PNP id) */
    uint16_t product;      /* EDID bytes 10..11 (little endian) */
    uint8_t name_length;   /* monitor name from the EDID name descriptor, without terminator */
    char name[14];
} nx_audio_caps;
bool nx_audio_caps_from_edid(const uint8_t *edid, size_t bytes, nx_audio_caps *out);
/* True when 48 kHz / 16-bit stereo LPCM is accepted - the format this stack sends. */
bool nx_audio_caps_stereo48(const nx_audio_caps *caps);
#endif
