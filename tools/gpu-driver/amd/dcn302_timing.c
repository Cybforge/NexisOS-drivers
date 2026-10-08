/* Native DCN3 optc3_lock/optc1 timing/global-sync/VTG and buffering control.
 * Based on pinned AMD Linux v6.12 MIT sources. The generated register header
 * retains the original notice. Parent owns complete native clock/PHY/audio
 * modesetting; these disabled-pipeline writes do not prove active scanout. */
#include "dcn302_timing.h"
#include <string.h>
#define GET(v,f) (((v)&DCN302_##f##_MASK)>>DCN302_##f##_SHIFT)
#define SET(v,f,n) (((v)&~DCN302_##f##_MASK)|((uint32_t)(n)<<DCN302_##f##_SHIFT))
#define GL0 DCN302_TIMING_R_GLOBAL0
#define GL1 DCN302_TIMING_R_GLOBAL1
#define GL2 DCN302_R_GLOBAL2
#define LOCK DCN302_R_LOCK
#define DBUF DCN302_TIMING_R_DBUF
static const unsigned timing_regs[]={DCN302_R_H_TOTAL,DCN302_R_H_BLANK,DCN302_R_H_SYNC,DCN302_R_H_POL,
    DCN302_R_V_TOTAL,DCN302_R_V_MIN,DCN302_R_V_MAX,DCN302_R_V_CONTROL,DCN302_R_V_BLANK,
    DCN302_R_V_SYNC,DCN302_R_V_POL,DCN302_R_INTERLACE,DCN302_R_CONTROL,
    DCN302_R_V_STARTUP,DCN302_R_V_UPDATE,DCN302_R_V_READY,DCN302_R_VTG};
