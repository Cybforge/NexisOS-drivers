/* Native Navi23/DCN302 HUBBUB watermarks/reference/policy, based on AMD MIT
 * Linux v6.12 dcn30_hubbub.c and inherited dcn21/dcn20/dcn10 routines.
 * The generated register header retains the AMD copyright/license notice. */
#include "dcn302_hubbub.h"
#include <stddef.h>
#include <string.h>
typedef struct {uint32_t mask;uint16_t offset;uint8_t reg,shift,ns;} wm_field;
#define FIELD(reg,mask,shift,member,ns) {mask,offsetof(dcn302_hubbub_values,member),reg,shift,ns},
static const wm_field fields[]={DCN302_HUBBUB_FIELDS(FIELD)};
#undef FIELD
_Static_assert(DCN302_HUBBUB_REGISTER_COUNT<64,"register bitmap");
#define POLICY DCN302_HUBBUB_R_DCHUBBUB_ARB_DRAM_STATE_CNTL
static bool io_valid(const dcn302_io *io){return io && io->read && io->write;}
static bool separate(const void *a,size_t size,const dcn302_hubbub_transaction *t){
    uintptr_t p=(uintptr_t)a,q=(uintptr_t)t;
    if(!a || p>UINTPTR_MAX-size || q>UINTPTR_MAX-sizeof(*t))return false;
    return p<q?q-p>=size:p-q>=sizeof(*t);
}
static enum dcn302_hubbub_error decode(dcn302_reference *r){
    if(r->crystal_khz<10000 || r->crystal_khz>200000 || r->crystal_khz%10)return DCN302_HUBBUB_INPUT;
    if((r->ref_control&DCN302_HUBBUB_REF_ENABLE_MASK) || !(r->timer&DCN302_HUBBUB_TIMER_ENABLE_MASK))return DCN302_HUBBUB_REFERENCE;
    uint32_t div=(r->timer&DCN302_HUBBUB_TIMER_DIV_MASK)>>DCN302_HUBBUB_TIMER_DIV_SHIFT;
    r->khz=div==2?r->crystal_khz/2:r->crystal_khz;
    if(r->khz<40000 || r->khz>60000)return DCN302_HUBBUB_REFERENCE;
    r->valid=true;return DCN302_HUBBUB_OK;
}
enum dcn302_hubbub_error dcn302_reference_read(const dcn302_io *io,uint32_t crystal,dcn302_reference *out){
    uintptr_t p=(uintptr_t)io,q=(uintptr_t)out;
    if(!out || q%_Alignof(dcn302_reference) || q>UINTPTR_MAX-sizeof(*out) ||
       (io && (p%_Alignof(dcn302_io) || p>UINTPTR_MAX-sizeof(*io) ||
       (p<q?q-p<sizeof(*io):p-q<sizeof(*out)))))return DCN302_HUBBUB_INPUT;
    memset(out,0,sizeof(*out));
    if(!io || !io->read)return DCN302_HUBBUB_INPUT;
    dcn302_reference r={.crystal_khz=crystal};
    if(!io->read(io->context,DCN302_HUBBUB_REF_BYTES,&r.ref_control) ||
       !io->read(io->context,DCN302_HUBBUB_TIMER_BYTES,&r.timer))return DCN302_HUBBUB_IO;
    enum dcn302_hubbub_error e=decode(&r);if(e)return e;
    uint32_t ref,timer;
    if(!io->read(io->context,DCN302_HUBBUB_REF_BYTES,&ref) || !io->read(io->context,DCN302_HUBBUB_TIMER_BYTES,&timer))return DCN302_HUBBUB_IO;
    if(ref!=r.ref_control || timer!=r.timer)return DCN302_HUBBUB_READBACK;
    *out=r;return DCN302_HUBBUB_OK;
}
bool dcn302_reference_equal(const dcn302_reference *a,const dcn302_reference *b){
    /* Struct tail padding is not hardware identity. */
    return a && b && a->valid && b->valid && a->ref_control==b->ref_control &&
        a->timer==b->timer && a->crystal_khz==b->crystal_khz && a->khz==b->khz;
}
static bool reference_valid(const dcn302_reference *r){
    if(!r || !r->valid)return false;
    dcn302_reference copy=*r;
    return !decode(&copy) && copy.khz==r->khz;
}
static enum dcn302_hubbub_error plan(const dcn302_reference *ref,const dcn302_dml_output *o,uint32_t *result){
    if(!reference_valid(ref) || !o || !o->urgent_ns || !o->memory_trip_ns || !o->disp_khz || !o->dpp_khz ||
       o->frac_urg_nom>1000 || o->frac_urg_flip>1000)return DCN302_HUBBUB_INPUT;
    dcn302_hubbub_values values={.urgent_ns=o->urgent_ns,.memory_trip_ns=o->memory_trip_ns,
        .stutter_enter_exit_ns=o->stutter_enter_exit_ns,.stutter_exit_ns=o->stutter_exit_ns,
        .dram_change_ns=o->dram_change_ns,.frac_urg_nom=o->frac_urg_nom,.frac_urg_flip=o->frac_urg_flip,
        .sat_cycles=(60u*ref->khz+999u)/1000u,.min_outstanding=0x1ff,
        .sr_value=0,.sr_force=1,.pstate_value=0,.pstate_force=1};
    memset(result,0,DCN302_HUBBUB_REGISTER_COUNT*sizeof(*result));
    for(unsigned n=0;n<sizeof(fields)/sizeof(*fields);n++){
        const wm_field *f=&fields[n];uint32_t v;memcpy(&v,(unsigned char *)&values+f->offset,sizeof(v));
        /* Nanoseconds * kHz / 1e6. uint64 avoids upstream uint32 overflow;
         * round UP and reject real native width overflow, never wrap/clamp. */
        uint64_t cycles=f->ns?((uint64_t)v*ref->khz+999999u)/1000000u:v;
        if(cycles>(f->mask>>f->shift))return DCN302_HUBBUB_RANGE;
        result[f->reg]|=(uint32_t)cycles<<f->shift;
    }
    return DCN302_HUBBUB_OK;
}
static enum dcn302_hubbub_error snapshot(const dcn302_io *io,uint32_t *values){
    for(unsigned n=0;n<DCN302_HUBBUB_REGISTER_COUNT;n++)
        if(!io->read(io->context,dcn302_hubbub_register_bytes[n],&values[n]))return DCN302_HUBBUB_IO;
    return DCN302_HUBBUB_OK;
}
enum dcn302_hubbub_error dcn302_hubbub_prepare(const dcn302_io *io,unsigned hubp,const dcn302_reference *ref,const dcn302_dml_output *o,dcn302_hubbub_transaction *t){
    if(!t || (uintptr_t)t%_Alignof(dcn302_hubbub_transaction))return DCN302_HUBBUB_INPUT;
    if((uintptr_t)io%_Alignof(dcn302_io) || (uintptr_t)ref%_Alignof(dcn302_reference) || (uintptr_t)o%_Alignof(dcn302_dml_output) ||
       !separate(io,sizeof(*io),t) || !separate(ref,sizeof(*ref),t) || !separate(o,sizeof(*o),t))return DCN302_HUBBUB_INPUT;
    memset(t,0,sizeof(*t));
    if(!io_valid(io) || hubp>=5)return t->error=DCN302_HUBBUB_INPUT;
    uint32_t target[DCN302_HUBBUB_REGISTER_COUNT],second[DCN302_HUBBUB_REGISTER_COUNT];
    enum dcn302_hubbub_error e=plan(ref,o,target);if(e)return t->error=e;
    dcn302_reference current;e=dcn302_reference_read(io,ref->crystal_khz,&current);if(e)return t->error=e;
    if(!dcn302_reference_equal(&current,ref))return t->error=DCN302_HUBBUB_READBACK;
    e=snapshot(io,t->before);if(e)return t->error=e;
    e=snapshot(io,second);if(e)return t->error=e;
    if(memcmp(t->before,second,sizeof(second)))return t->error=DCN302_HUBBUB_READBACK;
    for(unsigned n=0;n<DCN302_HUBBUB_REGISTER_COUNT;n++)t->after[n]=(t->before[n]&~dcn302_hubbub_owned[n])|target[n];
    t->owner=*io;t->reference=*ref;t->request=*o;t->hubp=hubp;t->prepared=true;return t->error=DCN302_HUBBUB_OK;
}
static bool usable(const dcn302_io *io,const dcn302_hubbub_transaction *t){
    if(!io_valid(io) || !t || (uintptr_t)t%_Alignof(dcn302_hubbub_transaction) || !t->prepared || t->hubp>=5 ||
       io->context!=t->owner.context || io->read!=t->owner.read || io->write!=t->owner.write || io->delay_us!=t->owner.delay_us ||
       t->touched>>DCN302_HUBBUB_REGISTER_COUNT)return false;
    uint32_t target[DCN302_HUBBUB_REGISTER_COUNT];if(plan(&t->reference,&t->request,target))return false;
    for(unsigned n=0;n<DCN302_HUBBUB_REGISTER_COUNT;n++)if(t->after[n]!=((t->before[n]&~dcn302_hubbub_owned[n])|target[n]))return false;
    return true;
}
static enum dcn302_hubbub_error quiet(const dcn302_io *io,const dcn302_hubbub_transaction *t){
    for(unsigned sweep=0;sweep<2;sweep++){
        dcn302_reference current;enum dcn302_hubbub_error e=dcn302_reference_read(io,t->reference.crystal_khz,&current);if(e)return e;
        if(!dcn302_reference_equal(&current,&t->reference))return DCN302_HUBBUB_READBACK;
        for(unsigned p=0;p<5;p++){
            uint32_t c,k,v;
            if(!io->read(io->context,dcn302_register_bytes[p][DCN302_R_CONTROL],&c) ||
               !io->read(io->context,dcn302_register_bytes[p][DCN302_R_CLOCK],&k) ||
               !io->read(io->context,dcn302_register_bytes[p][DCN302_R_VTG],&v))return DCN302_HUBBUB_IO;
            if((c&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) || (k&DCN302_BUSY_MASK) || (v&DCN302_VTG_ENABLE_MASK))return DCN302_HUBBUB_BUSY;
        }
        uint32_t c,k;
        if(!io->read(io->context,dcn302_hubp_register_bytes[t->hubp][DCN302_HUBP_R_DCHUBP_CNTL],&c) ||
           !io->read(io->context,dcn302_hubp_clock_bytes[t->hubp],&k))return DCN302_HUBBUB_IO;
        const uint32_t control=DCN302_HUBP_HUBP_BLANK_EN_MASK|DCN302_HUBP_HUBP_TTU_DISABLE_MASK|
            DCN302_HUBP_HUBP_NO_OUTSTANDING_REQ_MASK|DCN302_HUBP_HUBP_DISABLE_MASK;
        const uint32_t clocks=DCN302_HUBP_HUBP_CLOCK_ENABLE_MASK|DCN302_HUBP_HUBP_DISPCLK_R_CLOCK_ON_MASK|
            DCN302_HUBP_HUBP_DPPCLK_G_CLOCK_ON_MASK|DCN302_HUBP_HUBP_DCFCLK_R_CLOCK_ON_MASK|
            DCN302_HUBP_HUBP_DCFCLK_G_CLOCK_ON_MASK;
        if((c&control)!=(DCN302_HUBP_HUBP_BLANK_EN_MASK|DCN302_HUBP_HUBP_NO_OUTSTANDING_REQ_MASK) ||
           (c&(DCN302_HUBP_HUBP_UNDERFLOW_STATUS_MASK|DCN302_HUBP_HUBP_TIMEOUT_STATUS_MASK)) ||
           (k&clocks)!=clocks)return DCN302_HUBBUB_BUSY;
    }
    return DCN302_HUBBUB_OK;
}
static enum dcn302_hubbub_error verify(const dcn302_io *io,const uint32_t *target){
    uint32_t current[DCN302_HUBBUB_REGISTER_COUNT];enum dcn302_hubbub_error e=snapshot(io,current);
    return e?e:memcmp(current,target,sizeof(current))?DCN302_HUBBUB_READBACK:DCN302_HUBBUB_OK;
}
enum dcn302_hubbub_error dcn302_hubbub_verify_installed(const dcn302_io *io,const dcn302_hubbub_transaction *t){
    if(!usable(io,t))return DCN302_HUBBUB_INPUT;
    if(!t->applied || t->poisoned)return DCN302_HUBBUB_BUSY;
    enum dcn302_hubbub_error e=quiet(io,t);if(e)return e;
    e=verify(io,t->after);return e?e:quiet(io,t);
}
static enum dcn302_hubbub_error program_one(const dcn302_io *io,dcn302_hubbub_transaction *t,unsigned reg,const uint32_t *target){
    enum dcn302_hubbub_error e=quiet(io,t);if(e)return e;
    uint32_t current,mask=dcn302_hubbub_owned[reg],addr=dcn302_hubbub_register_bytes[reg];
    if(!io->read(io->context,addr,&current))return DCN302_HUBBUB_IO;
    if((current^target[reg])&~mask)return DCN302_HUBBUB_READBACK;
    if(current!=target[reg]){
        t->dirty=true;t->touched|=UINT64_C(1)<<reg;
        if(!io->write(io->context,addr,(current&~mask)|(target[reg]&mask)))return DCN302_HUBBUB_IO;
        if(!io->read(io->context,addr,&current))return DCN302_HUBBUB_IO;
        if(current!=target[reg])return DCN302_HUBBUB_READBACK;
    }
    return quiet(io,t);
}
enum dcn302_hubbub_error dcn302_hubbub_restore_disabled(const dcn302_io *io,dcn302_hubbub_transaction *t){
    if(!usable(io,t))return DCN302_HUBBUB_INPUT;
    enum dcn302_hubbub_error e=quiet(io,t);
    if(!e)for(unsigned n=DCN302_HUBBUB_REGISTER_COUNT;n>0;n--){
        if(!(t->touched&(UINT64_C(1)<<(n-1))))continue;
        e=program_one(io,t,n-1,t->before);if(e)break;
    }
    if(!e)e=verify(io,t->before);
    if(!e)e=quiet(io,t);
    if(e){t->poisoned=true;return t->error=DCN302_HUBBUB_ROLLBACK;}
    t->touched=0;t->dirty=t->applied=t->poisoned=false;return t->error=DCN302_HUBBUB_OK;
}
enum dcn302_hubbub_error dcn302_hubbub_apply_disabled(const dcn302_io *io,dcn302_hubbub_transaction *t){
    if(!usable(io,t))return DCN302_HUBBUB_INPUT;
    if(t->dirty || t->applied || t->poisoned || t->touched)return t->error=DCN302_HUBBUB_BUSY;
    enum dcn302_hubbub_error e=quiet(io,t);if(e)return t->error=e;
    e=verify(io,t->before);if(e)return t->error=e;
    for(unsigned n=0;n<DCN302_HUBBUB_REGISTER_COUNT;n++){e=program_one(io,t,n,t->after);if(e)break;}
    if(!e)e=verify(io,t->after);
    if(!e)e=quiet(io,t);
    if(e){if(t->dirty && dcn302_hubbub_restore_disabled(io,t))return DCN302_HUBBUB_ROLLBACK;return t->error=e;}
    t->applied=true;return t->error=DCN302_HUBBUB_OK;
}
