/*
 * Native DCN302 timing programming adapted from AMD's Linux v6.12
 * dcn10_optc.c and dcn30_optc.c. Original Copyright 2016/2020 AMD, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include "dcn302_otg.h"
#include <string.h>
static const enum dcn302_register owned[]={
    DCN302_R_H_TOTAL,DCN302_R_H_BLANK,DCN302_R_H_SYNC,DCN302_R_H_POL,
    DCN302_R_V_TOTAL,DCN302_R_V_MIN,DCN302_R_V_MAX,DCN302_R_V_BLANK,
    DCN302_R_V_SYNC,DCN302_R_V_POL,DCN302_R_INTERLACE,DCN302_R_CONTROL,
    DCN302_R_V_STARTUP,DCN302_R_V_UPDATE,DCN302_R_V_READY,DCN302_R_VTG,
};
#define GET(value,field) (((value)&DCN302_##field##_MASK)>>DCN302_##field##_SHIFT)
#define SET(value,field,n) (((value)&~DCN302_##field##_MASK)|(((uint32_t)(n)<<DCN302_##field##_SHIFT)&DCN302_##field##_MASK))
static bool valid_io(const dcn302_io *io,unsigned pipe){return io && io->read && io->write && io->delay_us && pipe<5;}
static bool read_reg(const dcn302_io *io,unsigned pipe,enum dcn302_register reg,uint32_t *value){return io->read(io->context,dcn302_register_bytes[pipe][reg],value);}
static bool write_reg(const dcn302_io *io,unsigned pipe,enum dcn302_register reg,uint32_t value){return io->write(io->context,dcn302_register_bytes[pipe][reg],value);}
bool dcn302_otg_snapshot(const dcn302_io *io,unsigned pipe,dcn302_snapshot *snapshot){
    if(!io || !io->read || pipe>=5 || !snapshot)return false;
    memset(snapshot,0,sizeof(*snapshot));snapshot->pipe=pipe;
    for(unsigned n=0;n<DCN302_REGISTER_COUNT;n++)if(!read_reg(io,pipe,(enum dcn302_register)n,&snapshot->registers[n]))return false;
    snapshot->valid=true;return true;
}
static bool geometry(const nexis_gpu_timing *t){
    return t && t->hactive && t->vactive && !(t->flags&~3u) && t->hactive<t->hsync_start && t->hsync_start<t->hsync_end &&
        t->hsync_end<=t->htotal && t->htotal<=32768 && t->htotal-t->hactive>=32 && t->hsync_end-t->hsync_start>=4 &&
        t->vactive<t->vsync_start && t->vsync_start<t->vsync_end && t->vsync_end<=t->vtotal && t->vtotal<=32768 && t->vtotal-t->vactive>=3;
}
static bool progressive_rgb(const dcn302_snapshot *s){
    return !GET(s->registers[DCN302_R_INTERLACE],INTERLACE) && !GET(s->registers[DCN302_R_H_DIV],H_DIV) &&
        !GET(s->registers[DCN302_R_FORMAT],FORMAT) && !GET(s->registers[DCN302_R_FORMAT],DSC) &&
        GET(s->registers[DCN302_R_SOURCE],SEG0)<5 &&
        (!GET(s->registers[DCN302_R_SOURCE],SEGMENTS) || GET(s->registers[DCN302_R_SOURCE],SEG1)==15);
}
static bool disabled_clocked(const dcn302_snapshot *s){
    return !GET(s->registers[DCN302_R_CONTROL],MASTER_ENABLE) && !GET(s->registers[DCN302_R_CONTROL],MASTER_ACTIVE) &&
        GET(s->registers[DCN302_R_CLOCK],CLOCK_ENABLE) && GET(s->registers[DCN302_R_CLOCK],CLOCK_ON) &&
        !GET(s->registers[DCN302_R_CLOCK],SOFT_RESET) && !GET(s->registers[DCN302_R_CLOCK],BUSY);
}
static bool shape(const dcn302_snapshot *s,nexis_gpu_timing *out){
    if(!progressive_rgb(s))return false;
    uint32_t hs=GET(s->registers[DCN302_R_H_BLANK],H_BLANK_START),he=GET(s->registers[DCN302_R_H_BLANK],H_BLANK_END);
    uint32_t vs=GET(s->registers[DCN302_R_V_BLANK],V_BLANK_START),ve=GET(s->registers[DCN302_R_V_BLANK],V_BLANK_END);
    nexis_gpu_timing t={0};t.htotal=GET(s->registers[DCN302_R_H_TOTAL],H_TOTAL)+1;t.vtotal=GET(s->registers[DCN302_R_V_TOTAL],V_TOTAL)+1;
    if(hs<he || hs>t.htotal || vs<ve || vs>t.vtotal || GET(s->registers[DCN302_R_H_SYNC],H_SYNC_START) || GET(s->registers[DCN302_R_V_SYNC],V_SYNC_START))return false;
    t.hactive=hs-he;t.vactive=vs-ve;t.hsync_start=t.hactive+t.htotal-hs;t.hsync_end=t.hsync_start+GET(s->registers[DCN302_R_H_SYNC],H_SYNC_END);
    t.vsync_start=t.vactive+t.vtotal-vs;t.vsync_end=t.vsync_start+GET(s->registers[DCN302_R_V_SYNC],V_SYNC_END);
    t.flags=(!GET(s->registers[DCN302_R_H_POL],H_POL)?1u:0)|(!GET(s->registers[DCN302_R_V_POL],V_POL)?2u:0);
    if(!geometry(&t))return false;
    *out=t;return true;
}
bool dcn302_otg_read_shape(const dcn302_io *io,unsigned pipe,nexis_gpu_timing *out,bool *active){
    if(!out || !active)return false;
    memset(out,0,sizeof(*out));*active=false;dcn302_snapshot s;
    if(!dcn302_otg_snapshot(io,pipe,&s) || !shape(&s,out))return false;
    *active=GET(s.registers[DCN302_R_CONTROL],MASTER_ENABLE) && GET(s.registers[DCN302_R_CONTROL],MASTER_ACTIVE) &&
        GET(s.registers[DCN302_R_CLOCK],CLOCK_ENABLE) && GET(s.registers[DCN302_R_CLOCK],CLOCK_ON) && !GET(s.registers[DCN302_R_CLOCK],SOFT_RESET);
    return true;
}
bool dcn302_otg_frame_count(const dcn302_io *io,unsigned pipe,uint32_t *count){
    uint32_t value;if(!io || !io->read || pipe>=5 || !count || !read_reg(io,pipe,DCN302_R_FRAME_COUNT,&value))return false;
    *count=GET(value,FRAME_COUNT);return true;
}
static enum dcn302_error wait_running(const dcn302_io *io,unsigned pipe,bool enabled){
    for(unsigned n=0;n<=20000;n++){
        uint32_t control,clock;
        if(!read_reg(io,pipe,DCN302_R_CONTROL,&control) || !read_reg(io,pipe,DCN302_R_CLOCK,&clock))return DCN302_IO;
        if(GET(control,MASTER_ENABLE)==(unsigned)enabled && GET(control,MASTER_ACTIVE)==(unsigned)enabled &&
           (enabled || !GET(clock,BUSY)))return DCN302_OK;
        if(n==20000)return DCN302_TIMEOUT;
        if(!io->delay_us(io->context,10))return DCN302_IO;
    }
    return DCN302_TIMEOUT;
}
enum dcn302_error dcn302_otg_disable(const dcn302_io *io,unsigned pipe){
    if(!valid_io(io,pipe))return DCN302_INPUT;
    dcn302_snapshot old;if(!dcn302_otg_snapshot(io,pipe,&old))return DCN302_IO;
    if(GET(old.registers[DCN302_R_LOCK],LOCK) || GET(old.registers[DCN302_R_LOCK],LOCK_STATUS))return DCN302_BUSY;
    uint32_t control=SET(SET(old.registers[DCN302_R_CONTROL],DISABLE_POINT,3),MASTER_ENABLE,0);
    if(!write_reg(io,pipe,DCN302_R_CONTROL,control))return DCN302_IO;
    if(!write_reg(io,pipe,DCN302_R_VTG,SET(old.registers[DCN302_R_VTG],VTG_ENABLE,0)))return DCN302_IO;
    enum dcn302_error result=wait_running(io,pipe,false);if(result!=DCN302_OK)return result;
    uint32_t value;
    if(!read_reg(io,pipe,DCN302_R_VTG,&value))return DCN302_IO;
    return !GET(value,VTG_ENABLE)?DCN302_OK:DCN302_READBACK;
}
enum dcn302_error dcn302_otg_enable(const dcn302_io *io,unsigned pipe){
    if(!valid_io(io,pipe))return DCN302_INPUT;
    dcn302_snapshot old;if(!dcn302_otg_snapshot(io,pipe,&old))return DCN302_IO;
    if(!disabled_clocked(&old) || GET(old.registers[DCN302_R_LOCK],LOCK) || GET(old.registers[DCN302_R_LOCK],LOCK_STATUS))return DCN302_BUSY;
    nexis_gpu_timing timing;if(!progressive_rgb(&old) || !shape(&old,&timing))return DCN302_UNSUPPORTED;
    uint32_t before;if(!dcn302_otg_frame_count(io,pipe,&before))return DCN302_IO;
    if(!write_reg(io,pipe,DCN302_R_VTG,SET(old.registers[DCN302_R_VTG],VTG_ENABLE,1)) ||
       !write_reg(io,pipe,DCN302_R_CONTROL,SET(SET(old.registers[DCN302_R_CONTROL],DISABLE_POINT,3),MASTER_ENABLE,1)))return DCN302_IO;
    uint32_t vtg;if(!read_reg(io,pipe,DCN302_R_VTG,&vtg))return DCN302_IO;
    if(!GET(vtg,VTG_ENABLE))return DCN302_READBACK;
    enum dcn302_error result=wait_running(io,pipe,true);if(result!=DCN302_OK)return result;
    for(unsigned n=0;n<=20000;n++){
        uint32_t count;if(!dcn302_otg_frame_count(io,pipe,&count))return DCN302_IO;
        if(count!=before)return DCN302_OK;
        if(n==20000)return DCN302_TIMEOUT;
        if(!io->delay_us(io->context,10))return DCN302_IO;
    }
    return DCN302_TIMEOUT;
}
static enum dcn302_error wait_lock(const dcn302_io *io,unsigned pipe,bool locked){
    for(unsigned n=0;n<11;n++){
        uint32_t value;if(!read_reg(io,pipe,DCN302_R_LOCK,&value))return DCN302_IO;
        if(GET(value,LOCK_STATUS)==(unsigned)locked)return DCN302_OK;
        if(n<10 && !io->delay_us(io->context,1))return DCN302_IO;
    }
    return DCN302_TIMEOUT;
}
static enum dcn302_error lock(const dcn302_io *io,unsigned pipe,uint32_t global2){
    if(!write_reg(io,pipe,DCN302_R_GLOBAL2,SET(global2,LOCK_SELECT,pipe)) || !write_reg(io,pipe,DCN302_R_LOCK,1))return DCN302_IO;
    return wait_lock(io,pipe,true);
}
static bool unlock(const dcn302_io *io,unsigned pipe,uint32_t global2){
    bool ok=write_reg(io,pipe,DCN302_R_LOCK,0);
    if(wait_lock(io,pipe,false)!=DCN302_OK)ok=false;
    if(!write_reg(io,pipe,DCN302_R_GLOBAL2,global2))ok=false;
    return ok;
}
static bool restore(const dcn302_io *io,const dcn302_snapshot *s){
    bool ok=true;
    for(unsigned n=sizeof(owned)/sizeof(*owned);n;n--)if(!write_reg(io,s->pipe,owned[n-1],s->registers[owned[n-1]]))ok=false;
    for(unsigned n=0;n<sizeof(owned)/sizeof(*owned);n++){
        uint32_t value;if(!read_reg(io,s->pipe,owned[n],&value) || value!=s->registers[owned[n]])ok=false;
    }
    if(!unlock(io,s->pipe,s->registers[DCN302_R_GLOBAL2]))ok=false;
    return ok;
}
enum dcn302_error dcn302_otg_restore_disabled(const dcn302_io *io,const dcn302_snapshot *saved){
    if(!saved || !saved->valid || !valid_io(io,saved->pipe) || !disabled_clocked(saved))return DCN302_INPUT;
    dcn302_snapshot current;if(!dcn302_otg_snapshot(io,saved->pipe,&current))return DCN302_IO;
    if(!disabled_clocked(&current) || GET(current.registers[DCN302_R_LOCK],LOCK) || GET(current.registers[DCN302_R_LOCK],LOCK_STATUS))return DCN302_BUSY;
    enum dcn302_error result=lock(io,saved->pipe,current.registers[DCN302_R_GLOBAL2]);
    if(result!=DCN302_OK){if(!unlock(io,saved->pipe,current.registers[DCN302_R_GLOBAL2]))return DCN302_ROLLBACK;return result;}
    return restore(io,saved)?DCN302_OK:DCN302_ROLLBACK;
}
enum dcn302_error dcn302_otg_program_disabled(const dcn302_io *io,unsigned pipe,const nexis_gpu_timing *timing,const dcn302_sync *sync){
    if(!valid_io(io,pipe) || !geometry(timing) || !timing->pixel_khz || timing->pixel_khz>4000000 || !sync || !sync->vstartup || sync->vstartup>1023 || sync->vstartup>timing->vtotal || sync->vready>65535 ||
       sync->vupdate_offset>65535 || !sync->vupdate_width || sync->vupdate_width>1023)return DCN302_INPUT;
    uint32_t vblank_start=timing->vtotal-(timing->vsync_start-timing->vactive),vblank_end=vblank_start-timing->vactive;
    uint32_t fp2=sync->vstartup>vblank_end+1?sync->vstartup-vblank_end-1:0;
    if(fp2>32767)return DCN302_INPUT;
    dcn302_snapshot old;if(!dcn302_otg_snapshot(io,pipe,&old))return DCN302_IO;
    if(!disabled_clocked(&old) || GET(old.registers[DCN302_R_LOCK],LOCK) || GET(old.registers[DCN302_R_LOCK],LOCK_STATUS))return DCN302_BUSY;
    if(!progressive_rgb(&old) || (old.registers[DCN302_R_V_CONTROL]&12))return DCN302_UNSUPPORTED;
    dcn302_snapshot target=old;uint32_t *r=target.registers;
    uint32_t hblank_start=timing->htotal-(timing->hsync_start-timing->hactive),hblank_end=hblank_start-timing->hactive;
    r[DCN302_R_H_TOTAL]=SET(r[DCN302_R_H_TOTAL],H_TOTAL,timing->htotal-1);
    r[DCN302_R_H_BLANK]=SET(SET(r[DCN302_R_H_BLANK],H_BLANK_START,hblank_start),H_BLANK_END,hblank_end);
    r[DCN302_R_H_SYNC]=SET(SET(r[DCN302_R_H_SYNC],H_SYNC_START,0),H_SYNC_END,timing->hsync_end-timing->hsync_start);
    r[DCN302_R_H_POL]=SET(r[DCN302_R_H_POL],H_POL,(timing->flags&1)?0:1);
    r[DCN302_R_V_TOTAL]=SET(r[DCN302_R_V_TOTAL],V_TOTAL,timing->vtotal-1);
    r[DCN302_R_V_MIN]=SET(r[DCN302_R_V_MIN],V_MIN,timing->vtotal-1);r[DCN302_R_V_MAX]=SET(r[DCN302_R_V_MAX],V_MAX,timing->vtotal-1);
    r[DCN302_R_V_BLANK]=SET(SET(r[DCN302_R_V_BLANK],V_BLANK_START,vblank_start),V_BLANK_END,vblank_end);
    r[DCN302_R_V_SYNC]=SET(SET(r[DCN302_R_V_SYNC],V_SYNC_START,0),V_SYNC_END,timing->vsync_end-timing->vsync_start);
    r[DCN302_R_V_POL]=SET(r[DCN302_R_V_POL],V_POL,(timing->flags&2)?0:1);
    r[DCN302_R_INTERLACE]=SET(r[DCN302_R_INTERLACE],INTERLACE,0);
    r[DCN302_R_CONTROL]=SET(SET(r[DCN302_R_CONTROL],START_POINT,sync->display_port?1:0),FIELD_NUMBER,0);
    r[DCN302_R_V_STARTUP]=SET(r[DCN302_R_V_STARTUP],V_STARTUP,sync->vstartup);
    r[DCN302_R_V_UPDATE]=SET(SET(r[DCN302_R_V_UPDATE],V_UPDATE_OFFSET,sync->vupdate_offset),V_UPDATE_WIDTH,sync->vupdate_width);
    r[DCN302_R_V_READY]=SET(r[DCN302_R_V_READY],V_READY,sync->vready);
    r[DCN302_R_VTG]=SET(SET(SET(r[DCN302_R_VTG],VTG_ENABLE,0),VTG_INIT,vblank_start),VTG_FP2,fp2);
    enum dcn302_error result=lock(io,pipe,old.registers[DCN302_R_GLOBAL2]);
    if(result!=DCN302_OK){if(!unlock(io,pipe,old.registers[DCN302_R_GLOBAL2]))return DCN302_ROLLBACK;return result;}
    for(unsigned n=0;n<sizeof(owned)/sizeof(*owned);n++)if(!write_reg(io,pipe,owned[n],r[owned[n]])){result=DCN302_IO;break;}
    if(result==DCN302_OK)for(unsigned n=0;n<sizeof(owned)/sizeof(*owned);n++){
        uint32_t readback;if(!read_reg(io,pipe,owned[n],&readback)){result=DCN302_IO;break;}
        if(readback!=r[owned[n]]){result=DCN302_READBACK;break;}
    }
    if(result!=DCN302_OK)return restore(io,&old)?result:DCN302_ROLLBACK;
    if(!unlock(io,pipe,old.registers[DCN302_R_GLOBAL2]))return restore(io,&old)?DCN302_IO:DCN302_ROLLBACK;
    return DCN302_OK;
}
