#ifndef NEXIS_ATOM_MEMORY_H
#define NEXIS_ATOM_MEMORY_H
#include "atom_tables.h"
enum atom_memory_error {ATOM_MEMORY_OK,ATOM_MEMORY_INPUT,ATOM_MEMORY_TABLE,ATOM_MEMORY_VERSION,ATOM_MEMORY_GEOMETRY,ATOM_MEMORY_AMBIGUOUS};
typedef struct {
    uint32_t memory_mb,channel_enable;
    uint8_t type,channels,channel_bytes,modules;
} atom_memory;
/* Read-only GDDR6 topology from VRAM_INFO 2.3/2.4/2.5 (master data 2.1,
 * index 28). Every advertised module must agree on geometry, capacity and
 * channel mask; no guessed module selection/clock/MC reconfiguration.
 * Does not prove current MC state or authorize a hardware modeset. */
enum atom_memory_error atom_memory_open(const atom_rom *,atom_memory *);
#endif
