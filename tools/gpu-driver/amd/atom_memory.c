/* Original bounded ATOM VRAM_INFO decoder. Format reference: AMD MIT
 * atomfirmware.h / bios_parser2.c from Linux v6.12; no firmware execution. */
#include "atom_memory.h"
#include <string.h>
static unsigned u16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t u32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
enum atom_memory_error atom_memory_open(const atom_rom *rom,atom_memory *out){
    if(!out)return ATOM_MEMORY_INPUT;
    memset(out,0,sizeof(*out));
    if(!rom || !rom->image || rom->data>rom->size || rom->size-rom->data<4)return ATOM_MEMORY_INPUT;
    if(rom->image[rom->data+2]!=2 || rom->image[rom->data+3]!=1)return ATOM_MEMORY_VERSION;
    atom_table t;if(!atom_rom_table(rom,false,28,&t))return ATOM_MEMORY_TABLE;
    if(t.format!=2 || t.revision<3 || t.revision>5)return ATOM_MEMORY_VERSION;
    if(t.size<24 || !t.bytes[20] || t.bytes[20]>16)return ATOM_MEMORY_TABLE;
    unsigned module_size_offset=t.revision==5?10:20;
    unsigned type_offset=t.revision==5?13:23;
    unsigned min_size=t.revision==3?52:t.revision==4?60:84;
    size_t offset=24;atom_memory first={0};
    for(unsigned n=0;n<t.bytes[20];n++){
        if(offset>t.size || t.size-offset<min_size)return ATOM_MEMORY_TABLE;
        const uint8_t *p=t.bytes+offset;unsigned size=u16(p+module_size_offset);
        if(size<min_size || size>t.size-offset)return ATOM_MEMORY_TABLE;
        atom_memory m={.memory_mb=u32(p),.channel_enable=u32(p+4),
            .type=p[type_offset],.channels=p[type_offset+1],.modules=t.bytes[20]};
        unsigned log2_bits=p[type_offset+2];
        if(m.type!=0x70 || m.channels<1 || m.channels>16 || log2_bits<4 || log2_bits>6 ||
           !m.memory_mb || m.memory_mb>65536 || !m.channel_enable)return ATOM_MEMORY_GEOMETRY;
        m.channel_bytes=(uint8_t)((1u<<log2_bits)/8);
        if(n && memcmp(&first,&m,sizeof(m)))return ATOM_MEMORY_AMBIGUOUS;
        first=m;offset+=size;
    }
    *out=first;return ATOM_MEMORY_OK;
}
