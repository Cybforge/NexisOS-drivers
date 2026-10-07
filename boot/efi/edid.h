#ifndef NEXIS_EFI_EDID_H
#define NEXIS_EFI_EDID_H
/* Original NexisOS protocol adapter. ABI identifiers/layout per UEFI/EDK2. */
typedef struct { UINT32 SizeOfEdid; UINT8 *Edid; } nexis_efi_edid_protocol;
static EFI_HANDLE capture_output_edid(EFI_BOOT_SERVICES *bs,EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,nexis_boot_info_t *info){
    EFI_GUID gop_guid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GUID active={0xbd8c1056,0x9f36,0x44ec,{0x92,0xa8,0xa6,0x33,0x7f,0x81,0x79,0x86}};
    EFI_GUID discovered={0x1c0c34f6,0xd380,0x41fa,{0xa0,0x49,0x8a,0xd0,0x6c,0x1a,0x66,0xaa}};
    EFI_LOCATE_HANDLE_BUFFER locate=(EFI_LOCATE_HANDLE_BUFFER)bs->LocateHandleBuffer;
    EFI_HANDLE *handles=NULL;UINTN count=0;
    info->edid_size=info->edid_source=0;
    EFI_HANDLE matched=NULL;
    if(!gop || !locate || locate(ByProtocol,&gop_guid,NULL,&count,&handles)!=EFI_SUCCESS || !handles)return NULL;
    /* Do not use LocateProtocol(EDID): on multi-GPU systems it can describe
     * a different monitor from the framebuffer being used for scanout. */
    for(UINTN i=0;i<count;i++){
        EFI_GRAPHICS_OUTPUT_PROTOCOL *output=NULL;
        if(bs->HandleProtocol(handles[i],&gop_guid,(VOID **)&output)!=EFI_SUCCESS || output!=gop)continue;
        matched=handles[i];
        nexis_efi_edid_protocol *protocol=NULL;UINT32 source=1;
        if(bs->HandleProtocol(handles[i],&active,(VOID **)&protocol)!=EFI_SUCCESS){
            source=2;
            if(bs->HandleProtocol(handles[i],&discovered,(VOID **)&protocol)!=EFI_SUCCESS)break;
        }
        if(!protocol || !protocol->Edid || protocol->SizeOfEdid<128 || protocol->SizeOfEdid%128 || protocol->SizeOfEdid>MAX_BOOT_EDID_BYTES)break;
        const UINT8 *bytes=protocol->Edid;UINT32 size=((UINT32)bytes[126]+1)*128;
        if(size>protocol->SizeOfEdid || size>MAX_BOOT_EDID_BYTES)break;
        for(UINT32 j=0;j<size;j++)info->edid[j]=bytes[j];
        info->edid_size=size;info->edid_source=source;break;
    }
    bs->FreePool(handles);
    return matched;
}
#endif
