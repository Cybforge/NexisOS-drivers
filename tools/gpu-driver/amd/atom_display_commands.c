/* Original MIT-licensed ATOM display-command adapter, layout reference:
 * AMD Linux v6.12 atomfirmware.h and command_table2.c. */
#include "atom_display_commands.h"
#include <string.h>
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(i*8));}
static bool clock_valid(uint32_t khz){return khz>=25000 && khz<=600000;}
static bool path_valid(const atom_board_path *p,uint32_t khz){
    unsigned phy=0;
    return p && p->kind==ATOM_BOARD_HDMI && (p->connector>>12)==3 && (p->connector&255)==p->kind &&
        p->internal_phy && atom_board_phy(p->encoder,&phy) && phy==p->phy && !p->external_encoder && p->phy<6 &&
        clock_valid(khz) && (khz<=340000 || (p->has_encoder_caps && (p->encoder_caps&ATOM_BOARD_HDMI6G)));
}
bool atom_hdmi_pixel_parameters(uint8_t out[16],const atom_board_path *p,unsigned otg,uint32_t khz){
    if(!out || !path_valid(p,khz) || otg>=6)return false;
    /* PixelClock1.7 uses 100-Hz units and board-selected COMBOPHY_PLL20..25.
     * FORCE_PROGRAM applies only to this board's chosen physical PLL. */
    return atom_pixel_clock_v7(out,khz,(uint8_t)otg,(uint8_t)(20+p->phy),(uint8_t)(p->encoder&255),3,1);
}
bool atom_hdmi_stream_parameters(uint8_t out[12],unsigned stream,uint32_t khz){
    if(!out || stream>=6 || !clock_valid(khz))return false;
    memset(out,0,12);out[0]=(uint8_t)stream;out[1]=15;out[2]=3;out[3]=4;
    put32(out+4,khz/10);out[8]=2; /* PANEL_8BIT_PER_COLOR is 2, not 8. */
    return true;
}
bool atom_hdmi_transmitter_parameters(uint8_t out[32],const atom_board_path *p,unsigned stream,unsigned hpd,uint32_t khz,unsigned action){
    if(!out || !path_valid(p,khz) || stream>=6 || hpd>=6 || !p->has_hpd)return false;
    switch(action){case 0:case 1:case 7:case 8:case 9:case 10:case 12:case 13:break;default:return false;}
    memset(out,0,32);out[0]=p->phy;out[1]=(uint8_t)action;out[2]=3;out[3]=4;
    put32(out+4,khz/10);out[8]=(uint8_t)(hpd+1);out[9]=(uint8_t)(1u<<stream);out[10]=p->kind;
    return true;
}
bool atom_hdmi_transmitter_v7_parameters(uint8_t out[60],const atom_board_path *p,unsigned stream,unsigned hpd,uint32_t khz,unsigned action){
    uint8_t legacy[32];if(!out || !atom_hdmi_transmitter_parameters(legacy,p,stream,hpd,khz,action))return false;
    memset(out,0,60);memcpy(out,legacy,11);return true;
}
enum atom_vm_error atom_display_execute(atom_vm *vm,enum atom_display_command cmd,uint8_t *parameters,size_t size){
    if(!vm || !parameters)return ATOM_VM_INPUT;
    atom_table t;if(!atom_rom_table(&vm->rom,true,(unsigned)cmd,&t))return ATOM_VM_TABLE;
    unsigned revision;size_t allocation;
    switch(cmd){
        case ATOM_DISPLAY_ENCODER:revision=5;allocation=12;break;
        case ATOM_DISPLAY_PIXEL_CLOCK:revision=7;allocation=16;break;
        case ATOM_DISPLAY_TRANSMITTER:
            if(t.revision!=6 && t.revision!=7)return ATOM_VM_UNSUPPORTED;
            revision=t.revision;allocation=t.revision==6?32:60;break;
        default:return ATOM_VM_UNSUPPORTED;
    }
    if(t.format!=1 || t.revision!=revision)return ATOM_VM_UNSUPPORTED;
    if(size!=allocation || t.parameters>allocation)return ATOM_VM_INPUT;
    /* Firmware interprets a dword parameter space. A caller's byte buffer
     * need not be aligned; avoid casting it to a uint32_t pointer. */
    uint32_t words[16]={0};memcpy(words,parameters,size);
    if(!atom_vm_execute(vm,(unsigned)cmd,words,(unsigned)(size/4)))return vm->error==ATOM_VM_OK?ATOM_VM_INPUT:vm->error;
    memcpy(parameters,words,size);return ATOM_VM_OK;
}
