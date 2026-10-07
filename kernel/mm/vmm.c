#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "../include/string.h"
#include "../include/io.h"
#include "../drivers/serial.h"

static uint64_t *g_kernel_pml4 = NULL;

static inline void invlpg(uint64_t addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

static uint64_t *get_next_level(uint64_t *table, size_t index, bool allocate, uint64_t flags) {
    if (table[index] & PAGE_PRESENT) {
        if (table[index] & PAGE_HUGE) return NULL;
        if (flags & PAGE_USER) table[index] |= PAGE_USER;
        return (uint64_t*)(uintptr_t)(table[index] & 0x000FFFFFFFFFF000ULL);
    }

    if (!allocate) return NULL;

    uint64_t new_table_phys = pmm_alloc_page();
    if (!new_table_phys) return NULL;

    uint64_t *new_table = (uint64_t*)(uintptr_t)new_table_phys;
    memset(new_table, 0, PAGE_SIZE);

    table[index] = new_table_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    return new_table;
}

bool vmm_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4 || (virt & 4095) || (phys & 4095)) return false;
    /* The first 64 GiB hold supervisor identity mappings needed by the kernel. */
    if ((flags & PAGE_USER) && (pml4 == g_kernel_pml4 || virt < (64ULL << 30) || virt >= (1ULL << 47))) return false;

    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx   = (virt >> 21) & 0x1FF;
    size_t pt_idx   = (virt >> 12) & 0x1FF;

    if ((flags & PAGE_USER) && g_kernel_pml4) {
        uint64_t shared = g_kernel_pml4[pml4_idx];
        /* Only PML4[0] has a private copied PDPT. Other kernel entries and
         * existing device regions must never be promoted to user mappings. */
        if ((shared & PAGE_PRESENT) && pml4_idx != 0) return false;
        if (shared & PAGE_PRESENT) {
            uint64_t *kernel_pdpt = (uint64_t *)(uintptr_t)(shared & 0x000FFFFFFFFFF000ULL);
            if (kernel_pdpt[pdpt_idx] & PAGE_PRESENT) return false;
        }
    }

    uint64_t *pdpt = get_next_level(pml4, pml4_idx, true, flags);
    if (!pdpt) return false;

    uint64_t *pd = get_next_level(pdpt, pdpt_idx, true, flags);
    if (!pd) return false;

    uint64_t *pt = get_next_level(pd, pd_idx, true, flags);
    if (!pt) return false;
    if ((flags & PAGE_USER) && (pt[pt_idx] & PAGE_PRESENT)) return false;

    pt[pt_idx] = (phys & ~0xFFFULL) | flags | PAGE_PRESENT;
    invlpg(virt);

    return true;
}

void vmm_unmap_page(uint64_t *pml4, uint64_t virt) {
    if (!pml4) return;

    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx   = (virt >> 21) & 0x1FF;
    size_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = get_next_level(pml4, pml4_idx, false, 0);
    if (!pdpt) return;

    uint64_t *pd = get_next_level(pdpt, pdpt_idx, false, 0);
    if (!pd) return;

    uint64_t *pt = get_next_level(pd, pd_idx, false, 0);
    if (!pt) return;

    pt[pt_idx] = 0;
    invlpg(virt);
}

