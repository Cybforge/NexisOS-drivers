#ifndef NEXIS_VMM_H
#define NEXIS_VMM_H

#include "../include/types.h"
#include "../include/bootinfo.h"

#define PAGE_PRESENT    (1ULL << 0)
#define PAGE_WRITABLE   (1ULL << 1)
#define PAGE_USER       (1ULL << 2)
#define PAGE_WRITETHRU  (1ULL << 3)
#define PAGE_NOCACHE    (1ULL << 4)
#define PAGE_ACCESSED   (1ULL << 5)
#define PAGE_DIRTY      (1ULL << 6)
#define PAGE_HUGE       (1ULL << 7)
#define PAGE_GLOBAL     (1ULL << 8)
#define PAGE_NO_EXECUTE (1ULL << 63)

#define KERNEL_VIRTUAL_BASE 0xFFFF800000000000ULL
#define GPU_MODULE_VIRTUAL_BASE 0xffffa00000000000ULL

typedef uint64_t page_table_t[512];

bool vmm_init(nexis_boot_info_t *boot_info);
bool vmm_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(uint64_t *pml4, uint64_t virt);
uint64_t *vmm_create_address_space(void);
void vmm_destroy_address_space(uint64_t *pml4);
uint64_t *vmm_clone_user_address_space(uint64_t *source);
bool vmm_user_page(uint64_t *pml4, uint64_t address, bool writing, uint64_t *physical);
bool vmm_user_range(uint64_t *pml4, uint64_t address, size_t length, bool writing);
bool vmm_protect_user_page(uint64_t *pml4, uint64_t address, uint64_t flags);
void vmm_switch_pml4(uint64_t *pml4);
uint64_t *vmm_get_kernel_pml4(void);
bool vmm_map_mmio(uint64_t physical, size_t size);

#define VMM_CACHE_WB 0
#define VMM_CACHE_WC 1
#define VMM_CACHE_UC 2
bool vmm_set_cache_mode(uint64_t physical, uint64_t size, int mode);
/* Split RAM identity huge pages as needed. Module code's identity alias must
 * also be read-only/NX, so the executable high alias is not writable via RAM. */
bool vmm_protect_identity(uint64_t physical,size_t bytes,uint64_t flags);

#endif /* NEXIS_VMM_H */