_Static_assert(DCN302_TIMING_REGISTER_COUNT<32,"transaction bitmap");
static bool io_valid(const dcn302_io *io){return io && io->read && io->write && io->delay_us;}
static bool separate(const void *p,size_t bytes,const dcn302_timing_transaction *t){
    uintptr_t a=(uintptr_t)p,b=(uintptr_t)t;
    return p && a<=UINTPTR_MAX-bytes && b<=UINTPTR_MAX-sizeof(*t) && (a<b?b-a>=bytes:a-b>=sizeof(*t));
}
static uint32_t normalized(unsigned reg,uint32_t value){return value&~dcn302_timing_excluded[reg];}
static bool rd(const dcn302_io *io,unsigned pipe,unsigned reg,uint32_t *out){return io->read(io->context,dcn302_timing_register_bytes[pipe][reg],out);}
static enum dcn302_error snapshot(const dcn302_io *io,unsigned pipe,uint32_t *out){
    for(unsigned r=0;r<DCN302_TIMING_REGISTER_COUNT;r++){
        out[r]=0;if(r==DCN302_R_FRAME_COUNT)continue;
        if(!rd(io,pipe,r,&out[r]))return DCN302_IO;
        out[r]=normalized(r,out[r]);
    }
    return DCN302_OK;
}
static bool powered(uint32_t clock){return (clock&(DCN302_CLOCK_ENABLE_MASK|DCN302_CLOCK_ON_MASK|DCN302_SOFT_RESET_MASK|DCN302_BUSY_MASK))==(DCN302_CLOCK_ENABLE_MASK|DCN302_CLOCK_ON_MASK);}
static bool supported(const uint32_t *s){
    return !GET(s[DCN302_R_INTERLACE],INTERLACE) && !GET(s[DCN302_R_H_DIV],H_DIV) &&
        !GET(s[DCN302_R_FORMAT],FORMAT) && !GET(s[DCN302_R_FORMAT],DSC) &&
        !GET(s[DCN302_R_SOURCE],SEGMENTS) && GET(s[DCN302_R_SOURCE],SEG0)<5;
}
static enum dcn302_error encode(const nexis_gpu_timing *t,const dcn302_sync *s,const uint32_t *before,uint32_t *out){
    if(!t || !s || !t->pixel_khz || t->pixel_khz>4000000 || !t->hactive || !t->vactive || (t->flags&~3u) ||
       t->hactive>=t->hsync_start || t->hsync_start>=t->hsync_end || t->hsync_end>t->htotal || t->htotal>32768 ||
       t->htotal-t->hactive<32 || t->hsync_end-t->hsync_start<4 ||
       t->vactive>=t->vsync_start || t->vsync_start>=t->vsync_end || t->vsync_end>t->vtotal || t->vtotal>32768 ||
       t->vtotal-t->vactive<3 || !s->vstartup || s->vstartup>1023 || s->vstartup>t->vtotal ||
       s->vready>65535 || s->vupdate_offset>65535 || !s->vupdate_width || s->vupdate_width>1023)return DCN302_INPUT;
    uint32_t hs=t->htotal-(t->hsync_start-t->hactive),he=hs-t->hactive;
    uint32_t vs=t->vtotal-(t->vsync_start-t->vactive),ve=vs-t->vactive;
    uint32_t fp2=s->vstartup>ve+1?s->vstartup-ve-1:0;
    if(fp2>32767)return DCN302_INPUT;
    memcpy(out,before,DCN302_TIMING_REGISTER_COUNT*sizeof(*out));
    out[DCN302_R_H_TOTAL]=SET(out[DCN302_R_H_TOTAL],H_TOTAL,t->htotal-1);
    out[DCN302_R_H_BLANK]=SET(SET(out[DCN302_R_H_BLANK],H_BLANK_START,hs),H_BLANK_END,he);
    out[DCN302_R_H_SYNC]=SET(SET(out[DCN302_R_H_SYNC],H_SYNC_START,0),H_SYNC_END,t->hsync_end-t->hsync_start);
    out[DCN302_R_H_POL]=SET(out[DCN302_R_H_POL],H_POL,!(t->flags&1));
    out[DCN302_R_V_TOTAL]=SET(out[DCN302_R_V_TOTAL],V_TOTAL,t->vtotal-1);
    out[DCN302_R_V_MIN]=SET(out[DCN302_R_V_MIN],V_MIN,t->vtotal-1);
    out[DCN302_R_V_MAX]=SET(out[DCN302_R_V_MAX],V_MAX,t->vtotal-1);
    out[DCN302_R_V_CONTROL]&=~dcn302_timing_owned[DCN302_R_V_CONTROL];
    out[DCN302_R_V_BLANK]=SET(SET(out[DCN302_R_V_BLANK],V_BLANK_START,vs),V_BLANK_END,ve);
    out[DCN302_R_V_SYNC]=SET(SET(out[DCN302_R_V_SYNC],V_SYNC_START,0),V_SYNC_END,t->vsync_end-t->vsync_start);
    out[DCN302_R_V_POL]=SET(out[DCN302_R_V_POL],V_POL,!(t->flags&2));
    out[DCN302_R_INTERLACE]=SET(out[DCN302_R_INTERLACE],INTERLACE,0);
    out[DCN302_R_CONTROL]=SET(SET(out[DCN302_R_CONTROL],START_POINT,s->display_port),FIELD_NUMBER,0);
    out[DCN302_R_V_STARTUP]=SET(out[DCN302_R_V_STARTUP],V_STARTUP,s->vstartup);
    out[DCN302_R_V_UPDATE]=SET(SET(out[DCN302_R_V_UPDATE],V_UPDATE_OFFSET,s->vupdate_offset),V_UPDATE_WIDTH,s->vupdate_width);
    out[DCN302_R_V_READY]=SET(out[DCN302_R_V_READY],V_READY,s->vready);
    out[DCN302_R_VTG]=SET(SET(out[DCN302_R_VTG],VTG_INIT,vs),VTG_FP2,fp2);
    out[GL0]&=~dcn302_timing_owned[GL0];out[GL1]&=~dcn302_timing_owned[GL1];
    out[GL2]&=~DCN302_TIMING_GLOBAL_LOCK_ENABLE_MASK;
    out[DBUF]&=~DCN302_TIMING_DRR_MODE_MASK;out[LOCK]&=~DCN302_LOCK_MASK;
    return DCN302_OK;
}
enum dcn302_error dcn302_timing_prepare(const dcn302_io *io,unsigned pipe,const nexis_gpu_timing *timing,
    const dcn302_sync *sync,bool (*guard)(void *),uint64_t (*now)(void *),dcn302_timing_transaction *t){
    if(!t || (uintptr_t)t%_Alignof(dcn302_timing_transaction) ||
       (uintptr_t)io%_Alignof(dcn302_io) || (uintptr_t)timing%_Alignof(nexis_gpu_timing) || (uintptr_t)sync%_Alignof(dcn302_sync) ||
       !separate(io,sizeof(*io),t) || !separate(timing,sizeof(*timing),t) || !separate(sync,sizeof(*sync),t))return DCN302_INPUT;
    memset(t,0,sizeof(*t));if(!io_valid(io) || pipe>=5 || !guard || !now)return t->error=DCN302_INPUT;
    uint32_t zero[DCN302_TIMING_REGISTER_COUNT]={0},second[DCN302_TIMING_REGISTER_COUNT],clock,lock;
    enum dcn302_error e=encode(timing,sync,zero,second);if(e)return t->error=e;
    if(!rd(io,pipe,DCN302_R_CLOCK,&clock) || !rd(io,pipe,LOCK,&lock))return t->error=DCN302_IO;
    if(!powered(clock) || (lock&(DCN302_LOCK_MASK|DCN302_LOCK_STATUS_MASK)))return t->error=DCN302_BUSY;
    e=snapshot(io,pipe,t->before);if(e)return t->error=e;
    e=snapshot(io,pipe,second);if(e)return t->error=e;
    if(memcmp(second,t->before,sizeof(second)))return t->error=DCN302_READBACK;
    if(!supported(t->before))return t->error=DCN302_UNSUPPORTED;
    if(t->before[LOCK]&DCN302_LOCK_MASK)return t->error=DCN302_BUSY;
    e=encode(timing,sync,t->before,t->after);if(e)return t->error=e;
    t->owner=*io;t->guard=guard;t->now=now;t->timing=*timing;t->sync=*sync;t->pipe=pipe;t->prepared=true;return t->error=DCN302_OK;
}
static bool usable(const dcn302_io *io,const dcn302_timing_transaction *t){
    if(!t || (uintptr_t)t%_Alignof(dcn302_timing_transaction) || (uintptr_t)io%_Alignof(dcn302_io) ||
       !separate(io,sizeof(*io),t) || !io_valid(io) || !t->prepared || t->pipe>=5 || !t->guard || !t->now ||
       io->context!=t->owner.context || io->read!=t->owner.read || io->write!=t->owner.write || io->delay_us!=t->owner.delay_us ||
       t->touched>>DCN302_TIMING_REGISTER_COUNT || !supported(t->before) || (t->before[LOCK]&DCN302_LOCK_MASK))return false;
    uint32_t target[DCN302_TIMING_REGISTER_COUNT];if(encode(&t->timing,&t->sync,t->before,target) || memcmp(target,t->after,sizeof(target)))return false;
    for(unsigned r=0;r<DCN302_TIMING_REGISTER_COUNT;r++)if(t->before[r]&dcn302_timing_excluded[r])return false;
    return true;
}
static enum dcn302_error quiet(const dcn302_io *io,const dcn302_timing_transaction *t){
    for(unsigned sweep=0;sweep<2;sweep++){
        if(!t->guard(io->context))return DCN302_BUSY;
        for(unsigned p=0;p<5;p++){
            uint32_t c,k,v;
            if(!rd(io,p,DCN302_R_CONTROL,&c) || !rd(io,p,DCN302_R_CLOCK,&k) || !rd(io,p,DCN302_R_VTG,&v))return DCN302_IO;
            if((c&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) || (k&DCN302_BUSY_MASK) || (v&DCN302_VTG_ENABLE_MASK))return DCN302_BUSY;
            if(p==t->pipe && (!powered(k) || normalized(DCN302_R_CLOCK,k)!=t->before[DCN302_R_CLOCK]))return DCN302_BUSY;
        }
        for(unsigned r=0;r<DCN302_TIMING_REGISTER_COUNT;r++)if(!dcn302_timing_owned[r] && r!=DCN302_R_FRAME_COUNT && r!=DCN302_R_CLOCK){
            uint32_t v;if(!rd(io,t->pipe,r,&v))return DCN302_IO;
            if(normalized(r,v)!=t->before[r])return DCN302_READBACK;
        }
    }
    return DCN302_OK;
}
static enum dcn302_error wait_state(const dcn302_io *io,dcn302_timing_transaction *t,bool pending,bool locked){
    uint64_t start=t->now(io->context),last=start;unsigned maximum=pending?100000:10;
    for(unsigned n=0;n<=maximum;n++){
        enum dcn302_error e=quiet(io,t);if(e)return e;
        uint32_t v;if(!rd(io,t->pipe,pending?DBUF:LOCK,&v))return DCN302_IO;
        uint64_t time=t->now(io->context);if(time<last)return DCN302_TIMEOUT;last=time;
        bool done=pending?!(v&(DCN302_TIMING_DBUF_PENDING_MASK|DCN302_TIMING_DBUF_INSTANT_MASK)):
            ((!!(v&DCN302_LOCK_MASK)==locked) && (!!(v&DCN302_LOCK_STATUS_MASK)==locked));
        if(done)return DCN302_OK;
        if(n==maximum || time-start>(pending?200000u:10u))return DCN302_TIMEOUT;
        if(!io->delay_us(io->context,pending?2:1))return DCN302_IO;
    }
    return DCN302_TIMEOUT;
}
static enum dcn302_error lock_good(const dcn302_io *io,const dcn302_timing_transaction *t){
    uint32_t l,g;if(!rd(io,t->pipe,LOCK,&l) || !rd(io,t->pipe,GL2,&g))return DCN302_IO;
    return (l&(DCN302_LOCK_MASK|DCN302_LOCK_STATUS_MASK))==(DCN302_LOCK_MASK|DCN302_LOCK_STATUS_MASK) &&
        (g&dcn302_timing_owned[GL2])==(t->pipe<<DCN302_TIMING_LOCK_SELECT_SHIFT)?DCN302_OK:DCN302_BUSY;
}
static enum dcn302_error program(const dcn302_io *io,dcn302_timing_transaction *t,unsigned r,uint32_t target,bool locked){
    enum dcn302_error e=quiet(io,t);if(e)return e;
    if(locked){e=lock_good(io,t);if(e)return e;}
    uint32_t value,mask=dcn302_timing_owned[r];if(!mask)return DCN302_INPUT;
    if(!rd(io,t->pipe,r,&value))return DCN302_IO;
    if((normalized(r,value)^target)&~mask)return DCN302_READBACK;
    if((value&mask)!=(target&mask)){
        value=(value&~(mask|dcn302_timing_write_excluded[r]))|(target&mask);
        t->dirty=true;t->touched|=1u<<r;
        if(!io->write(io->context,dcn302_timing_register_bytes[t->pipe][r],value))return DCN302_IO;
        if(!rd(io,t->pipe,r,&value))return DCN302_IO;
        if(normalized(r,value)!=target)return DCN302_READBACK;
    }
    e=quiet(io,t);if(!e && locked)e=lock_good(io,t);return e;
}
static enum dcn302_error buffers_off(const dcn302_io *io,dcn302_timing_transaction *t){
    const unsigned regs[]={DBUF,GL0,GL1,GL2};
    for(unsigned n=0;n<4;n++){
        unsigned r=regs[n];uint32_t value=t->after[r];
        if(r==GL2)value=(value&~DCN302_TIMING_LOCK_SELECT_MASK)|(t->pipe<<DCN302_TIMING_LOCK_SELECT_SHIFT);
        enum dcn302_error e=program(io,t,r,value,false);if(e)return e;
    }
    return DCN302_OK;
}
static enum dcn302_error acquire(const dcn302_io *io,dcn302_timing_transaction *t){
    uint32_t lock;if(!rd(io,t->pipe,LOCK,&lock))return DCN302_IO;
    if((lock&(DCN302_LOCK_MASK|DCN302_LOCK_STATUS_MASK)) && !(t->touched&(1u<<LOCK)))return DCN302_BUSY;
    if(!(lock&DCN302_LOCK_MASK)){
        enum dcn302_error e=wait_state(io,t,false,false);if(e)return e;
        e=wait_state(io,t,true,false);if(e)return e;
    }else{
        /* A possibly posted lock acquisition belongs to this transaction.
         * Never redirect its selector while it is held: first prove the
         * existing selected pipe and already-disabled buffer configuration. */
        enum dcn302_error e=wait_state(io,t,false,true);if(e)return e;
        e=lock_good(io,t);if(e)return e;
        const unsigned regs[]={DBUF,GL0,GL1};
        for(unsigned n=0;n<3;n++){uint32_t v;unsigned r=regs[n];if(!rd(io,t->pipe,r,&v))return DCN302_IO;
            if(normalized(r,v)!=t->after[r])return DCN302_READBACK;}
        return DCN302_OK;
    }
    enum dcn302_error e=buffers_off(io,t);if(e)return e;
    e=program(io,t,LOCK,t->before[LOCK]|DCN302_LOCK_MASK,false);if(e)return e;
    return wait_state(io,t,false,true);
}
static enum dcn302_error verify(const dcn302_io *io,dcn302_timing_transaction *t,const uint32_t *target,bool locked){
    uint32_t actual[DCN302_TIMING_REGISTER_COUNT],expected[DCN302_TIMING_REGISTER_COUNT];
    enum dcn302_error e=snapshot(io,t->pipe,actual);if(e)return e;
    memcpy(expected,target,sizeof(expected));
    if(locked){expected[LOCK]|=DCN302_LOCK_MASK;expected[GL0]=t->after[GL0];expected[GL1]=t->after[GL1];expected[DBUF]=t->after[DBUF];
        expected[GL2]=(t->after[GL2]&~DCN302_TIMING_LOCK_SELECT_MASK)|(t->pipe<<DCN302_TIMING_LOCK_SELECT_SHIFT);}
    return memcmp(actual,expected,sizeof(actual))?DCN302_READBACK:DCN302_OK;
}
static enum dcn302_error release(const dcn302_io *io,dcn302_timing_transaction *t){
    enum dcn302_error e=program(io,t,LOCK,t->before[LOCK],false);if(e)return e;
    e=wait_state(io,t,false,false);if(e)return e;
    return wait_state(io,t,true,false);
}
enum dcn302_error dcn302_timing_restore_disabled(const dcn302_io *io,dcn302_timing_transaction *t){
    if(!usable(io,t))return DCN302_INPUT;
    enum dcn302_error e=quiet(io,t);
    if(!e && (t->dirty || t->applied || t->touched)){
        e=acquire(io,t);
        if(!e)for(unsigned n=sizeof(timing_regs)/sizeof(*timing_regs);n;n--){
            unsigned r=timing_regs[n-1];if(!(t->touched&(1u<<r)))continue;
            e=program(io,t,r,t->before[r],true);if(e)break;
        }
        if(!e)e=verify(io,t,t->before,true);
        if(!e)e=release(io,t);
        /* Buffer mode/global enable last, after old shadow timing has drained. */
        const unsigned regs[]={DBUF,GL1,GL0,GL2};
        if(!e)for(unsigned n=0;n<4;n++){unsigned r=regs[n];e=program(io,t,r,t->before[r],false);if(e)break;}
    }
    if(!e)e=wait_state(io,t,false,false);
    if(!e)e=wait_state(io,t,true,false);
    if(!e)e=verify(io,t,t->before,false);
    if(!e)e=quiet(io,t);
    if(e){t->poisoned=true;return t->error=DCN302_ROLLBACK;}
    t->touched=0;t->dirty=t->applied=t->poisoned=false;return t->error=DCN302_OK;
}
enum dcn302_error dcn302_timing_apply_disabled(const dcn302_io *io,dcn302_timing_transaction *t){
    if(!usable(io,t))return DCN302_INPUT;
    if(t->dirty || t->applied || t->poisoned || t->touched)return t->error=DCN302_BUSY;
    enum dcn302_error e=quiet(io,t);if(e)return t->error=e;
    e=wait_state(io,t,false,false);if(e)return t->error=e;
    e=wait_state(io,t,true,false);if(e)return t->error=e;
    e=verify(io,t,t->before,false);if(e)return t->error=e;
    e=acquire(io,t);
    if(!e)for(unsigned n=0;n<sizeof(timing_regs)/sizeof(*timing_regs);n++){
        unsigned r=timing_regs[n];e=program(io,t,r,t->after[r],true);if(e)break;
    }
    if(!e)e=verify(io,t,t->after,true);
    if(!e)e=release(io,t);
    if(!e)e=program(io,t,GL2,t->after[GL2],false);
    if(!e)e=verify(io,t,t->after,false);
    if(!e)e=quiet(io,t);
    if(e){if(t->dirty && dcn302_timing_restore_disabled(io,t))return DCN302_ROLLBACK;return t->error=e;}
    t->applied=true;return t->error=DCN302_OK;
}
