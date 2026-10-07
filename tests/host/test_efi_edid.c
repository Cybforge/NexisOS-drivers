/* Original MIT licensed checks for output-associated UEFI EDID capture. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../boot/efi/efi.h"
#include "../../kernel/include/bootinfo.h"
#include "../../boot/efi/edid.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static EFI_GRAPHICS_OUTPUT_PROTOCOL first,second;
static EFI_HANDLE handles[2]={(EFI_HANDLE)1,(EFI_HANDLE)2};
static UINT8 bytes[256],wrong_bytes[128];
static nexis_efi_edid_protocol right={256,bytes},wrong={128,wrong_bytes};
static unsigned frees,active_missing,locate_fail,protocol_null,cases;
static EFI_STATUS EFIAPI locate(EFI_LOCATE_SEARCH_TYPE type,EFI_GUID *guid,VOID *key,UINTN *count,EFI_HANDLE **out){
    CHECK(type==ByProtocol && guid->Data1==0x9042a9de && !key);
    if(locate_fail)return EFI_NOT_FOUND;
    *count=2;*out=handles;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI protocol(EFI_HANDLE handle,EFI_GUID *guid,VOID **out){
    CHECK(handle==(EFI_HANDLE)1 || handle==(EFI_HANDLE)2);
    if(guid->Data1==0x9042a9de){*out=handle==(EFI_HANDLE)1?(void *)&first:(void *)&second;return EFI_SUCCESS;}
    CHECK(guid->Data1==0xbd8c1056 || guid->Data1==0x1c0c34f6);
    if(active_missing && guid->Data1==0xbd8c1056)return EFI_NOT_FOUND;
    *out=protocol_null?NULL:handle==(EFI_HANDLE)1?(void *)&wrong:(void *)&right;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI release(VOID *buffer){CHECK(buffer==handles);frees++;return EFI_SUCCESS;}
int main(void){
    EFI_BOOT_SERVICES bs={0};bs.LocateHandleBuffer=(void *)locate;bs.HandleProtocol=protocol;bs.FreePool=release;
    nexis_boot_info_t info={0};memset(bytes,0x23,sizeof(bytes));bytes[126]=1;
    memset(wrong_bytes,0x42,sizeof(wrong_bytes));wrong_bytes[126]=0;
    capture_output_edid(&bs,&second,&info);CHECK(frees==1 && info.edid_size==256 && info.edid_source==1 && !memcmp(info.edid,bytes,256));cases++;
    active_missing=1;capture_output_edid(&bs,&second,&info);CHECK(frees==2 && info.edid_source==2);cases++;
    active_missing=0;right.SizeOfEdid=128;capture_output_edid(&bs,&second,&info);CHECK(frees==3 && !info.edid_size && !info.edid_source);cases++;
    right.SizeOfEdid=256;bytes[126]=255;capture_output_edid(&bs,&second,&info);CHECK(frees==4 && !info.edid_size);cases++;
    bytes[126]=1;protocol_null=1;capture_output_edid(&bs,&second,&info);CHECK(frees==5 && !info.edid_size);cases++;
    protocol_null=0;right.SizeOfEdid=255;capture_output_edid(&bs,&second,&info);CHECK(frees==6 && !info.edid_size);cases++;
    right.SizeOfEdid=4096;capture_output_edid(&bs,&second,&info);CHECK(frees==7 && !info.edid_size);cases++;
    locate_fail=1;capture_output_edid(&bs,&second,&info);CHECK(frees==7 && !info.edid_size);cases++;
    locate_fail=0;capture_output_edid(&bs,&first,&info);CHECK(frees==8 && info.edid_size==128 && info.edid[0]==0x42);cases++;
    printf("{\"passed\":true,\"cases\":%u,\"wrong_output_edid_rejected\":true}\n",cases);return 0;
}
