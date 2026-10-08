#ifndef NEXIS_DCN302_PIXEL_RESYNC_H
#define NEXIS_DCN302_PIXEL_RESYNC_H
#include "dcn302_otg.h"
#include "dcn302_pixel_resync_regs.h"

enum dcn302_hdmi_color_depth {
    DCN302_HDMI_RGB8 = 8,
    DCN302_HDMI_RGB10 = 10,
    DCN302_HDMI_RGB12 = 12,
    DCN302_HDMI_RGB16 = 16,
};

typedef struct {
    dcn302_io owner;
    bool (*guard)(void *); /* Parent proves stopped OTG/VTGs and retained state. */
    uint32_t before, after;
    unsigned phy;
    bool prepared, dirty, applied, poisoned, touched;
    enum dcn302_error error;
} dcn302_pixel_resync_transaction;

/* Native DCN302 HDMI pixel-resync ownership.  Only DCCG deep-color is wired
 * to the Navi23 clock-source mask. YCbCr420's raw double-rate control remains
 * deliberately unsupported here. PIXCLK_RESYNC_ENABLE, PIXCLK_ENABLE, and
 * DOUBLE_RATE stay under the native firmware/clock path. `prepare` can observe a live
 * old mode; all writes and rollback require the parent to prove every pipe
 * stopped.  The component does not claim PLL lock, sink lock, or activation. */
enum dcn302_error dcn302_pixel_resync_prepare(const dcn302_io *, unsigned phy,
    enum dcn302_hdmi_color_depth, bool ycbcr420, bool (*guard)(void *),
    dcn302_pixel_resync_transaction *);
enum dcn302_error dcn302_pixel_resync_apply_disabled(const dcn302_io *,
    dcn302_pixel_resync_transaction *);
enum dcn302_error dcn302_pixel_resync_restore_disabled(const dcn302_io *,
    dcn302_pixel_resync_transaction *);
enum dcn302_error dcn302_pixel_resync_verify_installed(const dcn302_io *,
    const dcn302_pixel_resync_transaction *);
#endif