bool vmm_init(nexis_boot_info_t *boot_info) {
    uint64_t pml4_phys = pmm_alloc_page();
    if (!pml4_phys) {
        kprintf("[VMM] KRITISCH: Konnte PML4-Tabelle nicht allozieren!\n");
        return false;
    }

    g_kernel_pml4 = (uint64_t*)(uintptr_t)pml4_phys;
    memset(g_kernel_pml4, 0, PAGE_SIZE);

    /* 1. Sauberes Identity-Mapping der ersten 64 GiB (deckt allen RAM, Kernel & PCI MMIO ab) */
    uint64_t *pdpt = get_next_level(g_kernel_pml4, 0, true, PAGE_WRITABLE);
    if (!pdpt) goto allocation_failed;
    for (uint64_t gib = 0; gib < 64; gib++) {
        uint64_t *pd = get_next_level(pdpt, (size_t)gib, true, PAGE_WRITABLE);
        if (!pd) goto allocation_failed;
        for (uint64_t pd_idx = 0; pd_idx < 512; pd_idx++) {
            uint64_t phys = (gib * (1ULL * 1024 * 1024 * 1024)) + (pd_idx * (2ULL * 1024 * 1024));
            pd[pd_idx] = phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;
        }
    }

    /* 1b. Sicherstellen, dass der GOP Framebuffer (auch oberhalb von 16 GiB / Resizable BAR) gemappt ist */
    if (boot_info && boot_info->framebuffer_base >= (16ULL * 1024 * 1024 * 1024)) {
        uint64_t fb_start = boot_info->framebuffer_base & ~0x1FFFFFULL;
        uint64_t fb_size = boot_info->framebuffer_size ? boot_info->framebuffer_size : ((uint64_t)boot_info->pixels_per_scanline * boot_info->screen_height * 4);
        if (boot_info->framebuffer_base >= (1ULL << 47) || fb_size > (1ULL << 47) - boot_info->framebuffer_base) goto allocation_failed;
        uint64_t fb_end = (boot_info->framebuffer_base + fb_size + 0x1FFFFFULL) & ~0x1FFFFFULL;
        for (uint64_t addr = fb_start; addr < fb_end; addr += 0x200000ULL) {
            size_t pml4_idx = (addr >> 39) & 0x1FF;
            size_t pdpt_idx = (addr >> 30) & 0x1FF;
            size_t pd_idx   = (addr >> 21) & 0x1FF;
            uint64_t *l3 = get_next_level(g_kernel_pml4, pml4_idx, true, PAGE_WRITABLE);
            if (!l3) goto allocation_failed;
            uint64_t *l2 = get_next_level(l3, pdpt_idx, true, PAGE_WRITABLE);
            if (!l2) goto allocation_failed;
            l2[pd_idx] = addr | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;
        }
    }

    /* Reserve the module branch before any process copies the kernel PML4.
     * Later driver mappings then remain visible in existing address spaces. */
    if(!get_next_level(g_kernel_pml4,(GPU_MODULE_VIRTUAL_BASE>>39)&511,true,PAGE_WRITABLE))goto allocation_failed;

    /* 2. In CR3 schreiben und Page Tables aktivieren */
    write_cr3(pml4_phys);
    kprintf("[VMM] 4-level paging ready (PML4 at 0x%p, 64 GiB identity map + framebuffer)\n", g_kernel_pml4);
    return true;

allocation_failed:
    /* None of these tables has been activated in CR3. Only free the table
     * pages; their huge identity entries do not own the physical memory. */
    for (unsigned a = 0; a < 512; a++) if (g_kernel_pml4[a] & PAGE_PRESENT) {
        uint64_t *l3 = (uint64_t *)(uintptr_t)(g_kernel_pml4[a] & 0x000FFFFFFFFFF000ULL);
        for (unsigned b = 0; b < 512; b++) if ((l3[b] & PAGE_PRESENT) && !(l3[b] & PAGE_HUGE))
            pmm_free_page(l3[b] & 0x000FFFFFFFFFF000ULL);
        pmm_free_page((uint64_t)(uintptr_t)l3);
    }
    pmm_free_page(pml4_phys);
    g_kernel_pml4 = NULL;
    return false;
}

uint64_t *vmm_create_address_space(void) {
    uint64_t pml4_phys = pmm_alloc_page();
    if (!pml4_phys) return NULL;

    uint64_t *new_pml4 = (uint64_t*)(uintptr_t)pml4_phys;
    memset(new_pml4, 0, PAGE_SIZE);

    /* Kernel Higher-Half & Identity Mapping aus Master-PML4 kopieren */
    if (g_kernel_pml4) {
        /* Erste Hälfte (Identity Mapping) und 511. Eintrag (Higher Half) kopieren */
        memcpy(new_pml4, g_kernel_pml4, PAGE_SIZE);
        /* Private PDPT: adding user mappings must not mutate the kernel table. */
        uint64_t lower = pmm_alloc_page();
        if (!lower) { pmm_free_page(pml4_phys); return NULL; }
        memcpy((void *)(uintptr_t)lower, (void *)(uintptr_t)(g_kernel_pml4[0] & 0x000FFFFFFFFFF000ULL), PAGE_SIZE);
        new_pml4[0] = lower | PAGE_PRESENT | PAGE_WRITABLE;
    }

    return new_pml4;
}

static void destroy_private_table(uint64_t *table, uint64_t *shared, unsigned level) {
    for (unsigned i = 0; i < 512; i++) {
        uint64_t e = table[i];
        if (!(e & PAGE_PRESENT)) continue;
        uint64_t address = e & 0x000FFFFFFFFFF000ULL;
        if (shared && address == (shared[i] & 0x000FFFFFFFFFF000ULL) && (shared[i] & PAGE_PRESENT)) continue;
        if (level == 1) { if (e & PAGE_USER) pmm_free_page(address); }
        else if (!(e & PAGE_HUGE)) {
            uint64_t *shared_next = shared && (shared[i] & PAGE_PRESENT) && !(shared[i] & PAGE_HUGE) ?
                (uint64_t *)(uintptr_t)(shared[i] & 0x000FFFFFFFFFF000ULL) : NULL;
            destroy_private_table((uint64_t *)(uintptr_t)address, shared_next, level - 1);
        }
    }
    pmm_free_page((uint64_t)(uintptr_t)table);
}

