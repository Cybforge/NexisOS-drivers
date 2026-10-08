/* Native DCN3 dpp1_dscl_set_scaler_manual_scale/dpp1_dscl_set_lb float path
 * and dpp3 cursor enable controls. Pinned AMD Linux v6.12 MIT definitions
 * in generated header retain the original notice. No scanout activation. */
#include "dcn302_dpp.h"
#include <string.h>
#define GET(v,f) (((v)&DCN302_DPP_##f##_MASK)>>DCN302_DPP_##f##_SHIFT)
static bool separate(const void *p,size_t bytes,const dcn302_dpp_transaction *t){
    uintptr_t a=(uintptr_t)p,b=(uintptr_t)t;
    return p && a<=UINTPTR_MAX-bytes && b<=UINTPTR_MAX-sizeof(*t) && (a<b?b-a>=bytes:a-b>=sizeof(*t));
}
static bool io_valid(const dcn302_io *io){return io && io->read && io->write;}
static uint32_t normalized(unsigned r,uint32_t v){return v&~dcn302_dpp_readonly[r];}
static bool rd(const dcn302_io *io,unsigned p,unsigned r,uint32_t *v){return io->read(io->context,dcn302_dpp_register_bytes[p][r],v);}
static enum dcn302_error powered(const dcn302_io *io,unsigned p){
    uint32_t clock,state,local;
    if(!rd(io,p,DCN302_DPP_R_CLOCK,&clock) || !rd(io,p,DCN302_DPP_R_STATUS,&state) ||
       !rd(io,p,DCN302_DPP_R_LOCAL_CLOCK,&local))return DCN302_IO;
    return (clock&DCN302_DPP_CLOCK_REQUIRED_MASK)==DCN302_DPP_CLOCK_REQUIRED_MASK &&
        (local&DCN302_DPP_LOCAL_CLOCK_ENABLE_MASK) && !(local&DCN302_DPP_LOCAL_TEST_MASK) &&
        !(state&DCN302_DPP_LB_POWER_MASK)?DCN302_OK:DCN302_BUSY;
}
static enum dcn302_error snapshot(const dcn302_io *io,unsigned p,uint32_t *out){
    for(unsigned r=0;r<DCN302_DPP_REGISTER_COUNT;r++){
        if(!rd(io,p,r,&out[r]))return DCN302_IO;
        out[r]=normalized(r,out[r]);
    }
    return DCN302_OK;
}
static enum dcn302_error encode(const nexis_gpu_timing *t,const uint32_t *before,uint32_t *after){
    if(!t || !t->hactive || !t->vactive || t->hactive>16383 || t->vactive>16383)return DCN302_INPUT;
    /* Viewport fields have the same native14-bit geometry as recout/MPC. */
    if((before[DCN302_DPP_R_VIEW_START]&(DCN302_DPP_VIEW_X_MASK|DCN302_DPP_VIEW_Y_MASK)) ||
       GET(before[DCN302_DPP_R_VIEW_SIZE],VIEW_W)!=t->hactive || GET(before[DCN302_DPP_R_VIEW_SIZE],VIEW_H)!=t->vactive)return DCN302_UNSUPPORTED;
    memcpy(after,before,DCN302_DPP_REGISTER_COUNT*sizeof(*after));
    for(unsigned r=0;r<DCN302_DPP_PROGRAM_COUNT;r++)after[r]&=~dcn302_dpp_owned[r];
    /* DCN3 CNV RGB8888 and identity R/G/B crossbar. HUBP's independently
     * verified memory crossbar already handles the two CPU pixel formats.
     * Whole CM bypass and unity pre-degamma avoid stale firmware LUT/CSC.
     * No DGAM register: that older DCN1 block does not exist on Navi23. */
    after[DCN302_DPP_R_PIXEL_FORMAT]|=8u<<DCN302_DPP_PIXEL_FORMAT_SHIFT;
    after[DCN302_DPP_R_FORMAT]|=1u<<DCN302_DPP_FORMAT_G_SHIFT|2u<<DCN302_DPP_FORMAT_B_SHIFT;
    after[DCN302_DPP_R_CM]|=DCN302_DPP_CM_BYPASS_MASK;
    after[DCN302_DPP_R_RECOUT_SIZE]|=t->vactive<<DCN302_DPP_RECOUT_H_SHIFT|t->hactive<<DCN302_DPP_RECOUT_W_SHIFT;
    after[DCN302_DPP_R_MPC_SIZE]|=t->vactive<<DCN302_DPP_MPC_H_SHIFT|t->hactive<<DCN302_DPP_MPC_W_SHIFT;
    /* Native maximum LB config0 corresponds to DML's full789504-bit buffer.
     * 2744 units, ceil(width/6) units per line, always>=1 for14-bit RGB1:1.
     * Selecting config1 merely because a tap fits would overstate DML size. */
    after[DCN302_DPP_R_LB_MEMORY]|=63u<<DCN302_DPP_LB_MAX_SHIFT;
    return DCN302_OK;
}
enum dcn302_error dcn302_dpp_prepare(const dcn302_io *io,unsigned hubp,const nexis_gpu_timing *timing,
    bool (*guard)(void *),dcn302_dpp_transaction *t){
    if(!t || (uintptr_t)t%_Alignof(dcn302_dpp_transaction) || (uintptr_t)io%_Alignof(dcn302_io) ||
       (uintptr_t)timing%_Alignof(nexis_gpu_timing) || !separate(io,sizeof(*io),t) || !separate(timing,sizeof(*timing),t))return DCN302_INPUT;
    memset(t,0,sizeof(*t));
    if(!io_valid(io) || hubp>=5 || !guard || !timing->hactive || !timing->vactive || timing->hactive>16383 || timing->vactive>16383)return t->error=DCN302_INPUT;
    enum dcn302_error e=powered(io,hubp);if(e)return t->error=e;
    uint32_t second[DCN302_DPP_REGISTER_COUNT];e=snapshot(io,hubp,t->before);if(e)return t->error=e;
    e=snapshot(io,hubp,second);if(e)return t->error=e;
    if(memcmp(second,t->before,sizeof(second)))return t->error=DCN302_READBACK;
    e=encode(timing,t->before,t->after);if(e)return t->error=e;
    e=powered(io,hubp);if(e)return t->error=e;
    t->owner=*io;t->guard=guard;t->timing=*timing;t->hubp=hubp;t->prepared=true;return t->error=DCN302_OK;
}
static bool usable(const dcn302_io *io,const dcn302_dpp_transaction *t){
    if(!t || (uintptr_t)t%_Alignof(dcn302_dpp_transaction) || (uintptr_t)io%_Alignof(dcn302_io) || !separate(io,sizeof(*io),t) ||
       !io_valid(io) || !t->prepared || !t->guard || t->hubp>=5 || t->touched>>DCN302_DPP_PROGRAM_COUNT ||
       io->context!=t->owner.context || io->read!=t->owner.read || io->write!=t->owner.write || io->delay_us!=t->owner.delay_us)return false;
    uint32_t target[DCN302_DPP_REGISTER_COUNT];
    if(encode(&t->timing,t->before,target) || memcmp(target,t->after,sizeof(target)))return false;
    for(unsigned r=0;r<DCN302_DPP_REGISTER_COUNT;r++)if(t->before[r]&dcn302_dpp_readonly[r])return false;
    return true;
}
static enum dcn302_error quiet(const dcn302_io *io,const dcn302_dpp_transaction *t){
    for(unsigned sweep=0;sweep<2;sweep++){
        if(!t->guard(io->context))return DCN302_BUSY;
        enum dcn302_error e=powered(io,t->hubp);if(e)return e;
        for(unsigned p=0;p<5;p++){
            uint32_t c,k,v;
            if(!io->read(io->context,dcn302_register_bytes[p][DCN302_R_CONTROL],&c) ||
               !io->read(io->context,dcn302_register_bytes[p][DCN302_R_CLOCK],&k) ||
               !io->read(io->context,dcn302_register_bytes[p][DCN302_R_VTG],&v))return DCN302_IO;
            if((c&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) || (k&DCN302_BUSY_MASK) || (v&DCN302_VTG_ENABLE_MASK))return DCN302_BUSY;
        }
        for(unsigned r=DCN302_DPP_PROGRAM_COUNT;r<DCN302_DPP_REGISTER_COUNT;r++){
            uint32_t v;if(!rd(io,t->hubp,r,&v))return DCN302_IO;
            if(normalized(r,v)!=t->before[r])return DCN302_READBACK;
        }
    }
    return DCN302_OK;
}
static enum dcn302_error verify(const dcn302_io *io,const dcn302_dpp_transaction *t,const uint32_t *target){
    uint32_t v[DCN302_DPP_REGISTER_COUNT];enum dcn302_error e=snapshot(io,t->hubp,v);
    return e?e:memcmp(v,target,sizeof(v))?DCN302_READBACK:DCN302_OK;
}
static enum dcn302_error program(const dcn302_io *io,dcn302_dpp_transaction *t,unsigned r,const uint32_t *target){
    enum dcn302_error e=quiet(io,t);if(e)return e;
    uint32_t v,mask=dcn302_dpp_owned[r];if(!rd(io,t->hubp,r,&v))return DCN302_IO;
    if((normalized(r,v)^target[r])&~mask)return DCN302_READBACK;
    if((v&mask)!=(target[r]&mask)){
        t->dirty=true;t->touched|=1u<<r;
        uint32_t value=(v&~(mask|dcn302_dpp_readonly[r]))|(target[r]&mask);
        if(!io->write(io->context,dcn302_dpp_register_bytes[t->hubp][r],value))return DCN302_IO;
        if(!rd(io,t->hubp,r,&v))return DCN302_IO;
        if(normalized(r,v)!=target[r])return DCN302_READBACK;
    }
    return quiet(io,t);
}
enum dcn302_error dcn302_dpp_verify_installed(const dcn302_io *io,const dcn302_dpp_transaction *t){
    if(!usable(io,t))return DCN302_INPUT;
    if(!t->applied || t->poisoned)return DCN302_BUSY;
    enum dcn302_error e=quiet(io,t);if(!e)e=verify(io,t,t->after);return e?e:quiet(io,t);
}
enum dcn302_error dcn302_dpp_restore_disabled(const dcn302_io *io,dcn302_dpp_transaction *t){
    if(!usable(io,t))return DCN302_INPUT;
    enum dcn302_error e=quiet(io,t);
    /* Reverse order restores old scaler/LB before re-enabling old cursors. */
    if(!e)for(unsigned n=DCN302_DPP_PROGRAM_COUNT;n;n--){unsigned r=n-1;if(!(t->touched&(1u<<r)))continue;e=program(io,t,r,t->before);if(e)break;}
    if(!e)e=verify(io,t,t->before);
    if(!e)e=quiet(io,t);
    if(e){t->poisoned=true;return t->error=DCN302_ROLLBACK;}
    t->touched=0;t->dirty=t->applied=t->poisoned=false;return t->error=DCN302_OK;
}
enum dcn302_error dcn302_dpp_apply_disabled(const dcn302_io *io,dcn302_dpp_transaction *t){
    if(!usable(io,t))return DCN302_INPUT;
    if(t->dirty || t->applied || t->poisoned || t->touched)return t->error=DCN302_BUSY;
    enum dcn302_error e=quiet(io,t);if(e)return t->error=e;
    e=verify(io,t,t->before);if(e)return t->error=e;
    for(unsigned r=0;r<DCN302_DPP_PROGRAM_COUNT;r++){e=program(io,t,r,t->after);if(e)break;}
    if(!e)e=verify(io,t,t->after);
    if(!e)e=quiet(io,t);
    if(e){if(t->dirty && dcn302_dpp_restore_disabled(io,t))return DCN302_ROLLBACK;return t->error=e;}
    t->applied=true;return t->error=DCN302_OK;
}
