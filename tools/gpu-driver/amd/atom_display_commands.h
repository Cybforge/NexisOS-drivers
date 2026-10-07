#ifndef NEXIS_AMD_ATOM_DISPLAY_COMMANDS_H
#define NEXIS_AMD_ATOM_DISPLAY_COMMANDS_H
#include "atom_board.h"
#include "atom_vm.h"
enum atom_display_command {ATOM_DISPLAY_ENCODER=4,ATOM_DISPLAY_PIXEL_CLOCK=12,ATOM_DISPLAY_TRANSMITTER=76};
/* Explicit RGB8/HDMI command layouts. Do not use these for DP link training. */
bool atom_hdmi_pixel_parameters(uint8_t out[16],const atom_board_path *,unsigned otg,uint32_t pixel_khz);
bool atom_hdmi_stream_parameters(uint8_t out[12],unsigned stream,uint32_t pixel_khz);
bool atom_hdmi_transmitter_parameters(uint8_t out[32],const atom_board_path *,unsigned stream,unsigned hpd,uint32_t pixel_khz,unsigned action);
/* v1.7 carries 60 bytes including HPO/reserved fields; legacy HDMI TMDS
 * (mode3) leaves HPO unused. This is not FRL/HPO initialization. */
bool atom_hdmi_transmitter_v7_parameters(uint8_t out[60],const atom_board_path *,unsigned stream,unsigned hpd,uint32_t pixel_khz,unsigned action);
/* Execute only a version-compatible, preflighted board command. The caller
 * owns PHY/link sequencing and complete hardware rollback after any error. */
enum atom_vm_error atom_display_execute(atom_vm *,enum atom_display_command,uint8_t *,size_t);
#endif
