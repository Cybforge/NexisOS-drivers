#ifndef NEXIS_AMD_ATOM_VM_H
#define NEXIS_AMD_ATOM_VM_H
#include "atom_tables.h"

/* ATOM is the board's register-programming bytecode, not an x86 option ROM.
 * The caller owns this context, its scratch buffer and the hardware callbacks.
 * This interpreter alone does not discover connectors or perform a modeset. */
enum atom_vm_error {
    ATOM_VM_OK, ATOM_VM_INPUT, ATOM_VM_TABLE, ATOM_VM_BYTECODE,
    ATOM_VM_UNSUPPORTED, ATOM_VM_BOUNDS, ATOM_VM_IO, ATOM_VM_LIMIT
};
enum atom_vm_space { ATOM_VM_MMIO, ATOM_VM_PLL, ATOM_VM_MC };
typedef struct {
    void *context;
    bool (*read)(void *,enum atom_vm_space,uint32_t,uint32_t *);
    bool (*write)(void *,enum atom_vm_space,uint32_t,uint32_t);
    bool (*delay_us)(void *,uint32_t);
    uint64_t (*time_us)(void *);
} atom_vm_io;
#define ATOM_VM_DEPTH 8
#define ATOM_VM_PARAMETERS 256
typedef struct {
    atom_table table;
    uint32_t workspace[256];
    uint8_t boundaries[8192];
    uint32_t *parameters;
    unsigned words,pc,shift;
} atom_vm_frame;
typedef struct {
    atom_rom rom;
    atom_vm_io io;
    uint8_t *scratch;
    size_t scratch_bytes;
    atom_vm_frame frames[ATOM_VM_DEPTH];
    uint16_t indirect_begin[256],indirect_end[256];
    uint32_t data_block,reg_block,fb_base,divmul[2],io_attr,bit_shift,io_mode;
    bool equal,above,busy,ready;
    enum atom_vm_error error;
    unsigned steps,step_limit;
    uint64_t deadline_us,delay_remaining_us;
} atom_vm;
bool atom_vm_init(atom_vm *,const atom_rom *,const atom_vm_io *,uint8_t *scratch,size_t);
/* Every reachable command is structurally checked before any hardware access.
 * Runtime IO errors stop immediately. A failed hardware transaction is not
 * implicitly rolled back: the display driver must restore its saved state. */
bool atom_vm_execute(atom_vm *,unsigned command,uint32_t *parameters,unsigned words);
const char *atom_vm_error_string(enum atom_vm_error);
#endif
