#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../../tools/gpu-driver/include/nexis_gpu_v2.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
_Static_assert(sizeof(nexis_gpu_services)==104,"Module ABI layout");
static unsigned reads,cases;
static bool NEXIS_GPU_CALL read32(void *ctx,unsigned bar,uint32_t offset,uint32_t *value){CHECK(ctx==(void *)17 && bar==5 && offset==4);reads++;*value=0x73ff;return true;}
static nexis_gpu_services services={.abi=2,.size=sizeof(services),.service_context=(void *)17,.read32=read32};
static uint8_t *image;static size_t bytes;
static void w32(unsigned off,uint32_t v){for(unsigned i=0;i<4;i++)image[off+i]=(uint8_t)(v>>(8*i));}
static void run(const nexis_gpu_image *m,void **allocation,nexis_gpu_instance *instance){
    uint8_t *base=VirtualAlloc(NULL,m->memory_bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(base);
    memcpy(base,image+64,m->image_bytes);DWORD old;
    unsigned executable=(m->text_bytes+4095)&~4095u;
    CHECK(VirtualProtect(base,executable,PAGE_EXECUTE_READ,&old));
    if(executable<m->writable_offset)CHECK(VirtualProtect(base+executable,m->writable_offset-executable,PAGE_READONLY,&old));
    nexis_gpu_entry_v2 entry=(void *)(base+m->entry);memset(instance,0,sizeof(*instance));CHECK(!entry(&services,instance));
    CHECK(nexis_gpu_image_code_pointer(m,(uintptr_t)base,(uintptr_t)instance->poll));
    CHECK(nexis_gpu_image_state_pointer(m,(uintptr_t)base,(uintptr_t)instance->state,instance->state_bytes));
    CHECK(!strcmp(instance->name,"Retained loader test fixture"));*allocation=base;cases++;
}
int main(int argc,char **argv){
    CHECK(argc==2);FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length>64);bytes=(size_t)length;
    CHECK(!fseek(f,0,SEEK_SET));image=malloc(bytes);CHECK(image && fread(image,1,bytes,f)==bytes);fclose(f);
    nexis_gpu_image m;CHECK(nexis_gpu_image_parse(image,bytes,0x1002,0x73ff,&m));CHECK(!nexis_gpu_image_parse(image,bytes,0x1002,0x73df,&m));cases++;
    CHECK(nexis_gpu_image_parse(image,bytes,0x1002,0x73ff,&m));
    void *a,*b;nexis_gpu_instance first,second;run(&m,&a,&first);run(&m,&b,&second);CHECK(a!=b);
    /* Init has returned; execute retained PIC callbacks and independent BSS. */
    first.poll(first.state);first.poll(first.state);second.poll(second.state);CHECK(reads==3);
    uint32_t *one=(uint32_t *)((uint8_t *)first.state+8),*two=(uint32_t *)((uint8_t *)second.state+8);
    CHECK(one[0]==2 && one[1]==0x73ff && two[0]==1 && two[1]==0x73ff);cases++;
    CHECK(!nexis_gpu_image_code_pointer(&m,(uintptr_t)a,(uintptr_t)a+m.text_bytes));
    CHECK(!nexis_gpu_image_state_pointer(&m,(uintptr_t)a,(uintptr_t)a+m.memory_bytes,1));
    CHECK(!nexis_gpu_image_state_pointer(&m,(uintptr_t)a,(uintptr_t)a+m.writable_offset,0));
    CHECK(!nexis_gpu_image_state_pointer(&m,(uintptr_t)a,(uintptr_t)a+m.writable_offset-1,2));cases++;
    MEMORY_BASIC_INFORMATION p;CHECK(VirtualQuery(a,&p,sizeof(p)) && p.Protect==PAGE_EXECUTE_READ);
    CHECK(VirtualQuery(first.state,&p,sizeof(p)) && p.Protect==PAGE_READWRITE);cases++;
    first.shutdown(first.state);second.shutdown(second.state);CHECK(!*(void **)first.state && !*(void **)second.state);
    CHECK(VirtualFree(a,0,MEM_RELEASE) && VirtualFree(b,0,MEM_RELEASE));
    uint8_t *original=malloc(bytes);CHECK(original);memcpy(original,image,bytes);
    for(unsigned n=0;n<64;n++){
        memcpy(image,original,bytes);image[n]^=0x80;
        bool accepted=nexis_gpu_image_parse(image,bytes,0x1002,0x73ff,&m);
        if(n<8 || n>=28)CHECK(!accepted);
        /* A changed but structurally valid layout still requires the kernel's
         * pinned SHA before execution. These mutations are never executed. */
        if(accepted)CHECK(m.entry<m.text_bytes && m.text_bytes<=m.writable_offset && m.image_bytes<=m.memory_bytes);
        cases++;
    }
    for(unsigned n=0;n<64;n++){CHECK(!nexis_gpu_image_parse(original,n,0x1002,0x73ff,&m));cases++;}
    for(unsigned n=0;n<7;n++){
        memcpy(image,original,bytes);
        if(n==0)w32(8,0xffffffff);
        if(n==1)w32(12,1);
        if(n==2)w32(16,0);
        if(n==3)w32(20,4095);
        if(n==4)w32(24,0xffffffff);
        if(n==5)w32(36,0);
        if(n==6)w32(40,16);
        CHECK(!nexis_gpu_image_parse(image,bytes,0x1002,0x73ff,&m));cases++;
    }
    free(original);free(image);printf("{\"passed\":true,\"cases\":%u,\"retained_pic_callbacks\":true,\"physical_driver_tested\":false}\n",cases);return 0;
}