void vmm_destroy_address_space(uint64_t *pml4) {
    if (pml4 && pml4 != g_kernel_pml4) destroy_private_table(pml4, g_kernel_pml4, 4);
}

/* Fork copies only validated user leaf pages; supervisor identity/device
 * mappings retain the normal shared kernel tables. Roll back every failure. */
static bool clone_user_table(uint64_t *dest,uint64_t *table,unsigned shift,uint64_t base,size_t *pages) {
    for(unsigned i=0;i<512;i++) {
        uint64_t e=table[i];
        if((e&(PAGE_PRESENT|PAGE_USER))!=(PAGE_PRESENT|PAGE_USER))continue;
        if(e&PAGE_HUGE)return false;
        uint64_t address=base|((uint64_t)i<<shift),phys=e&0x000FFFFFFFFFF000ULL;
        if(shift>12){if(!clone_user_table(dest,(void *)(uintptr_t)phys,shift-9,address,pages))return false;}
        else {
            if(++*pages>32768)return false;
            uint64_t copy=pmm_alloc_page();if(!copy)return false;
            memcpy((void *)(uintptr_t)copy,(void *)(uintptr_t)phys,PAGE_SIZE);
            if(!vmm_map_page(dest,address,copy,e&(PAGE_USER|PAGE_WRITABLE|PAGE_NO_EXECUTE))){pmm_free_page(copy);return false;}
        }
    }
    return true;
}
uint64_t *vmm_clone_user_address_space(uint64_t *source) {
    if(!source || source==g_kernel_pml4)return NULL;
    uint64_t *copy=vmm_create_address_space();size_t pages=0;
    if(copy && !clone_user_table(copy,source,39,0,&pages)){vmm_destroy_address_space(copy);copy=NULL;}
    return copy;
}

bool vmm_user_page(uint64_t *pml4, uint64_t address, bool writing, uint64_t *physical) {
    if (!pml4 || pml4 == g_kernel_pml4 || address < (64ULL << 30) || address >= (1ULL << 47)) return false;
    uint64_t *table = pml4;
    for (int shift = 39; shift >= 12; shift -= 9) {
        uint64_t e = table[(address >> shift) & 511];
        if ((e & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER) || (writing && !(e & PAGE_WRITABLE))) return false;
        if (shift == 12) { if (physical) *physical = (e & 0x000FFFFFFFFFF000ULL) | (address & 4095); return true; }
        if (e & PAGE_HUGE) return false;
        table = (uint64_t *)(uintptr_t)(e & 0x000FFFFFFFFFF000ULL);
    }
    return false;
}
bool vmm_user_range(uint64_t *pml4, uint64_t address, size_t length, bool writing) {
    if (!length) return true;
    if (address >= (1ULL << 47) || length > (1ULL << 47) - address) return false;
    uint64_t end = address + length;
    for (uint64_t p = ALIGN_DOWN(address, PAGE_SIZE); p < end; p += PAGE_SIZE)
        if (!vmm_user_page(pml4, p, writing, NULL)) return false;
    return true;
}
bool vmm_protect_user_page(uint64_t *pml4,uint64_t address,uint64_t flags) {
    if(!vmm_user_page(pml4,address,false,NULL))return false;
    uint64_t *table=pml4;
    for(int shift=39;shift>12;shift-=9)table=(void *)(uintptr_t)(table[(address>>shift)&511]&0x000FFFFFFFFFF000ULL);
    uint64_t *e=&table[(address>>12)&511];
    *e=(*e&0x000FFFFFFFFFF000ULL)|PAGE_PRESENT|PAGE_USER|(flags&(PAGE_WRITABLE|PAGE_NO_EXECUTE));
    invlpg(address);return true;
}

void vmm_switch_pml4(uint64_t *pml4) {
    if (pml4) {
        write_cr3((uint64_t)(uintptr_t)pml4);
    }
}

uint64_t *vmm_get_kernel_pml4(void) {
    return g_kernel_pml4;
}

