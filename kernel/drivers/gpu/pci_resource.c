#include "pci_resource.h"
static bool one(const uint32_t raw[6],const uint64_t base[6],const uint64_t bytes[6],unsigned bar,nexis_gpu_resource *out){
    for(unsigned n=0;n<6;n++){
        uint32_t low=raw[n];unsigned type=(low>>1)&3;
        bool wide=!(low&1) && type==2;
        if(n==bar){
            if(!low || (low&1) || (type!=0 && type!=2) || (wide && n==5))return false;
            uint64_t address=low&~15u;
            if(wide)address|=(uint64_t)raw[n+1]<<32;
            uint64_t size=bytes[n];
            if(address<0x10000000 || address>=(1ULL<<47) || address!=base[n] || size<4096 ||
               (size&(size-1)) || (address&(size-1)) || size>(1ULL<<47)-address ||
               (!wide && size>(1ULL<<32)-address))return false;
            *out=(nexis_gpu_resource){address,size,NEXIS_GPU_RESOURCE_MEMORY|
                (wide?NEXIS_GPU_RESOURCE_64BIT:0)|(low&8?NEXIS_GPU_RESOURCE_PREFETCH:0),0};
            return true;
        }
        if(wide){if(n+1==bar)return false;n++;}
    }
    return false;
}
bool gpu_pci_resource_decode(const uint32_t raw[6],const uint64_t base[6],const uint64_t bytes[6],unsigned bar,nexis_gpu_resource *out){
    if(!out)return false;
    *out=(nexis_gpu_resource){0};
    nexis_gpu_resource result;
    if(!raw || !base || !bytes || bar>=6 || !one(raw,base,bytes,bar,&result))return false;
    /* Distinct usable apertures must not overlap; aliases or inconsistent
     * firmware descriptors cannot authorize a register/VRAM mapping. */
    for(unsigned n=0;n<6;n++)if(n!=bar){
        nexis_gpu_resource other;
        if(one(raw,base,bytes,n,&other) && result.base<other.base+other.bytes && other.base<result.base+result.bytes)return false;
    }
    *out=result;return true;
}
