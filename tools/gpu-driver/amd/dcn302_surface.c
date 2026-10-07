/* Original MIT-licensed Navi23 scanout identity validation. Hardware layout:
 * AMD Linux v6.12 dcn20_hubp.c, dcn10_mpc.c, dcn20_hubbub.c/amdgpu_gmc.c. */
#include "dcn302_surface.h"
#include <string.h>
#define GET(v,f) (((v)&DCN302_SURFACE_##f##_MASK)>>DCN302_SURFACE_##f##_SHIFT)
static bool rd(const dcn302_io *io,unsigned i,enum dcn302_surface_register r,uint32_t *v){return io->read(io->context,dcn302_surface_register_bytes[i][r],v);}
enum dcn302_surface_error dcn302_surface_bind(const dcn302_io *io,const dcn302_route *route,uint64_t bar_base,uint64_t bar_bytes,
        uint64_t gop_base,uint64_t gop_bytes,uint32_t pitch,uint32_t format,dcn302_surface *out){
    if(!out)return DCN302_SURFACE_INPUT;
    memset(out,0,sizeof(*out));
    if(!io || !io->read || !route || route->opp>=5 || !route->shape.hactive || !route->shape.vactive ||
       !bar_base || !bar_bytes || bar_base>UINT64_MAX-bar_bytes || !gop_base || !gop_bytes ||
       gop_base>UINT64_MAX-gop_bytes || pitch<route->shape.hactive || pitch>16384 || format>1)return DCN302_SURFACE_INPUT;
    uint32_t mux,top,bottom,opp,mode;
    if(!rd(io,route->opp,DCN302_SURFACE_R_OUT_MUX,&mux))return DCN302_SURFACE_IO;
    unsigned mpcc=GET(mux,MUX);if(mpcc>=5)return DCN302_SURFACE_UNSUPPORTED;
    if(!rd(io,mpcc,DCN302_SURFACE_R_TOP,&top) || !rd(io,mpcc,DCN302_SURFACE_R_BOTTOM,&bottom) ||
       !rd(io,mpcc,DCN302_SURFACE_R_OPP,&opp) || !rd(io,mpcc,DCN302_SURFACE_R_MODE,&mode))return DCN302_SURFACE_IO;
    unsigned hubp=GET(top,TOP);
    /* Navi23 resources pair HUBP[i] with DPP[i]; MPCC TOP selects this DPP.
     * Reject additional blending layers, self-referential trees and bypass. */
    if(hubp>=5 || GET(bottom,BOTTOM)!=15 || GET(opp,OPP)!=route->opp || (GET(mode,MODE)!=1 && GET(mode,MODE)!=2))return DCN302_SURFACE_UNSUPPORTED;
    uint32_t regs[DCN302_SURFACE_REGISTER_COUNT]={0};
    for(unsigned r=DCN302_SURFACE_R_CONFIG;r<DCN302_SURFACE_REGISTER_COUNT;r++)if(!rd(io,hubp,(enum dcn302_surface_register)r,&regs[r]))return DCN302_SURFACE_IO;
    uint32_t c=regs[DCN302_SURFACE_R_CONFIG],clock=regs[DCN302_SURFACE_R_CLOCK],ctl=regs[DCN302_SURFACE_R_HUBP];
    if(GET(c,FORMAT)!=8 || GET(c,ROTATION) || GET(c,MIRROR) || GET(c,ALPHA) || GET(regs[DCN302_SURFACE_R_TILING],SW_MODE) ||
       GET(ctl,BLANK) || GET(ctl,TTU_DISABLE) || GET(ctl,UNDERFLOW) || !GET(clock,CLOCK_ENABLE) || !GET(clock,DISP_ON) || !GET(clock,DPP_ON) ||
       GET(regs[DCN302_SURFACE_R_CONTROL],DCC) || GET(regs[DCN302_SURFACE_R_CONTROL],TMZ) || GET(regs[DCN302_SURFACE_R_INUSE_HIGH],VMID))return DCN302_SURFACE_UNSUPPORTED;
    if(GET(regs[DCN302_SURFACE_R_VIEW_START],X) || GET(regs[DCN302_SURFACE_R_VIEW_START],Y) ||
       GET(regs[DCN302_SURFACE_R_VIEW_SIZE],WIDTH)!=route->shape.hactive || GET(regs[DCN302_SURFACE_R_VIEW_SIZE],HEIGHT)!=route->shape.vactive ||
       GET(regs[DCN302_SURFACE_R_PITCH],PITCH)+1!=pitch)return DCN302_SURFACE_UNSUPPORTED;
    uint32_t crossbar=regs[DCN302_SURFACE_R_CROSSBAR];
    if(GET(crossbar,GREEN)!=1 || GET(crossbar,RED)!=(format?3u:2u) || GET(crossbar,BLUE)!=(format?2u:3u))return DCN302_SURFACE_UNSUPPORTED;
    uint64_t addr=((uint64_t)GET(regs[DCN302_SURFACE_R_ADDRESS_HIGH],HIGH)<<32)|regs[DCN302_SURFACE_R_ADDRESS];
    uint64_t actual=((uint64_t)GET(regs[DCN302_SURFACE_R_INUSE_HIGH],INUSE_HIGH)<<32)|regs[DCN302_SURFACE_R_INUSE];
    if(addr!=actual)return DCN302_SURFACE_CHANGED;
    uint32_t fb_base,fb_top,fb_offset;
    if(!io->read(io->context,DCN302_FB_BASE_BYTES,&fb_base) || !io->read(io->context,DCN302_FB_TOP_BYTES,&fb_top) ||
       !io->read(io->context,DCN302_FB_OFFSET_BYTES,&fb_offset))return DCN302_SURFACE_IO;
    uint64_t start=(uint64_t)((fb_base&DCN302_FB_BASE_MASK)>>DCN302_FB_BASE_SHIFT)<<24;
    uint64_t end=((uint64_t)((fb_top&DCN302_FB_TOP_MASK)>>DCN302_FB_TOP_SHIFT)+1)<<24;
    uint64_t physical=(uint64_t)((fb_offset&DCN302_FB_OFFSET_MASK)>>DCN302_FB_OFFSET_SHIFT)<<24;
    uint64_t length=(uint64_t)pitch*4*route->shape.vactive;
    if(end<=start || !length || actual<physical)return DCN302_SURFACE_BOUNDS;
    uint64_t relative=actual-physical,capacity=end-start;
    if(relative>capacity || length>capacity-relative || relative>bar_bytes || length>bar_bytes-relative ||
       bar_base+relative!=gop_base || length>gop_bytes)return DCN302_SURFACE_BOUNDS;
    /* Whole read-only identity is sampled again; caller serializes operations.
     * INUSE high/low changes and pending page flips are not guessed away. */
    uint32_t now;
    if(!rd(io,route->opp,DCN302_SURFACE_R_OUT_MUX,&now))return DCN302_SURFACE_IO;
    if(now!=mux)return DCN302_SURFACE_CHANGED;
    enum dcn302_surface_register ids[]={DCN302_SURFACE_R_TOP,DCN302_SURFACE_R_BOTTOM,DCN302_SURFACE_R_OPP,DCN302_SURFACE_R_MODE};
    uint32_t values[]={top,bottom,opp,mode};
    for(unsigned i=0;i<4;i++){if(!rd(io,mpcc,ids[i],&now))return DCN302_SURFACE_IO;if(now!=values[i])return DCN302_SURFACE_CHANGED;}
    for(unsigned r=DCN302_SURFACE_R_CONFIG;r<DCN302_SURFACE_REGISTER_COUNT;r++){
        if(!rd(io,hubp,(enum dcn302_surface_register)r,&now))return DCN302_SURFACE_IO;
        /* Underflow/status/clock changes invalidate takeover too. */
        if(now!=regs[r])return DCN302_SURFACE_CHANGED;
    }
    uint32_t fb[3]={fb_base,fb_top,fb_offset},offsets[3]={DCN302_FB_BASE_BYTES,DCN302_FB_TOP_BYTES,DCN302_FB_OFFSET_BYTES};
    for(unsigned i=0;i<3;i++){if(!io->read(io->context,offsets[i],&now))return DCN302_SURFACE_IO;if(now!=fb[i])return DCN302_SURFACE_CHANGED;}
    *out=(dcn302_surface){mpcc,hubp,pitch,format,actual,gop_base,length};return DCN302_SURFACE_OK;
}