/* Device registers must not use the write-back RAM identity mapping. */
bool vmm_map_mmio(uint64_t physical, size_t size) {
    if (!g_kernel_pml4 || !size || physical >= (1ULL << 47) ||
        size > (1ULL << 47) - physical) return false;
    uint64_t end = ALIGN_UP(physical + size, 0x200000ULL);
    for (uint64_t addr = ALIGN_DOWN(physical, 0x200000ULL); addr < end; addr += 0x200000ULL) {
        uint64_t *l3 = get_next_level(g_kernel_pml4, (addr >> 39) & 511, true, PAGE_WRITABLE);
        if (!l3) return false;
        uint64_t *l2 = get_next_level(l3, (addr >> 30) & 511, true, PAGE_WRITABLE);
        if (!l2) return false;
        size_t slot = (addr >> 21) & 511;
        if ((l2[slot] & PAGE_PRESENT) && !(l2[slot] & PAGE_HUGE)) return false;
        l2[slot] = addr | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE | PAGE_NOCACHE | PAGE_WRITETHRU;
        invlpg(addr);
    }
    return true;
}

/* Changes the caching mode of identity-mapped 2 MiB pages. Pages that
 * already carry an uncached device mapping are left uncached. */
bool vmm_set_cache_mode(uint64_t physical, uint64_t size, int mode) {
    if (!g_kernel_pml4 || !size || physical >= (1ULL << 47) || size > (1ULL << 47) - physical ||
        (mode != VMM_CACHE_WB && mode != VMM_CACHE_WC && mode != VMM_CACHE_UC)) return false;
    uint64_t end = ALIGN_UP(physical + size, 0x200000ULL);
    bool changed = false;
    for (uint64_t addr = ALIGN_DOWN(physical, 0x200000ULL); addr < end; addr += 0x200000ULL) {
        uint64_t *l3 = get_next_level(g_kernel_pml4, (addr >> 39) & 511, false, 0);
        if (!l3) return changed;
        uint64_t *l2 = get_next_level(l3, (addr >> 30) & 511, false, 0);
        if (!l2) return changed;
        uint64_t *e = &l2[(addr >> 21) & 511];
        if (!(*e & PAGE_PRESENT) || !(*e & PAGE_HUGE)) continue;
        if ((*e & PAGE_NOCACHE) && mode != VMM_CACHE_UC) continue;
        *e &= ~(PAGE_WRITETHRU | PAGE_NOCACHE);
        if (mode == VMM_CACHE_WC) *e |= PAGE_WRITETHRU;          /* PAT index 1 = WC */
        else if (mode == VMM_CACHE_UC) *e |= PAGE_WRITETHRU | PAGE_NOCACHE;
        invlpg(addr);
        changed = true;
    }
    return changed;
}

bool vmm_protect_identity(uint64_t physical,size_t bytes,uint64_t flags){
    if(!g_kernel_pml4 || !bytes || (physical&4095) || (bytes&4095) || physical>=(64ULL<<30) || bytes>(64ULL<<30)-physical ||
       (flags&~(PAGE_WRITABLE|PAGE_NO_EXECUTE)))return false;
    for(uint64_t address=physical;address<physical+bytes;address+=4096){
        uint64_t *l3=get_next_level(g_kernel_pml4,(address>>39)&511,false,0);
        uint64_t *l2=l3?get_next_level(l3,(address>>30)&511,false,0):NULL;
        if(!l2)return false;
        uint64_t *entry=&l2[(address>>21)&511];
        if(!(*entry&PAGE_PRESENT))return false;
        if(*entry&PAGE_HUGE){
            uint64_t page=pmm_alloc_page();if(!page)return false;
            uint64_t *table=(void *)(uintptr_t)page,old=*entry,base=old&0x000fffffffe00000ULL;
            uint64_t inherited=old&(PAGE_WRITABLE|PAGE_WRITETHRU|PAGE_NOCACHE|PAGE_NO_EXECUTE);
            if(old&(1ULL<<12))inherited|=1ULL<<7; /* huge PAT -> leaf PAT */
            for(unsigned n=0;n<512;n++)table[n]=(base+(uint64_t)n*4096)|PAGE_PRESENT|inherited;
            *entry=page|PAGE_PRESENT|PAGE_WRITABLE;
            /* Remove all cached translations of the former huge page. */
            for(unsigned n=0;n<512;n++)invlpg(base+(uint64_t)n*4096);
        }
        uint64_t *table=(void *)(uintptr_t)(*entry&0x000FFFFFFFFFF000ULL),*leaf=&table[(address>>12)&511];
        if(!(*leaf&PAGE_PRESENT) || (*leaf&0x000FFFFFFFFFF000ULL)!=address)return false;
        *leaf=(*leaf&~(PAGE_WRITABLE|PAGE_NO_EXECUTE))|flags;invlpg(address);
    }
    return true;
}
