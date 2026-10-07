#ifndef NEXIS_DCN302_OTG_H
#define NEXIS_DCN302_OTG_H
#include "../include/nexis_gpu_v2.h"
#include "dcn302_regs.h"
typedef struct {
    void *context;
    bool (*read)(void *,uint32_t byte_offset,uint32_t *);
    bool (*write)(void *,uint32_t byte_offset,uint32_t);
    bool (*delay_us)(void *,uint32_t);
} dcn302_io;
typedef struct {
    uint32_t vstartup,vready,vupdate_offset,vupdate_width;
    bool display_port;
} dcn302_sync;
enum dcn302_error { DCN302_OK,DCN302_INPUT,DCN302_IO,DCN302_BUSY,DCN302_UNSUPPORTED,DCN302_TIMEOUT,DCN302_READBACK,DCN302_ROLLBACK };
typedef struct { bool valid;unsigned pipe;uint32_t registers[DCN302_REGISTER_COUNT]; } dcn302_snapshot;
bool dcn302_otg_snapshot(const dcn302_io *,unsigned pipe,dcn302_snapshot *);
/* Read hardware geometry, not a requested pixel clock. pixel_khz is zero until
 * the parent driver obtains the physical clock from the clock source. */
bool dcn302_otg_read_shape(const dcn302_io *,unsigned pipe,nexis_gpu_timing *,bool *active);
bool dcn302_otg_frame_count(const dcn302_io *,unsigned pipe,uint32_t *);
/* Retain the configured OPP source and clocks during handoff. These control
 * actual OTG/VTG scanout, with bounded state/frame-counter readback. The
 * parent owns restoring clocks, PHY, memory bandwidth and prior mode. */
enum dcn302_error dcn302_otg_disable(const dcn302_io *,unsigned pipe);
enum dcn302_error dcn302_otg_enable(const dcn302_io *,unsigned pipe);
/* Native register transaction for one disabled progressive RGB pipe.
 * The parent backend must disable the pipeline, calculate DML global sync,
 * program clocks, establish HDMI/DP and restore those on failure. This is not
 * a complete modeset or a firmware-framebuffer alias. */
enum dcn302_error dcn302_otg_program_disabled(const dcn302_io *,unsigned pipe,const nexis_gpu_timing *,const dcn302_sync *);
enum dcn302_error dcn302_otg_restore_disabled(const dcn302_io *,const dcn302_snapshot *);
#endif
