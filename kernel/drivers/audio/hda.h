#ifndef NEXIS_HDA_H
#define NEXIS_HDA_H

#include "../../include/types.h"

#define HDA_VENDOR_INTEL 0x8086
#define HDA_DEV_ICH6     0x2668
#define HDA_DEV_ICH7     0x27D8
#define HDA_DEV_ICH8     0x284B
#define HDA_DEV_ICH9     0x293E
#define HDA_DEV_QEMU     0x2668

bool hda_init(void);
void hda_set_volume(uint8_t vol_percent);
bool hda_play_pcm(const int16_t *samples, size_t count);
const char *hda_get_name(void);
bool hda_output_ready(void);
bool hda_is_hdmi(void);
bool hda_rescan_hdmi(void);

#endif /* NEXIS_HDA_H */
