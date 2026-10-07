#ifndef NEXIS_DCN302_DDC_H
#define NEXIS_DCN302_DDC_H
#include "dcn302_otg.h"
#include "dcn302_ddc_regs.h"
enum dcn302_ddc_error {
    DCN302_DDC_OK,DCN302_DDC_INPUT,DCN302_DDC_IO,DCN302_DDC_BUSY,
    DCN302_DDC_PAD,DCN302_DDC_TIMEOUT,DCN302_DDC_NACK,DCN302_DDC_ABORTED,
    DCN302_DDC_READBACK,DCN302_DDC_RELEASE
};
typedef struct { uint8_t address;bool read;uint16_t bytes;uint8_t *data; } dcn302_ddc_payload;
typedef struct {
    dcn302_io io;unsigned bus;uint32_t crystal_khz;
    bool ready,busy,poisoned;enum dcn302_ddc_error error;
} dcn302_ddc;
/* Native Navi23 hardware I2C engine. Bus is zero-based DDC1..DDC5 from
 * the board's connector records. The caller serializes the shared I2C engine
 * and establishes HDMI/I2C GPIO routing first. AUX/software GPIO routing is
 * rejected, not silently reconfigured on a possibly different connector. */
bool dcn302_ddc_init(dcn302_ddc *,const dcn302_io *,unsigned bus,uint32_t crystal_khz);
/* Up to four repeated-start transactions; only the last may read.
 * 144-byte hardware FIFO, up to 128 returned bytes. Read output changes only
 * after a completed transfer and successful engine release. Slave writes
 * cannot be undone by the I2C controller; the HDMI transaction owns rollback. */
bool dcn302_ddc_transfer(dcn302_ddc *,const dcn302_ddc_payload *,unsigned count);
bool dcn302_ddc_read_byte(dcn302_ddc *,uint8_t address,uint8_t offset,uint8_t *);
bool dcn302_ddc_write_byte(dcn302_ddc *,uint8_t address,uint8_t offset,uint8_t);
#endif
