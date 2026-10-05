#include "packages.h"
#include "pins.h"
#include "../pci.h"
#include "../framebuffer.h"
#include "../serial.h"
#include "../../net/http.h"
#include "../../net/net.h"
#include "../../fs/vfs.h"
#include "../../mm/heap.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../security/sha256.h"
#include "../../include/string.h"
#include "../../include/io.h"
#include "../../sys/cmdline.h"
#include "../../lib/coop.h"
#include "../../arch/x86_64/pit.h"
#include "../../../gui/wm/wm.h"
#include "../../../tools/gpu-driver/include/nexis_gpu.h"
#define MODULE_VA 0xffffa00000000000ULL
#define CACHE "/opt/nexis-drivers/bochs.ndrv"
static pci_device_t *device;
static http_request_t *request;
static bool selected,active,finished,polling;
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t bar(pci_device_t *d,unsigned index){
 uint32_t lo=pci_read_dword(d->bus,d->slot,d->func,0x10+index*4);if(!lo || lo&1)return 0;
 if((lo&6)==4){if(index>=5)return 0;return (uint64_t)pci_read_dword(d->bus,d->slot,d->func,0x14+index*4)<<32|(lo&~15u);}
 if(lo&6)return 0;return lo&~15u;
}
static bool valid(const uint8_t *data,size_t size){
 if(!data || size<=16 || size>65552 || memcmp(data,"NDRV",4) || u32(data+4)!=NEXIS_GPU_ABI || u32(data+12)!=size-16 || u32(data+8)>=size-16)return false;
 uint8_t digest[32];sha256_hash(data,size,digest);return !memcmp(digest,bochs_sha256,32);
}
static int activate(uint64_t base,uint32_t pitch,uint32_t format,const char *name){
 bool ok=fb_native_activate(base,pitch,format,name);
 if(!ok)kprintf("[GPU] Framebuffer attach rejected: base %llx/%llx pitch %u/%u format %u name %u back %p size %llu\n",base,fb_front_base(),pitch,fb_get_pitch(),format,name?(unsigned)(uint8_t)*name:0,fb_get_backbuffer(),fb_front_size());
 return ok?0:-1;
}
static uint8_t *cached(size_t *size){
 vfs_node_t *node=vfs_lookup(CACHE);if(!node || node->flags!=VFS_FILE || node->length<=16 || node->length>65552 || !vfs_read_lock(node))return NULL;
 uint8_t *data=vfs_read_file(CACHE,size);vfs_read_unlock(node);return data;
}
void *gpu_install_payload(size_t *size){
 if(!device)return NULL;uint8_t *data=cached(size);if(data && !valid(data,*size)){kfree(data);return NULL;}return data;
}
static bool load(const uint8_t *data,size_t size){
 if(!valid(data,size) || !device)return false;
 uint64_t aperture=bar(device,0),regs=bar(device,2);
 /* Version 1 only takes over the known firmware aperture, with unchanged
  * geometry. Never treat an arbitrary PCI BAR as writable system memory. */
 if(!aperture || aperture!=fb_front_base() || !regs || regs<0x10000000 || regs>=1ULL<<47 || regs&4095 ||
    !(pci_read_word(device->bus,device->slot,device->func,4)&2)){kprintf("[GPU] Incompatible aperture %llx (firmware %llx), registers %llx\n",aperture,fb_front_base(),regs);return false;}
 if(!vmm_map_mmio(regs,4096)){kprintf("[GPU] Register map failed\n");return false;}
 size_t bytes=size-16,pages=(bytes+4095)/4096;uint64_t phys=pmm_alloc_pages(pages);if(!phys)return false;
 bool ok=true;size_t mapped=0;
 uint64_t flags;__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
 uint64_t original=read_cr3();vmm_switch_pml4(vmm_get_kernel_pml4());
 for(size_t i=0;i<pages;i++)if(!vmm_map_page(vmm_get_kernel_pml4(),MODULE_VA+i*4096,phys+i*4096,PAGE_PRESENT|PAGE_WRITABLE|PAGE_NO_EXECUTE)){ok=false;break;}else mapped++;
 if(ok){
  memcpy((void *)(uintptr_t)MODULE_VA,data+16,bytes);
  for(size_t i=0;i<pages;i++)if(!vmm_map_page(vmm_get_kernel_pml4(),MODULE_VA+i*4096,phys+i*4096,PAGE_PRESENT)){ok=false;break;}
 }
 if(ok){
  nexis_gpu_context c={NEXIS_GPU_ABI,fb_get_width(),fb_get_height(),fb_get_pitch(),fb_is_rgb()?0:1,0,aperture,fb_front_size(),regs,activate};
  int (*entry)(const nexis_gpu_context *)=(void *)(uintptr_t)(MODULE_VA+u32(data+8));int result=entry(&c);ok=result==0;if(!ok)kprintf("[GPU] Module initialization returned %d\n",result);
 }
 for(size_t i=0;i<mapped;i++)vmm_unmap_page(vmm_get_kernel_pml4(),MODULE_VA+i*4096);
 for(size_t i=0;i<pages;i++)pmm_free_page(phys+i*4096);
 write_cr3(original);if(flags&(1u<<9))sti();
 if(ok){wm_damage_all();kprintf("[GPU] Native driver active: %s (external SHA-256 verified module)\n",fb_driver_name());}
 return ok;
}
void gpu_packages_poll(void){
 if(polling || finished)return;polling=true;
 if(!selected){
  selected=true;
  if(!cmdline_has("nogpudriver"))for(pci_device_t *d=pci_get_device_list();d;d=d->next)if(d->class_id==3 && d->vendor_id==0x1234 && d->device_id==0x1111 && bar(d,0)==fb_front_base()){device=d;break;}
  if(!device){finished=true;polling=false;return;}
  size_t size=0;uint8_t *cache=cached(&size);
  if(cache){active=load(cache,size);kfree(cache);if(active){finished=true;polling=false;return;}kprintf("[GPU] Cached driver does not match this kernel/device; downloading pinned version\n");}
 }
 if(!request){
  if(!net_is_configured()){polling=false;return;}
  http_options_t opts={0};opts.url=cmdline_has("gpudriverlocal")?"http://10.0.2.2:8930/bochs.ndrv":BOCHS_DRIVER_URL;
  opts.max_body=65552;opts.timeout_ms=15000;opts.no_cookies=true;opts.identity_encoding=true;request=http_request_start(&opts);
  if(!request){finished=true;polling=false;return;}
  kprintf("[GPU] Background download for PCI 1234:1111, not a Store listing\n");
 }
 if(request){
  http_state_t state=http_request_poll(request);
  if(state!=HTTP_PENDING){
   size_t size=0;const uint8_t *data=http_body(request,&size);
   if(state==HTTP_DONE && http_status(request)==200 && valid(data,size)){
    /* Cache before executing. The installed root binds /opt persistently;
     * live sessions keep this file in RAM, so every boot downloads again. */
    if(vfs_write_file(CACHE,data,size)){vfs_node_t *n=vfs_lookup(CACHE);if(n){vfs_chmod_node(n,0644);vfs_sync_node(n);}active=load(data,size);}else kprintf("[GPU] Cannot write driver cache\n");
   }else kprintf("[GPU] Download failed or checksum mismatch (HTTP %d, %u bytes)\n",http_status(request),(unsigned)size);
   if(!active)kprintf("[GPU] External driver unavailable/rejected; firmware display retained\n");
   http_request_free(request);request=NULL;finished=true;
  }
 }
 polling=false;
}
bool gpu_driver_active(void){return active;}
void gpu_prepare_install(void){
 /* Complete the selected download before the installer copies its verified cache to /opt. Keep
  * installation usable offline; a missing driver is retried on the next boot. */
 uint64_t end=pit_get_ticks()+5000;
 do{gpu_packages_poll();if(finished)break;net_poll();coop_yield_check();}while(pit_get_ticks()<end);
 if(request){http_request_free(request);request=NULL;finished=true;kprintf("[GPU] Installer: download deferred until next boot\n");}
}
