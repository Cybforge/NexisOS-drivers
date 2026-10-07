#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "../../boot/efi/efi.h"
#include "../../kernel/include/bootinfo.h"
#include "../../boot/efi/gpu_rom.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static EFI_HANDLE handles[2]={(EFI_HANDLE)1,(EFI_HANDLE)2};
static uint8_t first_path[]={1,1,6,0,0,1,0x7f,0xff,4,0};
static uint8_t right_path[]={1,1,6,0,0,2,0x7f,0xff,4,0};
static uint8_t output_path[]={1,1,6,0,0,2,1,1,6,0,0,0,0x7f,0xff,4,0};
static uint8_t rom[512],copied[4096];
static nexis_efi_pci_io first,right;
static unsigned allocations,frees,cases,segment_override,pci_failure;
static EFI_STATUS EFIAPI config(nexis_efi_pci_io *pci,unsigned width,UINT32 off,UINTN count,VOID *out){
    CHECK(width==2 && !off && (count==1 || count==4));if(pci_failure)return EFI_DEVICE_ERROR;
    UINT32 *data=out;data[0]=0x73ff1002;
    if(count==4){data[1]=0;data[2]=3u<<24;data[3]=0;}
    CHECK(pci==&first || pci==&right);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI location(nexis_efi_pci_io *pci,UINTN *segment,UINTN *bus,UINTN *slot,UINTN *func){
    CHECK(pci==&right);*segment=segment_override;*bus=3;*slot=0;*func=0;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI locate(EFI_LOCATE_SEARCH_TYPE type,EFI_GUID *guid,VOID *key,UINTN *count,EFI_HANDLE **out){
    CHECK(type==ByProtocol && guid->Data1==0x4cf5b200 && !key);*count=2;*out=handles;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI protocol(EFI_HANDLE handle,EFI_GUID *guid,VOID **out){
    if(guid->Data1==0x09576e91){*out=handle==(EFI_HANDLE)1?(void *)first_path:handle==(EFI_HANDLE)2?(void *)right_path:(void *)output_path;return EFI_SUCCESS;}
    CHECK(guid->Data1==0x4cf5b200 && (handle==(EFI_HANDLE)1 || handle==(EFI_HANDLE)2));
    *out=handle==(EFI_HANDLE)1?(void *)&first:(void *)&right;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI allocate(EFI_ALLOCATE_TYPE type,EFI_MEMORY_TYPE memory,UINTN pages,UINT64 *address){
    CHECK(type==AllocateMaxAddress && memory==EfiLoaderData && pages==1 && *address==0xffffffffULL);
    *address=(uintptr_t)copied;allocations++;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI release(VOID *buffer){CHECK(buffer==handles);frees++;return EFI_SUCCESS;}
static void setup(nexis_boot_info_t *info){memset(info,0,sizeof(*info));right.rom_size=512;right.rom_image=rom;segment_override=0;pci_failure=0;}
int main(void){
    CHECK(offsetof(nexis_efi_pci_io,rom_size)==144 && offsetof(nexis_efi_pci_io,rom_image)==152);
    first.pci_read=right.pci_read=config;first.location=right.location=location;
    EFI_BOOT_SERVICES bs={0};bs.LocateHandleBuffer=(void *)locate;bs.HandleProtocol=protocol;bs.AllocatePages=allocate;bs.FreePool=release;
    nexis_boot_info_t info;for(unsigned i=0;i<512;i++)rom[i]=(uint8_t)i;
    setup(&info);capture_gpu_rom(&bs,(EFI_HANDLE)3,&info);CHECK(frees==1 && allocations==1 && info.gpu_vendor==0x1002 && info.gpu_device==0x73ff && info.gpu_bus==3);
    CHECK(info.gpu_rom_size==512 && !memcmp(copied,rom,512));cases++;
    setup(&info);segment_override=1;capture_gpu_rom(&bs,(EFI_HANDLE)3,&info);CHECK(frees==2 && allocations==1 && !info.gpu_rom_addr && !info.gpu_vendor);cases++;
    setup(&info);right.rom_size=1024*1024+1;capture_gpu_rom(&bs,(EFI_HANDLE)3,&info);CHECK(frees==3 && allocations==1 && !info.gpu_rom_addr);cases++;
    setup(&info);right.rom_image=NULL;capture_gpu_rom(&bs,(EFI_HANDLE)3,&info);CHECK(frees==4 && allocations==1 && !info.gpu_rom_addr);cases++;
    setup(&info);pci_failure=1;capture_gpu_rom(&bs,(EFI_HANDLE)3,&info);CHECK(frees==5 && allocations==1 && !info.gpu_vendor);cases++;
    setup(&info);output_path[3]=0xff;capture_gpu_rom(&bs,(EFI_HANDLE)3,&info);CHECK(frees==5 && allocations==1 && !info.gpu_vendor);cases++;
    printf("{\"passed\":true,\"cases\":%u,\"gpu_firmware_executed\":false,\"output_pci_binding_verified\":true}\n",cases);return 0;
}
