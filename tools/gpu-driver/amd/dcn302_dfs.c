/* Native display DFS/DTO programming based on AMD Linux v6.12
 * dcn20_clk_mgr.c, dcn30_clk_mgr.c and dcn20_dccg.c.
 * Copyright 2018/2020 Advanced Micro Devices, Inc.
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the Software),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "dcn302_dfs.h"
#include <string.h>
#define GET(v,f) (((v)&DCN302_DFS_##f##_MASK)>>DCN302_DFS_##f##_SHIFT)
#define SET(v,f,n) (((v)&~DCN302_DFS_##f##_MASK)|(((uint32_t)(n)<<DCN302_DFS_##f##_SHIFT)&DCN302_DFS_##f##_MASK))
#define STATUS (DCN302_DFS_DISP_READ_MASK|DCN302_DFS_DISP_DONE_MASK|DCN302_DFS_DPP_DONE_MASK|DCN302_DFS_DISP_CHANGE_TOGGLE_MASK|DCN302_DFS_DISP_DONE_TOGGLE_MASK|DCN302_DFS_DPP_CHANGE_TOGGLE_MASK|DCN302_DFS_DPP_DONE_TOGGLE_MASK)
static uint32_t divider(unsigned did){
    if(did<8 || did>127)return 0;
    if(did<64)return did;
    if(did<96)return 64+(did-64)*2;
    if(did<126)return 128+(did-96)*4;
    return 248+(did-126)*264;
}
static enum dcn302_dfs_error decode(dcn302_dfs_snapshot *s){
    uint64_t multiplier=((uint64_t)(s->pll&DCN302_DFS_INTEGER_MASK)<<32)|(s->pll&DCN302_DFS_FRACTION_MASK);
    /* CLK02's DFS reference is 100MHz, not the board's HDMI crystal. */
    s->vco_khz=(uint32_t)((multiplier*100000)>>32);
    unsigned disp=GET(s->dentist,DISP_WRITE),dpp=GET(s->dentist,DPP_WRITE);
    uint32_t dd=divider(disp),pd=divider(dpp);
    if(!s->vco_khz || !dd || !pd)return DCN302_DFS_UNSUPPORTED;
    if(!GET(s->dentist,DISP_DONE) || !GET(s->dentist,DPP_DONE) || GET(s->dentist,DISP_READ)!=disp)return DCN302_DFS_BUSY;
    uint64_t numerator=(uint64_t)s->vco_khz*4;
    s->disp_khz=(uint32_t)(numerator/dd);s->dpp_khz=(uint32_t)(numerator/pd);
    /* Voltage floors use the exact fixed-point PLL numerator, avoiding an
     * unsafe 1MHz rounding drop when integer-kHz readback lands on a MHz edge. */
    uint64_t exact=multiplier*400000,den_disp=(uint64_t)dd*1000*(1ULL<<32),den_dpp=(uint64_t)pd*1000*(1ULL<<32);
    s->disp_floor_mhz=(uint32_t)((exact+den_disp-1)/den_disp);s->dpp_floor_mhz=(uint32_t)((exact+den_dpp-1)/den_dpp);
    for(unsigned i=0;i<5;i++){
        if(s->dto_control&dcn302_dfs_dto_db[i])return DCN302_DFS_UNSUPPORTED;
        if(!(s->dto_control&dcn302_dfs_dto_enable[i]))s->pipe_khz[i]=s->dpp_khz;
        else{
            unsigned phase=GET(s->dto[i],PHASE),modulo=GET(s->dto[i],MODULO);
            if(!phase || !modulo || phase>modulo)return DCN302_DFS_UNSUPPORTED;
            s->pipe_khz[i]=(uint32_t)(numerator*phase/((uint64_t)pd*modulo));
            if(!s->pipe_khz[i])return DCN302_DFS_UNSUPPORTED;
        }
    }
    s->valid=true;return DCN302_DFS_OK;
}
static bool collect(const dcn302_io *io,dcn302_dfs_snapshot *s){
    memset(s,0,sizeof(*s));
    if(!io->read(io->context,DCN302_DFS_PLL_BYTES,&s->pll) ||
       !io->read(io->context,DCN302_DFS_DENTIST_BYTES,&s->dentist) ||
       !io->read(io->context,DCN302_DFS_DTO_CTRL_BYTES,&s->dto_control))return false;
    for(unsigned i=0;i<5;i++)if(!io->read(io->context,dcn302_dfs_dto_bytes[i],&s->dto[i]))return false;
    return true;
}
static bool same(const dcn302_dfs_snapshot *a,const dcn302_dfs_snapshot *b){
    return a->pll==b->pll && !((a->dentist^b->dentist)&~STATUS) && a->dto_control==b->dto_control && !memcmp(a->dto,b->dto,sizeof(a->dto));
}
enum dcn302_dfs_error dcn302_dfs_read(const dcn302_io *io,dcn302_dfs_snapshot *out){
    if(!out)return DCN302_DFS_INPUT;
    memset(out,0,sizeof(*out));
    if(!io || !io->read)return DCN302_DFS_INPUT;
    dcn302_dfs_snapshot a,b;
    if(!collect(io,&a) || !collect(io,&b))return DCN302_DFS_IO;
    enum dcn302_dfs_error e=decode(&a);if(e)return e;
    e=decode(&b);if(e)return e;
    if(!same(&a,&b))return DCN302_DFS_BUSY;
    *out=b;return DCN302_DFS_OK;
}
static bool choose(uint32_t vco,uint32_t requested,bool display,unsigned *did){
    if(!requested)return false;
    /* Largest allowed divider satisfying exact clock >= request. No clamping
     * an impossible request to an undersized hardware clock. */
    for(unsigned i=display?126:127;i>=8;i--)
        if((uint64_t)divider(i)*requested<=(uint64_t)vco*4){*did=i;return true;}
    return false;
}
static enum dcn302_dfs_error plan(const dcn302_dfs_snapshot *before,const dcn302_dfs_request *r,dcn302_dfs_snapshot *after){
    unsigned disp,dpp;
    if(!choose(before->vco_khz,r->disp_khz,true,&disp) || !choose(before->vco_khz,r->dpp_khz,false,&dpp))return DCN302_DFS_UNSUPPORTED;
    /* Preserve a no-op DID127 display clock, but never transition to/from it
     * without the stream FIFO calibration owned by the full modeset backend. */
    if(GET(before->dentist,DISP_WRITE)==127){
        if((uint64_t)r->disp_khz*512>(uint64_t)before->vco_khz*4)return DCN302_DFS_UNSUPPORTED;
        disp=127;
    }
    *after=*before;after->dentist=SET(SET(SET(before->dentist,DISP_WRITE,disp),DISP_READ,disp),DPP_WRITE,dpp);
    uint64_t numerator=(uint64_t)before->vco_khz*4;
    for(unsigned i=0;i<5;i++){
        if(!r->pipe_khz[i]){after->dto_control&=~dcn302_dfs_dto_enable[i];continue;}
        uint64_t need=(uint64_t)255*r->pipe_khz[i]*divider(dpp);
        uint64_t phase=(need+numerator-1)/numerator;
        if(!phase || phase>255)return DCN302_DFS_UNSUPPORTED;
        after->dto[i]=SET(SET(after->dto[i],PHASE,phase),MODULO,255);after->dto_control|=dcn302_dfs_dto_enable[i];
    }
    return decode(after);
}
static uint32_t maximum(uint32_t a,uint32_t b){return a>b?a:b;}
enum dcn302_dfs_error dcn302_dfs_prepare(const dcn302_io *io,const dcn302_dfs_request *r,dcn302_dfs_transaction *t){
    if(!t)return DCN302_DFS_INPUT;
    memset(t,0,sizeof(*t));
    if(!r)return t->error=DCN302_DFS_INPUT;
    enum dcn302_dfs_error e=dcn302_dfs_read(io,&t->before);if(e)return t->error=e;
    if(GET(t->before.dentist,MODE))return t->error=DCN302_DFS_UNSUPPORTED;
    t->request=*r;e=plan(&t->before,r,&t->after);if(e)return t->error=e;
    t->required_disp_floor_mhz=maximum(t->before.disp_floor_mhz,t->after.disp_floor_mhz);
    t->required_dpp_floor_mhz=maximum(t->before.dpp_floor_mhz,t->after.dpp_floor_mhz);
    t->prepared=true;return t->error=DCN302_DFS_OK;
}
static enum dcn302_dfs_error quiet(const dcn302_io *io){
    for(unsigned sweep=0;sweep<2;sweep++)for(unsigned i=0;i<5;i++){
        uint32_t c,k,v;
        if(!io->read(io->context,dcn302_register_bytes[i][DCN302_R_CONTROL],&c) ||
           !io->read(io->context,dcn302_register_bytes[i][DCN302_R_CLOCK],&k) ||
           !io->read(io->context,dcn302_register_bytes[i][DCN302_R_VTG],&v))return DCN302_DFS_IO;
        if((c&(DCN302_MASTER_ENABLE_MASK|DCN302_MASTER_ACTIVE_MASK)) || (k&DCN302_BUSY_MASK) || (v&DCN302_VTG_ENABLE_MASK))return DCN302_DFS_BUSY;
    }
    return DCN302_DFS_OK;
}
static enum dcn302_dfs_error set_divider(const dcn302_io *io,uint64_t (*now)(void *),uint32_t pll,unsigned did,bool display){
    enum dcn302_dfs_error e=quiet(io);if(e)return e;
    uint32_t initial;
    if(!io->read(io->context,DCN302_DFS_DENTIST_BYTES,&initial))return DCN302_DFS_IO;
    uint32_t value=display?SET(initial,DISP_WRITE,did):SET(initial,DPP_WRITE,did);
    if(value==initial)return DCN302_DFS_OK;
    if(!io->write(io->context,DCN302_DFS_DENTIST_BYTES,value))return DCN302_DFS_IO;
    uint64_t start=now(io->context),last=start;unsigned delay=display?50:5,limit=display?2000:100;
    for(unsigned n=0;n<=limit;n++){
        uint32_t current,vco;
        if(!io->read(io->context,DCN302_DFS_PLL_BYTES,&vco) || !io->read(io->context,DCN302_DFS_DENTIST_BYTES,&current))return DCN302_DFS_IO;
        if(vco!=pll || ((current^value)&~STATUS))return DCN302_DFS_READBACK;
        uint64_t time=now(io->context);
        if(time<last || time-start>(uint64_t)limit*delay)return DCN302_DFS_TIMEOUT;
        last=time;
        if(GET(current,DISP_DONE) && GET(current,DPP_DONE) && GET(current,DISP_READ)==GET(current,DISP_WRITE))return quiet(io);
        if(n==limit || !io->delay_us(io->context,delay))return n==limit?DCN302_DFS_TIMEOUT:DCN302_DFS_IO;
    }
    return DCN302_DFS_TIMEOUT;
}
static enum dcn302_dfs_error write_exact(const dcn302_io *io,uint32_t address,uint32_t value){
    enum dcn302_dfs_error e=quiet(io);if(e)return e;
    if(!io->write(io->context,address,value))return DCN302_DFS_IO;
    uint32_t current;
    if(!io->read(io->context,address,&current))return DCN302_DFS_IO;
    return current==value?DCN302_DFS_OK:DCN302_DFS_READBACK;
}
static enum dcn302_dfs_error program(const dcn302_io *io,uint64_t (*now)(void *),const dcn302_dfs_snapshot *target){
    dcn302_dfs_snapshot current;enum dcn302_dfs_error e=quiet(io);if(e)return e;
    e=dcn302_dfs_read(io,&current);if(e)return e;
    if(current.pll!=target->pll || GET(current.dentist,MODE)!=GET(target->dentist,MODE) ||
       ((GET(current.dentist,DISP_WRITE)==127)!=(GET(target->dentist,DISP_WRITE)==127)))return DCN302_DFS_UNSUPPORTED;
    e=set_divider(io,now,target->pll,GET(target->dentist,DISP_WRITE),true);if(e)return e;
    e=set_divider(io,now,target->pll,GET(target->dentist,DPP_WRITE),false);if(e)return e;
    /* Every pipe is stopped, so global clocks/DTO can be changed without the
     * live raise/lower ordering required by AMD's running-pipe path. */
    for(unsigned i=0;i<5;i++)if(current.dto[i]!=target->dto[i]){
        uint32_t old;if(!io->read(io->context,dcn302_dfs_dto_bytes[i],&old))return DCN302_DFS_IO;
        uint32_t value=(old&~(DCN302_DFS_PHASE_MASK|DCN302_DFS_MODULO_MASK))|(target->dto[i]&(DCN302_DFS_PHASE_MASK|DCN302_DFS_MODULO_MASK));
        e=write_exact(io,dcn302_dfs_dto_bytes[i],value);if(e)return e;
    }
    uint32_t control;if(!io->read(io->context,DCN302_DFS_DTO_CTRL_BYTES,&control))return DCN302_DFS_IO;
    uint32_t mask=0;for(unsigned i=0;i<5;i++)mask|=dcn302_dfs_dto_enable[i];
    uint32_t value=(control&~mask)|(target->dto_control&mask);
    if(value!=control){e=write_exact(io,DCN302_DFS_DTO_CTRL_BYTES,value);if(e)return e;}
    e=dcn302_dfs_read(io,&current);if(e)return e;
    if(!same(&current,target))return DCN302_DFS_READBACK;
    return quiet(io);
}
static bool usable(const dcn302_io *io,uint64_t (*now)(void *),const dcn302_dfs_transaction *t){
    if(!io || !io->read || !io->write || !io->delay_us || !now || !t || !t->prepared || !t->before.valid || !t->after.valid)return false;
    dcn302_dfs_snapshot before=t->before,after;
    return !decode(&before) && !memcmp(&before,&t->before,sizeof(before)) && !GET(before.dentist,MODE) &&
        !plan(&before,&t->request,&after) && !memcmp(&after,&t->after,sizeof(after)) &&
        t->required_disp_floor_mhz==maximum(t->before.disp_floor_mhz,after.disp_floor_mhz) && t->required_dpp_floor_mhz==maximum(t->before.dpp_floor_mhz,after.dpp_floor_mhz);
}
static bool authorized(const dcn302_io *io,uint64_t (*now)(void *),const dcn302_smu *smu,const dcn302_dfs_transaction *t){
    return smu && smu->io.context==io->context && smu->io.read==io->read && smu->io.write==io->write && smu->io.delay_us==io->delay_us && smu->time_us==now &&
       smu->ready && !smu->busy && !smu->poisoned && smu->floor_known[DCN302_SMU_DISPCLK] && smu->floor_known[DCN302_SMU_DPPCLK] &&
       smu->floor_mhz[DCN302_SMU_DISPCLK]>=t->required_disp_floor_mhz && smu->floor_mhz[DCN302_SMU_DPPCLK]>=t->required_dpp_floor_mhz;
}
enum dcn302_dfs_error dcn302_dfs_restore_disabled(const dcn302_io *io,uint64_t (*now)(void *),const dcn302_smu *smu,dcn302_dfs_transaction *t){
    if(!usable(io,now,t))return DCN302_DFS_INPUT;
    if(!authorized(io,now,smu,t)){t->poisoned=true;return t->error=DCN302_DFS_ROLLBACK;}
    dcn302_dfs_snapshot current;enum dcn302_dfs_error e=dcn302_dfs_read(io,&current);
    if(e || smu->floor_mhz[DCN302_SMU_DISPCLK]<current.disp_floor_mhz ||
       smu->floor_mhz[DCN302_SMU_DPPCLK]<current.dpp_floor_mhz){t->poisoned=true;return t->error=DCN302_DFS_ROLLBACK;}
    /* program begins with a completed stable snapshot, never resends a still
     * pending divider request after a timeout or uncertain posted write. */
    e=program(io,now,&t->before);
    if(e){t->poisoned=true;return t->error=DCN302_DFS_ROLLBACK;}
    t->dirty=t->applied=t->poisoned=false;return t->error=DCN302_DFS_OK;
}
enum dcn302_dfs_error dcn302_dfs_apply_disabled(const dcn302_io *io,uint64_t (*now)(void *),const dcn302_smu *smu,dcn302_dfs_transaction *t){
    if(!usable(io,now,t))return DCN302_DFS_INPUT;
    if(t->dirty || t->applied || t->poisoned)return t->error=DCN302_DFS_BUSY;
    if(!authorized(io,now,smu,t))return t->error=DCN302_DFS_PREREQUISITE;
    enum dcn302_dfs_error e=quiet(io);if(e)return t->error=e;
    dcn302_dfs_snapshot current;e=dcn302_dfs_read(io,&current);if(e)return t->error=e;
    if(!same(&current,&t->before))return t->error=DCN302_DFS_READBACK;
    t->dirty=true;e=program(io,now,&t->after);
    if(e){
        enum dcn302_dfs_error original=e;
        if(dcn302_dfs_restore_disabled(io,now,smu,t))return DCN302_DFS_ROLLBACK;
        return t->error=original;
    }
    t->applied=true;return t->error=DCN302_DFS_OK;
}
