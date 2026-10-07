#include "../../kernel/drivers/gpu/pci_resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases;
static uint32_t raw[6];static uint64_t base[6],bytes[6];
static void clear(void){memset(raw,0,sizeof(raw));memset(base,0,sizeof(base));memset(bytes,0,sizeof(bytes));}
static void put(unsigned n,uint64_t address,uint64_t size,bool wide,bool prefetch){
    raw[n]=(uint32_t)address|(wide?4u:0)|(prefetch?8u:0);
    if(wide && n<5)raw[n+1]=(uint32_t)(address>>32);
    base[n]=address;bytes[n]=size;
}
static bool decode(unsigned n,nexis_gpu_resource *out){return gpu_pci_resource_decode(raw,base,bytes,n,out);}
static void rejected(unsigned n){nexis_gpu_resource r,zero={0};memset(&r,0xa5,sizeof(r));CHECK(!decode(n,&r) && !memcmp(&r,&zero,sizeof(r)));cases++;}
int main(void){
    nexis_gpu_resource r;
    for(unsigned n=0;n<6;n++)for(unsigned wide=0;wide<2;wide++)for(unsigned pf=0;pf<2;pf++)for(unsigned size=12;size<=33;size++){
        clear();uint64_t address=wide?0x2000000000ULL:0x80000000ULL,length=1ULL<<size;
        put(n,address,length,wide!=0,pf!=0);
        bool valid=(wide?n<5:size<=31);
        CHECK(decode(n,&r)==valid);
        if(valid)CHECK(r.base==address && r.bytes==length && !r.reserved && r.flags==(1u|(wide?2u:0)|(pf?4u:0)));
        else CHECK(!r.base && !r.bytes && !r.flags);
        if(wide && n<5){CHECK(!decode(n+1,&r));}
        cases++;
    }
    for(unsigned n=0;n<6;n++)for(unsigned kind=0;kind<15;kind++){
        clear();put(n,0x80000000,0x10000000,false,false);
        switch(kind){
            case 0:raw[n]|=1;break;case 1:raw[n]|=2;break;case 2:raw[n]|=6;break;case 3:raw[n]=0;break;
            case 4:base[n]+=0x1000;break;case 5:bytes[n]=0;break;case 6:bytes[n]=2048;break;
            case 7:bytes[n]++;break;case 8:bytes[n]=0x100000000ULL;break;case 9:base[n]=0;break;
            case 10:put(n,0x80100000,0x10000000,false,false);break;
            case 11:put(n,0x8000000,4096,false,false);break;
            case 12:put(n,1ULL<<47,4096,true,false);break;
            case 13:put(n,(1ULL<<47)-4096,8192,true,false);break;
            case 14:bytes[n]=UINT64_MAX;break;
        }
        rejected(n);
    }
    clear();put(0,0x800000000,0x200000000ULL,true,true);put(2,0xa0000000,0x10000,false,false);put(5,0xb0000000,0x100000,false,false);
    CHECK(decode(0,&r) && r.base==0x800000000 && r.bytes==0x200000000ULL && r.flags==7);
    CHECK(decode(2,&r) && r.base==0xa0000000 && r.flags==1);CHECK(decode(5,&r) && r.base==0xb0000000);cases++;
    /* Even a malformed 64-bit low BAR consumes its upper slot. That slot's
     * bits must never become independent memory access authorization. */
    clear();raw[0]=0x80000004;raw[1]=0xa0000000;base[1]=0xa0000000;bytes[1]=4096;rejected(1);
    clear();put(0,0x80000000,0x10000000,false,false);put(2,0x80000000,4096,false,false);rejected(0);rejected(2);
    clear();put(0,0x80000000,0x10000000,false,false);put(2,0x90000000,4096,false,false);CHECK(decode(0,&r) && decode(2,&r));cases++;
    for(unsigned n=6;n<16;n++)rejected(n);
    CHECK(!gpu_pci_resource_decode(raw,base,bytes,0,NULL));cases++;
    const uint64_t *b=base,*s=bytes;
    for(unsigned n=0;n<3;n++){
        memset(&r,0xa5,sizeof(r));CHECK(!gpu_pci_resource_decode(n?raw:NULL,n==1?NULL:b,n==2?NULL:s,0,&r) && !r.base && !r.bytes && !r.flags && !r.reserved);cases++;
    }
    printf("{\"passed\":true,\"cases\":%u,\"live_pci_firmware_resource_policy\":true,\"pci_writes\":false,\"physical_hardware_verified\":false}\n",cases);return 0;
}
