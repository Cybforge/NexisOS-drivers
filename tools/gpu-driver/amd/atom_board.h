#ifndef NEXIS_AMD_ATOM_BOARD_H
#define NEXIS_AMD_ATOM_BOARD_H
#include "atom_tables.h"
#define ATOM_BOARD_MAX_PATHS 16u
#define ATOM_BOARD_HDMI 0x0cu
#define ATOM_BOARD_DP 0x13u
#define ATOM_BOARD_HDMI6G 0x04u
enum atom_board_error {ATOM_BOARD_OK,ATOM_BOARD_INPUT,ATOM_BOARD_TABLE,ATOM_BOARD_VERSION,ATOM_BOARD_AMBIGUOUS};
typedef struct {uint32_t index;uint8_t shift,mask_shift,id;} atom_board_gpio;
typedef struct {
    uint16_t connector,encoder,external_encoder,device_tag;
    uint32_t encoder_caps,connector_caps;
    uint8_t kind,phy,ddc_line,ddc_engine,ddc_slave,hpd_state;
    bool internal_phy,has_ddc,ddc_hardware,has_hpd,has_encoder_caps;
    atom_board_gpio ddc_gpio,hpd_gpio;
} atom_board_path;
typedef struct {
    uint32_t reference_khz,i2c_reference_khz,phy_reference_khz,boot_display_khz;
    uint16_t devices;uint8_t pipes,phys,plls,aux,count;
    atom_board_path paths[ATOM_BOARD_MAX_PATHS];
} atom_board;
/* Bounds-checked board wiring from ATOM display_object_info 1.4/1.5,
 * GPIO_PIN_LUT 2.1 and DCE_INFO 4.1..4.5. Registers are firmware dword
 * indices, not bytes. Unsupported paths are represented, never substituted
 * with another port. This routine performs no hardware access. */
enum atom_board_error atom_board_open(const atom_rom *,atom_board *);
/* Board-declared internal UNIPHY object -> physical combo PHY, not DIG FE. */
bool atom_board_phy(uint16_t encoder,unsigned *phy);
#endif
