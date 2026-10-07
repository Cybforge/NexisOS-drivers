#ifndef NEXIS_DCN302_HDMI_H
#define NEXIS_DCN302_HDMI_H
#include "dcn302_otg.h"
#include "dcn302_hdmi_regs.h"
enum dcn302_hdmi_error {DCN302_HDMI_OK,DCN302_HDMI_INPUT,DCN302_HDMI_IO,DCN302_HDMI_BUSY,DCN302_HDMI_CLOCK,DCN302_HDMI_READBACK,DCN302_HDMI_ROLLBACK};
typedef struct {
    bool valid,prepared,committed,audio;unsigned instance,link;
    uint32_t registers[DCN302_HDMI_REGISTER_COUNT];
} dcn302_hdmi_transaction;
/* Native DCN302 HDMI RGB8 + stereo PCM48 audio packet engine.
 * The parent must already have disabled the correct transmitter, routed its
 * front end, enabled AFMT clocks, configured the PHY/pixel clock and (above
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
#endif
