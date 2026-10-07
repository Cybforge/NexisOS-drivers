#ifndef NEXIS_EDID_H
#define NEXIS_EDID_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define NEXIS_EDID_MAX_BYTES 2048
#define NEXIS_EDID_MAX_MODES 128
enum { EDID_HPOS=1,EDID_VPOS=2,EDID_INTERLACE=4,EDID_DOUBLE_CLOCK=8,EDID_PREFERRED=16,EDID_Y420_ONLY=32 };
typedef struct {
    uint32_t clock_khz;
    uint32_t hactive,hsync_start,hsync_end,htotal;
    uint32_t vactive,vsync_start,vsync_end,vtotal;
    uint32_t flags;
} edid_timing;
typedef struct {
    bool valid,digital,hdmi,scdc,stereo_48k16,incomplete;
    uint16_t product_id;
    char manufacturer[4],name[14];
    uint32_t max_tmds_khz,count;
    edid_timing modes[NEXIS_EDID_MAX_MODES];
} edid_monitor;
/* Limits are supplied by the real connector/driver, not inferred from PCI IDs.
 * This RGB8 policy does not imply support for DSC, FRL or YCbCr420. */
typedef struct {
    uint32_t max_pixel_khz,max_tmds_khz;
    bool hdmi,scdc;
} edid_link_limits;
bool edid_parse(const uint8_t *,size_t,edid_monitor *);
uint32_t edid_refresh_millihz(const edid_timing *);
const edid_timing *edid_select_rgb8(const edid_monitor *,uint32_t width,uint32_t height,const edid_link_limits *);
#endif
