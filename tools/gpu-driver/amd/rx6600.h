#ifndef NEXIS_RX6600_H
#define NEXIS_RX6600_H
#include "dcn302_surface.h"
#include "dcn302_clock.h"
enum rx6600_error {RX6600_OK,RX6600_INPUT,RX6600_RESOURCE,RX6600_ROM,RX6600_BOARD,RX6600_ROUTE,RX6600_SURFACE,RX6600_CLOCK,RX6600_CHANGED,RX6600_MODESET_PENDING};
typedef struct {
    const nexis_gpu_services *services;
    dcn302_io io;
    atom_board board;
    dcn302_route route;
    dcn302_surface surface;
    dcn302_clock_measurement clock;
    nexis_gpu_resource vram,registers;
    uint32_t fixed_rate[3]; /* V_TOTAL_CONTROL, V_TOTAL_MIN, V_TOTAL_MAX */
    uint64_t sampled_us;uint32_t sampled_frame;
    bool ready,busy;
    enum rx6600_error error;
} rx6600_state;
/* Native RX6600 retained backend under construction. Probe/read/poll use real
 * PCI/ATOM/DCN state and perform no GPU writes. A changed mode currently fails
 * explicitly until the clock/PHY/bandwidth transaction is implemented. This
 * is not a completed card driver and must not enter the download catalog yet. */
enum rx6600_error rx6600_probe(rx6600_state *,const nexis_gpu_services *);
bool rx6600_read_mode(rx6600_state *,nexis_gpu_scanout *);
bool rx6600_set_mode(rx6600_state *,const nexis_gpu_timing *);
void rx6600_poll(rx6600_state *);
void rx6600_shutdown(rx6600_state *);
#endif
