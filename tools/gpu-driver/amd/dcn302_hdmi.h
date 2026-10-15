#ifndef NEXIS_DCN302_HDMI_H
#define NEXIS_DCN302_HDMI_H
#include "dcn302_otg.h"
#include "dcn302_hdmi_regs.h"
enum dcn302_hdmi_error {DCN302_HDMI_OK,DCN302_HDMI_INPUT,DCN302_HDMI_IO,DCN302_HDMI_BUSY,DCN302_HDMI_CLOCK,DCN302_HDMI_READBACK,DCN302_HDMI_ROLLBACK};
typedef struct {
    bool valid,prepared,committed,audio,mode_changed,clock_changed;unsigned instance,link;   /* mode_changed: DIG_MODE was DVI (2) and is HDMI (3) now; clock_changed: the AFMT audio clock was off and this transaction switched it on */
    uint32_t registers[DCN302_HDMI_REGISTER_COUNT];
} dcn302_hdmi_transaction;
/* Native DCN302 HDMI RGB8 + stereo PCM48 audio packet engine.
 * The parent must already have disabled the correct transmitter, routed its
 * front end, configured the PHY/pixel clock and (above
 * 340 MHz) read back SCDC TMDS_CONFIG=3 on that connector. No PLL, I2C pin,
 * scanout or HDA codec state is guessed here. audio_source is a verified
 * AZALIA endpoint, not the stream/pipe/connector number. */
/* instance identifies the stream encoder/AFMT; link identifies the independently
 * routed physical DIG back end. Their numbers need not be equal. */
enum dcn302_hdmi_error dcn302_hdmi_prepare(const dcn302_io *,unsigned instance,unsigned link,
    uint32_t actual_pixel_khz,uint8_t scdc_config,bool audio,unsigned audio_source,dcn302_hdmi_transaction *);
/* Parent enables the transmitter and validates sink lock before committing.
 * Commit removes AVMUTE and starts samples only when the native link is on. */
enum dcn302_hdmi_error dcn302_hdmi_commit(const dcn302_io *,dcn302_hdmi_transaction *);
/* Disable transmitter first; rollback restores original packet configuration. */
enum dcn302_hdmi_error dcn302_hdmi_restore(const dcn302_io *,dcn302_hdmi_transaction *);
/* Back-end enable bit only (see dcn302_hdmi.c): lets the parent make the link quiet for dcn302_hdmi_prepare() without the VBIOS
 * transmitter command. saved receives the register as it was; restore writes the enable bit back to that state. */
enum dcn302_hdmi_error dcn302_hdmi_link_disable(const dcn302_io *,unsigned link,uint32_t *saved);
enum dcn302_hdmi_error dcn302_hdmi_link_restore(const dcn302_io *,unsigned link,uint32_t saved);
#endif
