/* AMD DCN3.02 native requestor/deadline/TTU programming. Register and field
 * maps are generated from pinned AMD Linux v6.12 sources; see the generated
 * header for the AMD MIT notice. No emulated-device or firmware mode API. */
#include "dcn302_hubp.h"
#include <string.h>
#define CONTROL DCN302_HUBP_R_DCHUBP_CNTL
#define BLANK (DCN302_HUBP_HUBP_BLANK_EN_MASK|DCN302_HUBP_HUBP_TTU_DISABLE_MASK)
#define CLOCKS (DCN302_HUBP_HUBP_CLOCK_ENABLE_MASK|DCN302_HUBP_HUBP_DISPCLK_R_CLOCK_ON_MASK|DCN302_HUBP_HUBP_DPPCLK_G_CLOCK_ON_MASK|DCN302_HUBP_HUBP_DCFCLK_R_CLOCK_ON_MASK|DCN302_HUBP_HUBP_DCFCLK_G_CLOCK_ON_MASK)
_Static_assert(DCN302_HUBP_REGISTER_COUNT<64,"touched register bitmap");
static bool io_valid(const dcn302_io *io){return io && io->read && io->write;}
static bool separate(const void *a,size_t bytes,const dcn302_hubp_transaction *t){
    uintptr_t p=(uintptr_t)a,q=(uintptr_t)t;
    if(!a || p>UINTPTR_MAX-bytes || q>UINTPTR_MAX-sizeof(*t))return false;
    return p<q?q-p>=bytes:p-q>=sizeof(*t);
}
static uint32_t config_mask(unsigned r){
    return ~(dcn302_hubp_forbidden[r]|dcn302_hubp_readonly[r]|(r==CONTROL?BLANK:0));
}
static bool timing_valid(const nexis_gpu_timing *t){
    return t && t->pixel_khz>=10000 && t->pixel_khz<=600000 && !(t->flags&~3u) &&
        t->hactive>=64 && t->hactive<=4096 && t->hactive<t->hsync_start &&
        t->hsync_start<t->hsync_end && t->hsync_end<t->htotal && t->htotal<=8192 &&
        t->vactive>=64 && t->vactive<=4096 && t->vactive<t->vsync_start &&
        t->vsync_start<t->vsync_end && t->vsync_end<t->vtotal && t->vtotal<=8192 &&
        t->vtotal-t->vactive>=16;
}
static enum dcn302_hubp_error encode(const nexis_gpu_timing *t,const dcn302_dml_output *o,uint32_t *values){
    if(!timing_valid(t) || !o || !o->disp_khz || o->disp_khz>4000000 || !o->dpp_khz || o->dpp_khz>4000000 ||
       !o->vstartup || o->vstartup>=t->vtotal-t->vactive)
        return DCN302_HUBP_INPUT;
    memset(values,0,DCN302_HUBP_REGISTER_COUNT*sizeof(*values));
    for(unsigned n=0;n<DCN302_HUBP_FIELD_COUNT;n++){
        const dcn302_hubp_field *f=&dcn302_hubp_fields[n];uint32_t v;
        memcpy(&v,(const unsigned char *)o+f->offset,sizeof(v));
        if(v>(f->mask>>f->shift))return DCN302_HUBP_RANGE;
        values[f->reg]|=v<<f->shift;
    }
    uint64_t offset=(uint64_t)o->vready_offset+o->vupdate_width+o->vupdate_offset;
    uint64_t lines=offset/t->htotal;
    /* Upstream expression is unsigned; reject underflow rather than letting
     * malformed global sync wrap and silently choose the opposite VREADY. */
    if(lines>o->vstartup)return DCN302_HUBP_RANGE;
    if(o->vstartup-lines<=t->vtotal-t->vsync_start)
        values[CONTROL]=DCN302_HUBP_HUBP_VREADY_AT_OR_AFTER_VSYNC_MASK;
    values[DCN302_HUBP_R_HUBPREQ_DEBUG_DB]=1u<<8;
    values[DCN302_HUBP_R_HUBPREQ_DEBUG]=1u<<26; /* DEDCN21-133 */
    return DCN302_HUBP_OK;
}
static enum dcn302_hubp_error snapshot(const dcn302_io *io,unsigned hubp,uint32_t *out){
    for(unsigned n=0;n<DCN302_HUBP_REGISTER_COUNT;n++){
        if(!io->read(io->context,dcn302_hubp_register_bytes[hubp][n],&out[n]))return DCN302_HUBP_IO;
        out[n]&=config_mask(n);
    }
    return DCN302_HUBP_OK;
}
static enum dcn302_hubp_error powered(const dcn302_io *io,unsigned hubp){
    uint32_t clocks;
    if(!io->read(io->context,dcn302_hubp_clock_bytes[hubp],&clocks))return DCN302_HUBP_IO;
    return (clocks&CLOCKS)==CLOCKS?DCN302_HUBP_OK:DCN302_HUBP_POWER;
}
enum dcn302_hubp_error dcn302_hubp_prepare(const dcn302_io *io,unsigned hubp,const nexis_gpu_timing *timing,const dcn302_dml_output *request,dcn302_hubp_transaction *t){
    if(!t || (uintptr_t)t%_Alignof(dcn302_hubp_transaction))return DCN302_HUBP_INPUT;
    /* Input storage must not alias the output, which is cleared here. */
    if((uintptr_t)io%_Alignof(dcn302_io) || (uintptr_t)timing%_Alignof(nexis_gpu_timing) ||
       (uintptr_t)request%_Alignof(dcn302_dml_output) || !separate(io,sizeof(*io),t) || !separate(timing,sizeof(*timing),t) ||
       !separate(request,sizeof(*request),t))return DCN302_HUBP_INPUT;
    memset(t,0,sizeof(*t));
    if(!io_valid(io) || hubp>=5)return t->error=DCN302_HUBP_INPUT;
    uint32_t values[DCN302_HUBP_REGISTER_COUNT],second[DCN302_HUBP_REGISTER_COUNT];
    enum dcn302_hubp_error e=encode(timing,request,values);if(e)return t->error=e;
    e=powered(io,hubp);if(e)return t->error=e;
    e=snapshot(io,hubp,t->before);if(e)return t->error=e;
    e=snapshot(io,hubp,second);if(e)return t->error=e;
    if(memcmp(t->before,second,sizeof(second)))return t->error=DCN302_HUBP_READBACK;
    for(unsigned n=0;n<DCN302_HUBP_REGISTER_COUNT;n++)t->after[n]=(t->before[n]&~dcn302_hubp_owned[n])|values[n];
    t->owner=*io;t->hubp=hubp;t->timing=*timing;t->request=*request;t->prepared=true;
    return t->error=DCN302_HUBP_OK;
}
static enum dcn302_hubp_error quiet(const dcn302_io *io,unsigned hubp){
    for(unsigned sweep=0;sweep<2;sweep++){
        enum dcn302_hubp_error e=powered(io,hubp);if(e)return e;
        for(unsigned pipe=0;pipe<5;pipe++){
            uint32_t c,k,v;
            if(!io->read(io->context,dcn302_register_bytes[pipe][DCN302_R_CONTROL],&c) ||
               !io->read(io->context,dcn302_register_bytes[pipe][DCN302_R_CLOCK],&k) ||
               !io->read(io->context,dcn302_register_bytes[pipe][DCN302_R_VTG],&v))return DCN302_HUBP_IO;
            if((c&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) || (k&DCN302_BUSY_MASK) ||
               (v&DCN302_VTG_ENABLE_MASK))return DCN302_HUBP_BUSY;
        }
        uint32_t c;
        if(!io->read(io->context,dcn302_hubp_register_bytes[hubp][CONTROL],&c))return DCN302_HUBP_IO;
        if((c&BLANK)!=DCN302_HUBP_HUBP_BLANK_EN_MASK || !(c&DCN302_HUBP_HUBP_NO_OUTSTANDING_REQ_MASK))return DCN302_HUBP_BUSY;
        if(c&(DCN302_HUBP_HUBP_DISABLE_MASK|DCN302_HUBP_HUBP_UNDERFLOW_STATUS_MASK|DCN302_HUBP_HUBP_TIMEOUT_STATUS_MASK))return DCN302_HUBP_POWER;
    }
    return DCN302_HUBP_OK;
}
enum dcn302_hubp_error dcn302_hubp_blank(const dcn302_io *io,unsigned hubp,uint64_t (*now)(void *)){
    if(!io_valid(io) || !io->delay_us || !now || hubp>=5)return DCN302_HUBP_INPUT;
    enum dcn302_hubp_error e=powered(io,hubp);if(e)return e;
    uint32_t old;
    if(!io->read(io->context,dcn302_hubp_register_bytes[hubp][CONTROL],&old))return DCN302_HUBP_IO;
    if(old&DCN302_HUBP_HUBP_DISABLE_MASK)return DCN302_HUBP_POWER;
    /* DCN3's .set_blank_regs is hubp2_set_blank_regs, not hubp1_set_blank.
     * The inherited implementation drains BEFORE setting blank and leaves
     * TTU enabled. Do not import DCN1's post-write/TTU-disable behavior. */
    uint64_t start=now(io->context),last=start;
    for(unsigned n=0;n<=100000;n++){
        uint32_t c;e=powered(io,hubp);if(e)return e;
        if(!io->read(io->context,dcn302_hubp_register_bytes[hubp][CONTROL],&c))return DCN302_HUBP_IO;
        if((c&config_mask(CONTROL))!=(old&config_mask(CONTROL)) || (c&BLANK)!=(old&BLANK))return DCN302_HUBP_READBACK;
        uint64_t time=now(io->context);
        if(time<last || time-start>100000)return DCN302_HUBP_TIMEOUT;
        last=time;
        if(c&DCN302_HUBP_HUBP_NO_OUTSTANDING_REQ_MASK){
            uint32_t value=(c&~(BLANK|dcn302_hubp_forbidden[CONTROL]|dcn302_hubp_readonly[CONTROL]))|DCN302_HUBP_HUBP_BLANK_EN_MASK;
            /* A rejected posted write may have changed hardware: never unblank it. */
            if(!io->write(io->context,dcn302_hubp_register_bytes[hubp][CONTROL],value))return DCN302_HUBP_IO;
            if(!io->read(io->context,dcn302_hubp_register_bytes[hubp][CONTROL],&c))return DCN302_HUBP_IO;
            if((c&BLANK)!=DCN302_HUBP_HUBP_BLANK_EN_MASK || (c&config_mask(CONTROL))!=(old&config_mask(CONTROL)) ||
               !(c&DCN302_HUBP_HUBP_NO_OUTSTANDING_REQ_MASK))return DCN302_HUBP_READBACK;
            return powered(io,hubp);
        }
        if(n==100000)return DCN302_HUBP_TIMEOUT;
        if(!io->delay_us(io->context,1))return DCN302_HUBP_IO;
    }
    return DCN302_HUBP_TIMEOUT;
}
static bool usable(const dcn302_io *io,const dcn302_hubp_transaction *t){
    if(!io_valid(io) || !t || (uintptr_t)t%_Alignof(dcn302_hubp_transaction) || !t->prepared || t->hubp>=5 || io->context!=t->owner.context ||
       io->read!=t->owner.read || io->write!=t->owner.write || io->delay_us!=t->owner.delay_us ||
       t->touched>>DCN302_HUBP_REGISTER_COUNT)return false;
    uint32_t values[DCN302_HUBP_REGISTER_COUNT];
    if(encode(&t->timing,&t->request,values))return false;
    for(unsigned n=0;n<DCN302_HUBP_REGISTER_COUNT;n++)
        if((t->before[n]&~config_mask(n)) || t->after[n]!=((t->before[n]&~dcn302_hubp_owned[n])|values[n]))return false;
    return true;
}
static enum dcn302_hubp_error verify(const dcn302_io *io,const dcn302_hubp_transaction *t,const uint32_t *target){
    uint32_t current[DCN302_HUBP_REGISTER_COUNT];enum dcn302_hubp_error e=snapshot(io,t->hubp,current);
    if(e)return e;
    return memcmp(current,target,sizeof(current))?DCN302_HUBP_READBACK:DCN302_HUBP_OK;
}
static enum dcn302_hubp_error program_one(const dcn302_io *io,dcn302_hubp_transaction *t,unsigned reg,const uint32_t *target){
    enum dcn302_hubp_error e=quiet(io,t->hubp);if(e)return e;
    uint32_t current,mask=dcn302_hubp_owned[reg],addr=dcn302_hubp_register_bytes[t->hubp][reg];
    if(!io->read(io->context,addr,&current))return DCN302_HUBP_IO;
    /* Don't overwrite concurrent/foreign changes to unowned fields. */
    if(((current^target[reg])&config_mask(reg)&~mask))return DCN302_HUBP_READBACK;
    if((current&mask)!=(target[reg]&mask)){
        uint32_t value=(current&~(mask|dcn302_hubp_forbidden[reg]|dcn302_hubp_readonly[reg]))|(target[reg]&mask);
        t->touched|=UINT64_C(1)<<reg;t->dirty=true;
        if(!io->write(io->context,addr,value))return DCN302_HUBP_IO;
        if(!io->read(io->context,addr,&current))return DCN302_HUBP_IO;
        if((current&config_mask(reg))!=target[reg])return DCN302_HUBP_READBACK;
    }
    return quiet(io,t->hubp);
}
enum dcn302_hubp_error dcn302_hubp_restore_disabled(const dcn302_io *io,dcn302_hubp_transaction *t){
    if(!usable(io,t))return DCN302_HUBP_INPUT;
    enum dcn302_hubp_error e=quiet(io,t->hubp);
    if(!e)for(unsigned n=DCN302_HUBP_REGISTER_COUNT;n>0;n--){
        if(!(t->touched&(UINT64_C(1)<<(n-1))))continue;
        e=program_one(io,t,n-1,t->before);if(e)break;
    }
    if(!e)e=verify(io,t,t->before);
    if(!e)e=quiet(io,t->hubp);
    if(e){t->poisoned=true;return t->error=DCN302_HUBP_ROLLBACK;}
    t->touched=0;t->dirty=t->applied=t->poisoned=false;return t->error=DCN302_HUBP_OK;
}
enum dcn302_hubp_error dcn302_hubp_apply_disabled(const dcn302_io *io,dcn302_hubp_transaction *t){
    if(!usable(io,t))return DCN302_HUBP_INPUT;
    if(t->dirty || t->applied || t->poisoned || t->touched)return t->error=DCN302_HUBP_BUSY;
    enum dcn302_hubp_error e=quiet(io,t->hubp);if(e)return t->error=e;
    e=verify(io,t,t->before);if(e)return t->error=e;
    for(unsigned n=0;n<DCN302_HUBP_REGISTER_COUNT;n++){
        e=program_one(io,t,n,t->after);if(e)break;
    }
    if(!e)e=verify(io,t,t->after);
    if(!e)e=quiet(io,t->hubp);
    if(e){
        if(t->dirty && dcn302_hubp_restore_disabled(io,t))return DCN302_HUBP_ROLLBACK;
        return t->error=e;
    }
    t->applied=true;return t->error=DCN302_HUBP_OK;
}
