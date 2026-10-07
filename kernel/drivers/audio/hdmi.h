#ifndef NEXIS_HDMI_CODEC_H
#define NEXIS_HDMI_CODEC_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/* Native HDA codec command transport; all accesses must receive a response. */
typedef struct {
 void *context;
 bool (*read)(void *,unsigned,unsigned,unsigned,uint32_t *);
 bool (*write)(void *,unsigned,unsigned,unsigned);
} hdmi_codec_io;
bool hdmi_codec_is_amd(uint32_t codec_id);
bool hdmi_codec_stereo_sink(const hdmi_codec_io *,unsigned pin,uint32_t codec_id);
bool hdmi_codec_program_stereo(const hdmi_codec_io *,unsigned pin,unsigned converter,uint32_t codec_id,uint32_t revision);
#endif
