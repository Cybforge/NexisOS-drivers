#ifndef NEXIS_HDMI_SCDC_H
#define NEXIS_HDMI_SCDC_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    void *context;
    bool (*read_byte)(void *,uint8_t address,uint8_t offset,uint8_t *);
    bool (*write_byte)(void *,uint8_t address,uint8_t offset,uint8_t);
    bool (*delay_us)(void *,uint32_t);
} hdmi_scdc_io;
typedef struct {bool valid;uint8_t source_version,tmds_config;} hdmi_scdc_snapshot;
enum hdmi_scdc_error {HDMI_SCDC_OK,HDMI_SCDC_INPUT,HDMI_SCDC_IO,HDMI_SCDC_VERSION,HDMI_SCDC_READBACK,HDMI_SCDC_TIMEOUT,HDMI_SCDC_ROLLBACK};
/* SCDC is a real monitor I2C register interface (7-bit address 0x54).
 * Above 340 MHz, configure scrambling and 1:40 clock ratio together.
 * Parent owns the physical PHY/pixel clock and output mute/rollback. */
enum hdmi_scdc_error hdmi_scdc_configure(const hdmi_scdc_io *,uint32_t tmds_khz,bool low_rate_scrambling,hdmi_scdc_snapshot *);
enum hdmi_scdc_error hdmi_scdc_restore(const hdmi_scdc_io *,const hdmi_scdc_snapshot *);
/* Call only after the parent has enabled the corresponding HDMI transmitter.
 * Scrambler and the clock/channel locks must agree with the selected mode. */
enum hdmi_scdc_error hdmi_scdc_verify_link(const hdmi_scdc_io *,uint32_t tmds_khz,bool low_rate_scrambling);
#endif
