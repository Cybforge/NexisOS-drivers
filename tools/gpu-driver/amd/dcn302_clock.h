#ifndef NEXIS_DCN302_CLOCK_H
#define NEXIS_DCN302_CLOCK_H
#include "dcn302_otg.h"
typedef struct {
    uint32_t pixel_khz,refresh_millihz,uncertainty_ppm;
    uint32_t frames;uint64_t elapsed_us;
} dcn302_clock_measurement;
/* Measure running native hardware frame-counter transitions against the
 * calibrated kernel clock. No requested clock/EDID refresh enters the result.
 * Used once at takeover/modeset; never block the compositor's ordinary poll.
 * Parent supplies a monotonic microsecond clock and restores output if this
 * proof fails. This measures timing, not PHY lock or HDMI audio. */
enum dcn302_error dcn302_clock_measure(const dcn302_io *,unsigned pipe,
    uint64_t (*time_us)(void *),unsigned frames,dcn302_clock_measurement *);
#endif
