#include "../../../tools/gpu-driver/include/nexis_gpu_v2.h"
#include "../../include/string.h"
static uint32_t u32(const uint8_t *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
bool nexis_gpu_image_parse(const uint8_t *data,size_t size,uint16_t vendor,uint16_t device,nexis_gpu_image *out){
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!data || size<=NEXIS_GPU_V2_HEADER || size>NEXIS_GPU_V2_HEADER+NEXIS_GPU_V2_MAX_BYTES ||
       memcmp(data,"NDRV",4) || u32(data+4)!=NEXIS_GPU_ABI_RETAINED || u32(data+40)!=NEXIS_GPU_V2_HEADER)return false;
    nexis_gpu_image i={u32(data+8),u32(data+12),u32(data+16),u32(data+20),u32(data+24),
        (uint16_t)(data[32]|(uint16_t)data[33]<<8),(uint16_t)(data[34]|(uint16_t)data[35]<<8)};
    if(i.image_bytes!=size-NEXIS_GPU_V2_HEADER || !i.text_bytes || i.entry>=i.text_bytes || i.text_bytes>i.image_bytes ||
       (i.writable_offset&4095) || i.writable_offset<((i.text_bytes+4095)&~4095u) || i.writable_offset>i.image_bytes ||
       !i.memory_bytes || (i.memory_bytes&4095) || i.image_bytes>i.memory_bytes || i.memory_bytes>NEXIS_GPU_V2_MAX_BYTES ||
       i.vendor!=vendor || i.device!=device || u32(data+36)!=sizeof(nexis_gpu_services))return false;
    for(unsigned n=28;n<32;n++)if(data[n])return false;
    for(unsigned n=44;n<NEXIS_GPU_V2_HEADER;n++)if(data[n])return false;
    *out=i;return true;
}
bool nexis_gpu_image_code_pointer(const nexis_gpu_image *i,uintptr_t base,uintptr_t pointer){
    return i && pointer>=base && pointer-base<i->text_bytes;
}
bool nexis_gpu_image_state_pointer(const nexis_gpu_image *i,uintptr_t base,uintptr_t pointer,size_t bytes){
    return i && bytes && pointer>=base && pointer-base>=i->writable_offset &&
        pointer-base<=i->memory_bytes && bytes<=i->memory_bytes-(pointer-base);
}
