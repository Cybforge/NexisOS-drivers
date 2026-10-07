#ifndef NEXIS_EFI_GPU_ROM_H
#define NEXIS_EFI_GPU_ROM_H
/* Original NexisOS read-only UEFI PCI ROM capture. No option-ROM code executes.
 * Protocol ABI/layout per MdePkg/Include/Protocol/PciIo.h in EDK2. */
typedef struct nexis_efi_pci_io nexis_efi_pci_io;
struct nexis_efi_pci_io {
    VOID *poll_mem,*poll_io,*mem_read,*mem_write,*io_read,*io_write;
    EFI_STATUS (EFIAPI *pci_read)(nexis_efi_pci_io *,unsigned,UINT32,UINTN,VOID *);
    VOID *pci_write,*copy_mem,*map,*unmap,*allocate_buffer,*free_buffer,*flush;
    EFI_STATUS (EFIAPI *location)(nexis_efi_pci_io *,UINTN *,UINTN *,UINTN *,UINTN *);
    VOID *attributes;
    EFI_STATUS (EFIAPI *get_bar)(nexis_efi_pci_io *,UINT8,UINT64 *,VOID **);
    VOID *set_bar;
    UINT64 rom_size;VOID *rom_image;
};
static UINT64 gpu_resource_u64(const UINT8 *p){UINT64 value=0;for(unsigned n=0;n<8;n++)value|=(UINT64)p[n]<<(n*8);return value;}
static void capture_gpu_bars(EFI_BOOT_SERVICES *bs,nexis_efi_pci_io *pci,nexis_boot_info_t *info){
    if(!pci->get_bar)return;
    for(unsigned n=0;n<6;n++){
        VOID *resources=NULL;UINT64 attributes=0;
        if(pci->get_bar(pci,(UINT8)n,&attributes,&resources)!=EFI_SUCCESS || !resources)continue;
        const UINT8 *r=resources;
        /* EFI PCI I/O returns one ACPI QWORD memory descriptor and an end tag.
         * NexisOS currently supports segment zero with no address translation. */
        if(r[0]==0x8a && r[1]==43 && !r[2] && !r[3] && r[46]==0x79 && !gpu_resource_u64(r+30)){
            UINT64 start=gpu_resource_u64(r+14),bytes=gpu_resource_u64(r+38),granularity=gpu_resource_u64(r+6);
            /* GetBarAttributes uses AddrRangeMax for alignment in EDK2;
             * it is not the last byte of this resource. AddrLen is its size. */
            if(start>=0x10000000 && start<(1ULL<<47) && !(start&4095) && bytes && !(bytes&4095) && bytes<=(1ULL<<47)-start &&
               !(bytes&(bytes-1)) && !(start&(bytes-1)) && (granularity==32 || granularity==64)){
                info->gpu_bar_address[n]=start;info->gpu_bar_bytes[n]=bytes;
            }
        }
        bs->FreePool(resources);
    }
}
static UINTN gpu_device_path_size(const UINT8 *path){
    if(!path)return 0;
    UINTN total=0;
    for(unsigned node=0;node<64;node++){
        UINTN len=path[total+2]|(UINTN)path[total+3]<<8;
        if(len<4 || len>1024-total)return 0;
        if(path[total]==0x7f)return path[total+1]==0xff && len==4?total:0;
        total+=len;if(total>1020)return 0;
    }
    return 0;
}
static void capture_gpu_rom(EFI_BOOT_SERVICES *bs,EFI_HANDLE output,nexis_boot_info_t *info){
    EFI_GUID pci_guid={0x4cf5b200,0x68b8,0x4ca5,{0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};
    EFI_GUID path_guid=EFI_DEVICE_PATH_PROTOCOL_GUID;
    const UINT8 *output_path=NULL;
    if(!output || bs->HandleProtocol(output,&path_guid,(VOID **)&output_path)!=EFI_SUCCESS)return;
    UINTN output_size=gpu_device_path_size(output_path);if(!output_size)return;
    EFI_LOCATE_HANDLE_BUFFER locate=(EFI_LOCATE_HANDLE_BUFFER)bs->LocateHandleBuffer;
    EFI_HANDLE *handles=NULL;UINTN count=0,best_size=0;nexis_efi_pci_io *best=NULL;
    if(!locate || locate(ByProtocol,&pci_guid,NULL,&count,&handles)!=EFI_SUCCESS || !handles)return;
    for(UINTN i=0;i<count;i++){
        const UINT8 *path=NULL;nexis_efi_pci_io *pci=NULL;
        if(bs->HandleProtocol(handles[i],&path_guid,(VOID **)&path)!=EFI_SUCCESS)continue;
        UINTN size=gpu_device_path_size(path);if(!size || size>output_size || size<=best_size)continue;
        UINTN match=0;while(match<size && path[match]==output_path[match])match++;
        if(match!=size || bs->HandleProtocol(handles[i],&pci_guid,(VOID **)&pci)!=EFI_SUCCESS || !pci || !pci->pci_read || !pci->location)continue;
        UINT32 cfg[4]={0};
        if(pci->pci_read(pci,2,0,4,cfg)!=EFI_SUCCESS || (cfg[2]>>24)!=3)continue;
        best=pci;best_size=size;
    }
    if(best){
        UINT32 cfg=0;UINTN segment=0,bus=0,slot=0,func=0;
        if(best->pci_read(best,2,0,1,&cfg)==EFI_SUCCESS && best->location(best,&segment,&bus,&slot,&func)==EFI_SUCCESS &&
           !segment && bus<=255 && slot<32 && func<8){
            info->gpu_vendor=(UINT16)cfg;info->gpu_device=(UINT16)(cfg>>16);
            info->gpu_bus=(UINT8)bus;info->gpu_slot=(UINT8)slot;info->gpu_func=(UINT8)func;
            capture_gpu_bars(bs,best,info);
            /* BIOS tables are input for the future native AMD driver; never
             * enable a ROM BAR or copy a ROM from a different graphics card. */
            if(info->gpu_vendor==0x1002 && best->rom_image && best->rom_size>=512 && best->rom_size<=1024*1024){
                UINT64 address=0xffffffffULL;UINTN pages=(best->rom_size+4095)/4096;
                if(bs->AllocatePages(AllocateMaxAddress,EfiLoaderData,pages,&address)==EFI_SUCCESS){
                    UINT8 *dst=(UINT8 *)(UINTN)address;const UINT8 *src=best->rom_image;
                    for(UINTN j=0;j<best->rom_size;j++)dst[j]=src[j];
                    info->gpu_rom_addr=address;info->gpu_rom_size=(UINT32)best->rom_size;
                }
            }
        }
    }
    bs->FreePool(handles);
}
#endif
