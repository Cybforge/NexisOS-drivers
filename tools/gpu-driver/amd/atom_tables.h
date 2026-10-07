#ifndef NEXIS_AMD_ATOM_TABLES_H
#define NEXIS_AMD_ATOM_TABLES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Read-only views of the board's own firmware. No x86 option-ROM execution. */
typedef struct { const uint8_t *image;size_t size;uint16_t commands,data; } atom_rom;
typedef struct { const uint8_t *bytes;uint16_t size;uint8_t format,revision,workspace,parameters; } atom_table;
bool atom_rom_open(const uint8_t *,size_t,uint16_t vendor,uint16_t device,atom_rom *);
bool atom_rom_table(const atom_rom *,bool command,unsigned index,atom_table *);
/* ATOM SetPixelClock v1.7 accepts 100-Hz units, not old 10-kHz units. */
bool atom_pixel_clock_v7(uint8_t output[16],uint32_t pixel_khz,uint8_t crtc,uint8_t pll,uint8_t encoder,uint8_t mode,uint8_t flags);
#endif
