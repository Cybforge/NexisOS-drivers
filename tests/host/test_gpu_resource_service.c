/* Compile the actual kernel service callback into a host model. Only PCI
 * config reads/resource()/register-address bounds run; VMM/load/MMIO do not. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/drivers/gpu/runtime.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
static unsigned cases,reads,mutate_at;static uint32_t config[64];
static nexis_boot_info_t boot;static bool boot_present;
uint32_t pci_read_dword(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset){
    CHECK(bus==1 && slot==0 && func==0 && !(offset&3));
    if(++reads==mutate_at)config[4]+=0x10000000;
    return config[offset/4];
}
uint16_t pci_read_word(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset){return (uint16_t)(pci_read_dword(bus,slot,func,offset&~3u)>>((offset&2)*8));}
const nexis_boot_info_t *bootinfo_get(void){return boot_present?&boot:NULL;}
uint64_t pit_tsc_hz(void){return 0;}uint64_t pit_get_ticks(void){return 0;}
uint64_t pmm_alloc_pages(size_t n){(void)n;CHECK(false);return 0;}
void pmm_free_page(uint64_t n){(void)n;CHECK(false);}
uint64_t *vmm_get_kernel_pml4(void){CHECK(false);return NULL;}
void vmm_unmap_page(uint64_t *p,uint64_t a){(void)p;(void)a;CHECK(false);}
bool vmm_map_page(uint64_t *p,uint64_t a,uint64_t b,uint64_t f){(void)p;(void)a;(void)b;(void)f;CHECK(false);return false;}
bool vmm_map_mmio(uint64_t a,size_t n){(void)a;(void)n;CHECK(false);return false;}
bool vmm_protect_identity(uint64_t a,size_t n,uint64_t f){(void)a;(void)n;(void)f;CHECK(false);return false;}
void display_clear_native_mode(void){CHECK(false);}
void display_set_native_mode(const edid_timing *t){(void)t;CHECK(false);}
const edid_monitor *display_monitor_get(void){CHECK(false);return NULL;}
const edid_timing *edid_select_rgb8(const edid_monitor *m,uint32_t w,uint32_t h,const edid_link_limits *l){(void)m;(void)w;(void)h;(void)l;CHECK(false);return NULL;}
uint32_t edid_refresh_millihz(const edid_timing *t){(void)t;CHECK(false);return 0;}
uint64_t fb_front_base(void){CHECK(false);return 0;}uint64_t fb_front_size(void){CHECK(false);return 0;}
uint32_t fb_get_pitch(void){CHECK(false);return 0;}uint32_t fb_get_width(void){CHECK(false);return 0;}uint32_t fb_get_height(void){CHECK(false);return 0;}
bool fb_is_rgb(void){CHECK(false);return false;}
bool fb_native_activate(uint64_t b,uint32_t p,uint32_t f,const char *s){(void)b;(void)p;(void)f;(void)s;CHECK(false);return false;}
void sha256_hash(const void *p,size_t n,uint8_t out[32]){(void)p;(void)n;(void)out;CHECK(false);}
static unsigned log_lines;
int kprintf(const char *fmt,...){(void)fmt;log_lines++;return 0;} /* resource() reports the first rejection per load */
static void init(void){
    memset(&R,0,sizeof(R));memset(&boot,0,sizeof(boot));memset(config,0,sizeof(config));reads=mutate_at=0;boot_present=true;
    R.device=(pci_device_t){.bus=1,.vendor_id=0x1002,.device_id=0x73ff,.class_id=3};
    config[0]=0x73ff1002;config[1]=2;config[2]=0x03000000;
    config[4]=0x0c;config[5]=8;config[9]=0xb0000000;
    boot.gpu_bar_address[0]=0x800000000ULL;boot.gpu_bar_bytes[0]=0x200000000ULL;
    boot.gpu_bar_address[5]=0xb0000000;boot.gpu_bar_bytes[5]=0x100000;
    uint32_t raw[6];uint64_t base[6],bytes[6];read_bars(&R.device,raw);firmware_ranges(&boot,base,bytes);
    CHECK(gpu_pci_resource_decode(raw,base,bytes,0,&R.resources[0]));
    CHECK(gpu_pci_resource_decode(raw,base,bytes,5,&R.resources[5]));
    R.resources[5].flags|=8;R.bar[5]=0xb0000000;R.bar_bytes[5]=0x100000;
    R.services.abi=2;R.services.size=112;R.services.service_context=&R;R.services.resource=resource;reads=0;
}
static bool call(unsigned bar,nexis_gpu_resource *out){return R.services.resource(R.services.service_context,bar,out);}
static void rejected(unsigned bar){nexis_gpu_resource out,zero={0};memset(&out,0xa5,sizeof(out));CHECK(!call(bar,&out) && !memcmp(&out,&zero,sizeof(out)));cases++;}
int main(void){
    nexis_gpu_resource out;
    init();CHECK(call(0,&out) && out.base==0x800000000ULL && out.bytes==0x200000000ULL && out.flags==7 && !out.reserved);unsigned ops=reads;cases++;
    init();CHECK(call(5,&out) && out.flags==9 && out.bytes==0x100000);cases++;
    for(unsigned fault=0;fault<13;fault++){
        init();switch(fault){
            case 0:boot_present=false;break;case 1:config[0]^=1;break;case 2:config[1]=0;break;case 3:config[3]=1u<<16;break;
            case 4:config[4]=0x1000000c;break;case 5:config[5]++;break;case 6:boot.gpu_bar_bytes[0]/=2;break;
            case 7:boot.gpu_bar_address[0]+=4096;break;case 8:R.resources[0].flags^=4;break;
            case 9:R.services.service_context=(void *)1;break;case 10:config[4]=0x08;break;case 11:config[5]=0;break;
            case 12:config[2]=0x02000000;break;
        }rejected(0);
    }
    /* BAR mutation anywhere in either sampled PCI config sweep is rejected. */
    for(unsigned n=1;n<=ops;n++){init();mutate_at=n;rejected(0);}
    for(unsigned n=1;n<5;n++){init();rejected(n);}
    init();rejected(6);CHECK(!call(0,NULL));cases++;
    /* A rejection is explained once in the kernel log, not once per register transaction. */
    init();log_lines=0;config[1]=0;rejected(0);rejected(0);rejected(5);CHECK(log_lines==1);cases++;
    init();log_lines=0;CHECK(call(0,&out) && call(5,&out) && !log_lines);cases++;
    uint64_t address;
    init();CHECK(register_address(&R,5,0xffffc,&address) && address==0xb00ffffc);cases++;
    CHECK(!register_address(&R,5,0x100000,&address) && !register_address(&R,5,2,&address) &&
        !register_address(&R,0,0,&address) && !register_address((void *)1,5,0,&address) && !register_address(&R,6,0,&address));cases++;
    printf("{\"passed\":true,\"cases\":%u,\"actual_kernel_resource_callback\":true,\"pci_access_mode\":\"modeled_read_only\",\"physical_hardware_verified\":false}\n",cases);return 0;
}
