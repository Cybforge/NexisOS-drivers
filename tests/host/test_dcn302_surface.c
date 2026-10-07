#include "../../tools/gpu-driver/amd/dcn302_surface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d case %u: %s\n",__LINE__,cases,#x);exit(1);}}while(0)
#define SET(v,f,n) (((v)&~DCN302_SURFACE_##f##_MASK)|(((uint32_t)(n)<<DCN302_SURFACE_##f##_SHIFT)&DCN302_SURFACE_##f##_MASK))
static unsigned cases;
typedef struct {uint32_t regs[5][DCN302_SURFACE_REGISTER_COUNT],base,top,offset;unsigned ops,fail,change_at;uint32_t change_offset,change_value;bool invalid;} model;
static uint32_t *locate(model *m,uint32_t offset){
    for(unsigned i=0;i<5;i++)for(unsigned r=0;r<DCN302_SURFACE_REGISTER_COUNT;r++)if(offset==dcn302_surface_register_bytes[i][r])return &m->regs[i][r];
    if(offset==DCN302_FB_BASE_BYTES)return &m->base;
    if(offset==DCN302_FB_TOP_BYTES)return &m->top;
    if(offset==DCN302_FB_OFFSET_BYTES)return &m->offset;
    m->invalid=true;return NULL;
}
static bool rd(void *c,uint32_t offset,uint32_t *out){
    model *m=c;if(++m->ops==m->fail)return false;
    if(m->ops==m->change_at){uint32_t *p=locate(m,m->change_offset);CHECK(p);*p=m->change_value;}
    uint32_t *p=locate(m,offset);if(!p)return false;*out=*p;return true;
}
static const uint64_t bar=0x0000000800000000ULL,bar_bytes=0x10000000,gop=0x0000000800200000ULL;
static const uint64_t physical=0x0000008000000000ULL,gop_bytes=2048u*4*1080;
static void init(model *m,dcn302_io *io,dcn302_route *route,unsigned opp,unsigned mpcc,unsigned hubp,unsigned format){
    memset(m,0,sizeof(*m));*io=(dcn302_io){m,rd,NULL,NULL};memset(route,0,sizeof(*route));route->opp=opp;
    route->shape.hactive=1920;route->shape.vactive=1080;
    m->regs[opp][DCN302_SURFACE_R_OUT_MUX]=mpcc;
    m->regs[mpcc][DCN302_SURFACE_R_TOP]=hubp;m->regs[mpcc][DCN302_SURFACE_R_BOTTOM]=15;
    m->regs[mpcc][DCN302_SURFACE_R_OPP]=opp;m->regs[mpcc][DCN302_SURFACE_R_MODE]=2;
    uint32_t *r=m->regs[hubp];r[DCN302_SURFACE_R_CONFIG]=8;r[DCN302_SURFACE_R_VIEW_SIZE]=SET(SET(0,WIDTH,1920),HEIGHT,1080);
    r[DCN302_SURFACE_R_CLOCK]=DCN302_SURFACE_CLOCK_ENABLE_MASK|DCN302_SURFACE_DISP_ON_MASK|DCN302_SURFACE_DPP_ON_MASK;
    r[DCN302_SURFACE_R_CROSSBAR]=SET(SET(SET(0,RED,format?3:2),GREEN,1),BLUE,format?2:3);
    r[DCN302_SURFACE_R_PITCH]=2047;r[DCN302_SURFACE_R_ADDRESS]=0x200000;r[DCN302_SURFACE_R_ADDRESS_HIGH]=(uint32_t)(physical>>32);
    r[DCN302_SURFACE_R_INUSE]=0x200000;r[DCN302_SURFACE_R_INUSE_HIGH]=(uint32_t)(physical>>32);
    m->base=0x9000;m->top=0x91ff;m->offset=(uint32_t)(physical>>24);
}
static enum dcn302_surface_error bind(model *m,dcn302_io *io,dcn302_route *r,unsigned format,dcn302_surface *out){
    enum dcn302_surface_error e=dcn302_surface_bind(io,r,bar,bar_bytes,gop,gop_bytes,2048,format,out);CHECK(!m->invalid);return e;
}
static void normal(void){
    model m;dcn302_io io;dcn302_route r;dcn302_surface s;
    for(unsigned opp=0;opp<5;opp++)for(unsigned mpcc=0;mpcc<5;mpcc++)for(unsigned hubp=0;hubp<5;hubp++)for(unsigned format=0;format<2;format++){
        init(&m,&io,&r,opp,mpcc,hubp,format);CHECK(bind(&m,&io,&r,format,&s)==DCN302_SURFACE_OK);
        CHECK(s.mpcc==mpcc && s.hubp==hubp && s.format==format && s.pitch==2048 && s.cpu_address==gop && s.gpu_address==physical+0x200000 && s.bytes==gop_bytes);cases++;
    }
    /* Resizable BAR is independently bounded; no 256-MB assumption. */
    init(&m,&io,&r,0,2,4,1);CHECK(dcn302_surface_bind(&io,&r,bar,0x200000000ULL,gop,gop_bytes,2048,1,&s)==DCN302_SURFACE_OK);cases++;
    /* Identity physical/memory-controller address is allowed but not required. */
    init(&m,&io,&r,0,2,4,1);m.base=m.offset;m.top=m.base+511;CHECK(bind(&m,&io,&r,1,&s)==DCN302_SURFACE_OK);cases++;
}
static void invalid(void){
    model m;dcn302_io io;dcn302_route r;dcn302_surface s,zero={0};
    for(unsigned kind=0;kind<27;kind++){
        init(&m,&io,&r,0,2,4,1);uint32_t *h=m.regs[4];
        switch(kind){
            case 0:m.regs[0][DCN302_SURFACE_R_OUT_MUX]=15;break;
            case 1:m.regs[2][DCN302_SURFACE_R_TOP]=15;break;case 2:m.regs[2][DCN302_SURFACE_R_BOTTOM]=2;break;
            case 3:m.regs[2][DCN302_SURFACE_R_OPP]=1;break;case 4:m.regs[2][DCN302_SURFACE_R_MODE]=3;break;
            case 5:h[DCN302_SURFACE_R_CONFIG]=10;break;case 6:h[DCN302_SURFACE_R_CONFIG]|=DCN302_SURFACE_ROTATION_MASK;break;
            case 7:h[DCN302_SURFACE_R_CONFIG]|=DCN302_SURFACE_MIRROR_MASK;break;case 8:h[DCN302_SURFACE_R_CONFIG]|=DCN302_SURFACE_ALPHA_MASK;break;
            case 9:h[DCN302_SURFACE_R_TILING]=1;break;case 10:h[DCN302_SURFACE_R_HUBP]=DCN302_SURFACE_BLANK_MASK;break;
            case 11:h[DCN302_SURFACE_R_HUBP]=DCN302_SURFACE_TTU_DISABLE_MASK;break;case 12:h[DCN302_SURFACE_R_HUBP]=DCN302_SURFACE_UNDERFLOW_MASK;break;
            case 13:h[DCN302_SURFACE_R_CLOCK]=0;break;case 14:h[DCN302_SURFACE_R_CLOCK]&=~DCN302_SURFACE_DPP_ON_MASK;break;
            case 15:h[DCN302_SURFACE_R_CONTROL]=DCN302_SURFACE_DCC_MASK;break;case 16:h[DCN302_SURFACE_R_CONTROL]=DCN302_SURFACE_TMZ_MASK;break;
            case 17:h[DCN302_SURFACE_R_INUSE_HIGH]|=DCN302_SURFACE_VMID_MASK;break;case 18:h[DCN302_SURFACE_R_VIEW_START]=1;break;
            case 19:h[DCN302_SURFACE_R_VIEW_SIZE]=0;break;case 20:h[DCN302_SURFACE_R_PITCH]++;break;
            case 21:h[DCN302_SURFACE_R_CROSSBAR]=0;break;case 22:h[DCN302_SURFACE_R_ADDRESS]++;break;
            case 23:m.top=m.base-1;break;case 24:m.offset++;break;
            case 25:h[DCN302_SURFACE_R_ADDRESS]=h[DCN302_SURFACE_R_INUSE]=0x0fc00000;break;
            case 26:m.top=m.base;h[DCN302_SURFACE_R_ADDRESS]=h[DCN302_SURFACE_R_INUSE]=0x1000000;break;
        }
        memset(&s,0xa5,sizeof(s));CHECK(bind(&m,&io,&r,1,&s)!=DCN302_SURFACE_OK && !memcmp(&s,&zero,sizeof(s)));cases++;
    }
    init(&m,&io,&r,0,2,4,1);
    CHECK(dcn302_surface_bind(&io,&r,UINT64_MAX-1,bar_bytes,gop,gop_bytes,2048,1,&s)==DCN302_SURFACE_INPUT);cases++;
    CHECK(dcn302_surface_bind(&io,&r,bar,bar_bytes,gop,gop_bytes-1,2048,1,&s)==DCN302_SURFACE_BOUNDS);cases++;
    CHECK(dcn302_surface_bind(&io,&r,bar,bar_bytes,gop+4,gop_bytes,2048,1,&s)==DCN302_SURFACE_BOUNDS);cases++;
    CHECK(dcn302_surface_bind(&io,&r,bar,bar_bytes,gop,gop_bytes,1919,1,&s)==DCN302_SURFACE_INPUT);cases++;
    CHECK(dcn302_surface_bind(&io,&r,bar,bar_bytes,gop,gop_bytes,2048,2,&s)==DCN302_SURFACE_INPUT);cases++;
    init(&m,&io,&r,0,2,4,1);CHECK(bind(&m,&io,&r,1,&s)==DCN302_SURFACE_OK);unsigned ops=m.ops;
    for(unsigned op=1;op<=ops;op++){
        init(&m,&io,&r,0,2,4,1);m.fail=op;CHECK(bind(&m,&io,&r,1,&s)==DCN302_SURFACE_IO && !memcmp(&s,&zero,sizeof(s)));cases++;
    }
    for(unsigned kind=0;kind<4;kind++){
        init(&m,&io,&r,0,2,4,1);m.change_at=22;
        switch(kind){
            case 0:m.change_offset=dcn302_surface_register_bytes[0][DCN302_SURFACE_R_OUT_MUX];m.change_value=4;break;
            case 1:m.change_offset=dcn302_surface_register_bytes[4][DCN302_SURFACE_R_INUSE];m.change_value=0x220000;break;
            case 2:m.change_offset=DCN302_FB_OFFSET_BYTES;m.change_value=m.offset+1;break;
            case 3:m.change_offset=dcn302_surface_register_bytes[2][DCN302_SURFACE_R_BOTTOM];m.change_value=0;break;
        }
        CHECK(bind(&m,&io,&r,1,&s)==DCN302_SURFACE_CHANGED && !memcmp(&s,&zero,sizeof(s)));cases++;
    }
}
int main(void){normal();invalid();printf("{\"passed\":true,\"cases\":%u,\"native_surface_mapping_proof\":true,\"gop_clock_used\":false,\"hardware_writes\":false,\"physical_hardware_verified\":false,\"physical_card_driver\":false}\n",cases);return 0;}
