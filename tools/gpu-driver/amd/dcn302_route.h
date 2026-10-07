#ifndef NEXIS_DCN302_ROUTE_H
#define NEXIS_DCN302_ROUTE_H
#include "atom_board.h"
#include "dcn302_hdmi.h"
#include "dcn302_route_regs.h"
enum dcn302_route_error {DCN302_ROUTE_OK,DCN302_ROUTE_INPUT,DCN302_ROUTE_IO,DCN302_ROUTE_ABSENT,DCN302_ROUTE_UNSUPPORTED,DCN302_ROUTE_AMBIGUOUS,DCN302_ROUTE_CHANGED};
typedef struct {
    unsigned path,link,stream,otg,opp,ddc,hpd;
    uint32_t max_tmds_khz;
    nexis_gpu_timing shape; /* Clock is zero until measured from native OTG. */
} dcn302_route;
/* Read-only proof of an existing, single progressive RGB8 HDMI pipeline.
 * Every identity comes from board wiring plus native registers. Multi-monitor,
 * MST, external converters, cloned FEs, split ODM and unknown wiring are
 * rejected explicitly. The parent must additionally validate the HUBP/GOP
 * surface and measure the clock before native takeover/modeset. The caller
 * serializes display operations; repeated reads detect changes, not a global
 * hardware snapshot atomic against unrelated firmware/driver writes. */
enum dcn302_route_error dcn302_route_find(const dcn302_io *,const atom_board *,unsigned width,unsigned height,dcn302_route *);
/* Cheap native HPD check for poll; no interrupt acknowledgement or GPIO writes. */
enum dcn302_route_error dcn302_route_connected(const dcn302_io *,const atom_board_path *,unsigned hpd,bool *connected);
#endif
