#ifndef NEXIS_BOOTINFO_H
#define NEXIS_BOOTINFO_H

#include <stdint.h>

#define NEXIS_BOOT_MAGIC 0x4E455849534F5331ULL /* "NEXISOS1" */

#define PIXEL_FORMAT_RGB  0
#define PIXEL_FORMAT_BGR  1
#define PIXEL_FORMAT_BITMASK 2

#define MAX_BOOT_DISKS 8
#define MAX_BOOT_EDID_BYTES 2048

typedef struct {
    uint32_t drive_id;
    uint32_t is_removable;  /* 1 = Removable USB Stick, 0 = Internal Disk/SSD */
    uint32_t is_partition;  /* 0 = Whole physical drive, 1 = Logical partition */
    uint32_t block_size;    /* 512, 4096 */
    uint64_t total_sectors; /* LastBlock + 1 */
    uint64_t total_bytes;   /* Capacity in bytes */
    char     model[64];     /* Model description */
} __attribute__((packed)) boot_disk_info_t;

typedef struct {
    uint64_t magic;                 /* NEXIS_BOOT_MAGIC */
    uint64_t framebuffer_base;      /* Physical base of framebuffer */
    uint64_t framebuffer_size;      /* Framebuffer size in bytes */
    uint32_t screen_width;          /* Screen width in pixels */
    uint32_t screen_height;         /* Screen height in pixels */
    uint32_t pixels_per_scanline;   /* Pixels per line */
    uint32_t pixel_format;          /* RGB, BGR, etc. */
    
    uint64_t memory_map_addr;       /* Physical address of UEFI memory map */
    uint64_t memory_map_size;       /* Total size of memory map */
    uint64_t descriptor_size;       /* Size of each EFI_MEMORY_DESCRIPTOR */
    uint32_t descriptor_version;    /* Descriptor version */
    uint64_t total_memory_bytes;    /* Detected RAM in bytes */
    
    uint64_t acpi_rsdp;             /* ACPI RSDP physical address */
    uint64_t initrd_addr;           /* Initial ramdisk physical address */
    uint64_t initrd_size;           /* Initial ramdisk size in bytes */
    
    char cmdline[256];              /* Kernel command line */

    uint32_t disk_count;
    boot_disk_info_t disks[MAX_BOOT_DISKS];
    uint64_t utc_epoch;             /* UEFI RTC fields read as UTC, seconds since 1970; 0 if invalid */
    uint8_t entropy[32];            /* EFI_RNG_PROTOCOL seed, never a clock substitute */
    uint32_t entropy_valid;
    int16_t  rtc_timezone;          /* minutes from UTC reported by firmware, 2047 = unspecified */
    uint8_t  rtc_daylight;
    uint8_t  reserved0;
    uint64_t kernel_image_end;      /* physical end of the loaded kernel incl. .bss */
    uint32_t gop_mode_count;
    uint32_t gop_mode;
    uint8_t  boot_part_guid[16];    /* unique GUID of the GPT partition the loader started from (live medium / installed ESP) */
    uint32_t boot_part_valid;
    uint32_t reserved1;
    uint64_t kernel_file_addr;      /* unmodified copy of KERNEL.BIN as read from the boot medium (kept for the installer) */
    uint64_t kernel_file_size;
    uint32_t edid_size;            /* copied from the same output handle as GOP; 0 if unavailable */
    uint32_t edid_source;          /* 1 = active firmware EDID; 2 = discovered EDID */
    uint8_t edid[MAX_BOOT_EDID_BYTES]; /* bytes remain valid after ExitBootServices */
    uint64_t gpu_rom_addr;          /* firmware's ROM image for the GOP's PCI device, retained as data */
    uint32_t gpu_rom_size;
    uint16_t gpu_vendor,gpu_device;
    uint8_t gpu_bus,gpu_slot,gpu_func,gpu_reserved;
} __attribute__((packed)) nexis_boot_info_t;

const nexis_boot_info_t *bootinfo_get(void);

#endif /* NEXIS_BOOTINFO_H */
