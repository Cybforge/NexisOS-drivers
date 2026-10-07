/*
 * Native DCN302 HDMI/AFMT packet programming adapted from AMD's Linux v6.12
 * dcn30_dio_stream_encoder.c and dcn30_afmt.c.
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "dcn302_hdmi.h"
#include <string.h>
#define GET(v,f) (((v)&DCN302_HDMI_##f##_MASK)>>DCN302_HDMI_##f##_SHIFT)
#define SET(v,f,n) (((v)&~DCN302_HDMI_##f##_MASK)|(((uint32_t)(n)<<DCN302_HDMI_##f##_SHIFT)&DCN302_HDMI_##f##_MASK))
typedef struct {enum dcn302_hdmi_register reg;uint32_t mask,pulse;} owned_reg;
static const owned_reg owned[]={
    {DCN302_HDMI_R_FE,DCN302_HDMI_RGB_ENCODING_MASK|DCN302_HDMI_COLOR_FORMAT_MASK,0},
    {DCN302_HDMI_R_CONTROL,DCN302_HDMI_PACKET_VERSION_MASK|DCN302_HDMI_KEEPOUT_MASK|DCN302_HDMI_DEEP_ENABLE_MASK|DCN302_HDMI_DEEP_DEPTH_MASK|DCN302_HDMI_SCRAMBLE_MASK|DCN302_HDMI_CLOCK_RATIO_MASK|DCN302_HDMI_NO_EXTRA_NULL_MASK,0},
    {DCN302_HDMI_R_VBI,DCN302_HDMI_GC_CONT_MASK|DCN302_HDMI_GC_SEND_MASK|DCN302_HDMI_NULL_SEND_MASK,0},
    {DCN302_HDMI_R_INFO0,DCN302_HDMI_INFO_SEND_MASK,0},{DCN302_HDMI_R_INFO1,DCN302_HDMI_INFO_LINE_MASK,0},
    {DCN302_HDMI_R_AUDIO,DCN302_HDMI_AUDIO_DELAY_MASK,0},
    {DCN302_HDMI_R_ACR,DCN302_HDMI_ACR_SEND_MASK|DCN302_HDMI_ACR_SOURCE_MASK|DCN302_HDMI_ACR_PRIORITY_MASK|DCN302_HDMI_ACR_MULTIPLE_MASK,0},
    {DCN302_HDMI_R_CTS32,DCN302_HDMI_CTS32_MASK,0},{DCN302_HDMI_R_N32,DCN302_HDMI_N32_MASK,0},
    {DCN302_HDMI_R_CTS44,DCN302_HDMI_CTS44_MASK,0},{DCN302_HDMI_R_N44,DCN302_HDMI_N44_MASK,0},
    {DCN302_HDMI_R_CTS48,DCN302_HDMI_CTS48_MASK,0},{DCN302_HDMI_R_N48,DCN302_HDMI_N48_MASK,0},
    {DCN302_HDMI_R_AFMT_PACKET2,DCN302_HDMI_LAYOUT_OVERRIDE_MASK|DCN302_HDMI_OSF_OVERRIDE_MASK|DCN302_HDMI_CHANNELS_MASK,0},
    {DCN302_HDMI_R_AFMT_SOURCE,DCN302_HDMI_SOURCE_MASK,0},
    {DCN302_HDMI_R_CS0,DCN302_HDMI_CHANNEL_L_MASK|DCN302_HDMI_CLOCK_ACCURACY_MASK,0},
    {DCN302_HDMI_R_CS1,DCN302_HDMI_CHANNEL_R_MASK,0},
    {DCN302_HDMI_R_CS2,DCN302_HDMI_CHANNEL2_MASK|DCN302_HDMI_CHANNEL3_MASK|DCN302_HDMI_CHANNEL4_MASK|DCN302_HDMI_CHANNEL5_MASK|DCN302_HDMI_CHANNEL6_MASK|DCN302_HDMI_CHANNEL7_MASK,0},
    {DCN302_HDMI_R_AFMT_INFO,0,DCN302_HDMI_INFO_UPDATE_MASK},
    {DCN302_HDMI_R_AFMT_PACKET,DCN302_HDMI_SAMPLE_SEND_MASK,DCN302_HDMI_CS_UPDATE_MASK},
};
static bool valid(const dcn302_io *io,unsigned instance){return io && io->read && io->write && instance<5;}
static bool rd(const dcn302_io *io,unsigned instance,enum dcn302_hdmi_register r,uint32_t *v){return io->read(io->context,dcn302_hdmi_register_bytes[instance][r],v);}
static bool wr(const dcn302_io *io,unsigned instance,enum dcn302_hdmi_register r,uint32_t v){return io->write(io->context,dcn302_hdmi_register_bytes[instance][r],v);}
static bool set_readback(const dcn302_io *io,unsigned inst,enum dcn302_hdmi_register r,uint32_t mask,uint32_t pulse,uint32_t value){
    uint32_t now,after;
    if(!rd(io,inst,r,&now) || !wr(io,inst,r,(now&~(mask|pulse))|(value&(mask|pulse))) || !rd(io,inst,r,&after))return false;
    return !((after^value)&mask); /* UPDATE strobes can self-clear; never compare/replay them. */
}
static bool quiescent(const dcn302_io *io,unsigned inst,unsigned link){
    uint32_t be,clock,power;
    return rd(io,link,DCN302_HDMI_R_BE_ENABLE,&be) && !GET(be,LINK_ENABLE) &&
        rd(io,inst,DCN302_HDMI_R_AUDIO_CLOCK,&clock) && GET(clock,CLOCK_ENABLE) && GET(clock,CLOCK_ON) &&
        rd(io,inst,DCN302_HDMI_R_AFMT_POWER,&power) && !GET(power,POWER_STATE);
}
static bool restore(const dcn302_io *io,dcn302_hdmi_transaction *t){
    bool ok=true;
    if(!set_readback(io,t->instance,DCN302_HDMI_R_AFMT_PACKET,DCN302_HDMI_SAMPLE_SEND_MASK,0,0))ok=false;
    if(!set_readback(io,t->instance,DCN302_HDMI_R_GC,DCN302_HDMI_AVMUTE_MASK,0,DCN302_HDMI_AVMUTE_MASK))ok=false;
    for(unsigned n=sizeof(owned)/sizeof(*owned);n;n--){
        const owned_reg *o=&owned[n-1];if(o->reg==DCN302_HDMI_R_AFMT_PACKET)continue;
        uint32_t mask=o->mask;if(o->reg==DCN302_HDMI_R_VBI)mask|=dcn302_hdmi_acp_mask[t->instance];
        if(!set_readback(io,t->instance,o->reg,mask,o->pulse,t->registers[o->reg]&~o->pulse))ok=false;
    }
    if(!set_readback(io,t->instance,DCN302_HDMI_R_AFMT_PACKET,DCN302_HDMI_SAMPLE_SEND_MASK,DCN302_HDMI_CS_UPDATE_MASK,t->registers[DCN302_HDMI_R_AFMT_PACKET]&~DCN302_HDMI_CS_UPDATE_MASK))ok=false;
    if(!set_readback(io,t->instance,DCN302_HDMI_R_GC,DCN302_HDMI_AVMUTE_MASK,0,t->registers[DCN302_HDMI_R_GC]))ok=false;
    if(ok){t->prepared=false;t->committed=false;}
    return ok;
}
enum dcn302_hdmi_error dcn302_hdmi_restore(const dcn302_io *io,dcn302_hdmi_transaction *t){
    if(!t || !t->valid || !valid(io,t->instance) || t->link>=5)return DCN302_HDMI_INPUT;
    if(!quiescent(io,t->instance,t->link))return DCN302_HDMI_BUSY;
    return restore(io,t)?DCN302_HDMI_OK:DCN302_HDMI_ROLLBACK;
}
enum dcn302_hdmi_error dcn302_hdmi_prepare(const dcn302_io *io,unsigned inst,unsigned link,uint32_t clock,uint8_t scdc,bool audio,unsigned source,dcn302_hdmi_transaction *t){
    if(!t)return DCN302_HDMI_INPUT;
    memset(t,0,sizeof(*t));
    if(!valid(io,inst) || link>=5 || clock<25000 || clock>600000 || source>6 || scdc>3 || (clock>340000?scdc!=3:(scdc&2)!=0))return DCN302_HDMI_INPUT;
    t->instance=inst;t->link=link;t->audio=audio;
    uint32_t be,clock_control,power,enabled;
    if(!rd(io,link,DCN302_HDMI_R_BE_ENABLE,&enabled) || !rd(io,link,DCN302_HDMI_R_BE,&be) ||
       !rd(io,inst,DCN302_HDMI_R_AUDIO_CLOCK,&clock_control) || !rd(io,inst,DCN302_HDMI_R_AFMT_POWER,&power))return DCN302_HDMI_IO;
    if(GET(enabled,LINK_ENABLE))return DCN302_HDMI_BUSY;
    if(GET(be,LINK_MODE)!=3 || GET(be,FE_SOURCE)!=(1u<<inst))return DCN302_HDMI_INPUT;
    if(!GET(clock_control,CLOCK_ENABLE) || !GET(clock_control,CLOCK_ON) || GET(power,POWER_STATE))return DCN302_HDMI_CLOCK;
    for(unsigned n=0;n<DCN302_HDMI_REGISTER_COUNT;n++){
        unsigned index=(n==DCN302_HDMI_R_BE || n==DCN302_HDMI_R_BE_ENABLE)?link:inst;
        if(!rd(io,index,(enum dcn302_hdmi_register)n,&t->registers[n]))return DCN302_HDMI_IO;
    }
    /* Do not publish a valid snapshot after routing/clock ownership changed
     * between the initial checks and the saved-register reads. */
    if(GET(t->registers[DCN302_HDMI_R_BE_ENABLE],LINK_ENABLE))return DCN302_HDMI_BUSY;
    if(GET(t->registers[DCN302_HDMI_R_BE],LINK_MODE)!=3 ||
       GET(t->registers[DCN302_HDMI_R_BE],FE_SOURCE)!=(1u<<inst))return DCN302_HDMI_INPUT;
    if(!GET(t->registers[DCN302_HDMI_R_AUDIO_CLOCK],CLOCK_ENABLE) || !GET(t->registers[DCN302_HDMI_R_AUDIO_CLOCK],CLOCK_ON) ||
       GET(t->registers[DCN302_HDMI_R_AFMT_POWER],POWER_STATE))return DCN302_HDMI_CLOCK;
    if(GET(t->registers[DCN302_HDMI_R_FE],PIPE)>=5)return DCN302_HDMI_INPUT;
    t->valid=true;uint32_t target[DCN302_HDMI_REGISTER_COUNT];memcpy(target,t->registers,sizeof(target));
    target[DCN302_HDMI_R_FE]=SET(SET(target[DCN302_HDMI_R_FE],RGB_ENCODING,0),COLOR_FORMAT,0);
    uint32_t c=target[DCN302_HDMI_R_CONTROL];c=SET(SET(SET(SET(c,PACKET_VERSION,1),KEEPOUT,1),DEEP_ENABLE,0),DEEP_DEPTH,0);
    target[DCN302_HDMI_R_CONTROL]=SET(SET(SET(c,SCRAMBLE,scdc&1),CLOCK_RATIO,(scdc>>1)&1),NO_EXTRA_NULL,1);
    target[DCN302_HDMI_R_VBI]=SET(SET(SET(target[DCN302_HDMI_R_VBI],GC_CONT,1),GC_SEND,1),NULL_SEND,1)&~dcn302_hdmi_acp_mask[inst];
    target[DCN302_HDMI_R_INFO0]=SET(target[DCN302_HDMI_R_INFO0],INFO_SEND,audio?1:0);
    target[DCN302_HDMI_R_INFO1]=SET(target[DCN302_HDMI_R_INFO1],INFO_LINE,2);
    target[DCN302_HDMI_R_AUDIO]=SET(target[DCN302_HDMI_R_AUDIO],AUDIO_DELAY,audio?1:0);
    target[DCN302_HDMI_R_ACR]=SET(SET(SET(SET(target[DCN302_HDMI_R_ACR],ACR_SEND,audio?1:0),ACR_SOURCE,0),ACR_PRIORITY,0),ACR_MULTIPLE,1);
    /* Hardware chooses an ACR family from HDA sample rate. This backend uses
     * stereo PCM48. Program all base families coherently, avoiding the invalid
     * N=6272 / CTS=pixel_khz combination (49 kHz rather than 44.1 kHz). */
    target[DCN302_HDMI_R_CTS32]=SET(target[DCN302_HDMI_R_CTS32],CTS32,clock);target[DCN302_HDMI_R_N32]=SET(target[DCN302_HDMI_R_N32],N32,4096);
    target[DCN302_HDMI_R_CTS44]=SET(target[DCN302_HDMI_R_CTS44],CTS44,((uint64_t)clock*10+4)/9);target[DCN302_HDMI_R_N44]=SET(target[DCN302_HDMI_R_N44],N44,6272);
    target[DCN302_HDMI_R_CTS48]=SET(target[DCN302_HDMI_R_CTS48],CTS48,clock);target[DCN302_HDMI_R_N48]=SET(target[DCN302_HDMI_R_N48],N48,6144);
    target[DCN302_HDMI_R_AFMT_PACKET2]=SET(SET(SET(target[DCN302_HDMI_R_AFMT_PACKET2],LAYOUT_OVERRIDE,0),OSF_OVERRIDE,0),CHANNELS,audio?3:0);
    target[DCN302_HDMI_R_AFMT_SOURCE]=SET(target[DCN302_HDMI_R_AFMT_SOURCE],SOURCE,source);
    target[DCN302_HDMI_R_CS0]=SET(SET(target[DCN302_HDMI_R_CS0],CHANNEL_L,1),CLOCK_ACCURACY,0);
    target[DCN302_HDMI_R_CS1]=SET(target[DCN302_HDMI_R_CS1],CHANNEL_R,2);
    target[DCN302_HDMI_R_CS2]=SET(SET(SET(SET(SET(SET(target[DCN302_HDMI_R_CS2],CHANNEL2,3),CHANNEL3,4),CHANNEL4,5),CHANNEL5,6),CHANNEL6,7),CHANNEL7,8);
    target[DCN302_HDMI_R_AFMT_INFO]|=DCN302_HDMI_INFO_UPDATE_MASK;
    target[DCN302_HDMI_R_AFMT_PACKET]=SET(target[DCN302_HDMI_R_AFMT_PACKET],SAMPLE_SEND,0)|DCN302_HDMI_CS_UPDATE_MASK;
    if(!set_readback(io,inst,DCN302_HDMI_R_GC,DCN302_HDMI_AVMUTE_MASK,0,DCN302_HDMI_AVMUTE_MASK) ||
       !set_readback(io,inst,DCN302_HDMI_R_AFMT_PACKET,DCN302_HDMI_SAMPLE_SEND_MASK,0,0))return restore(io,t)?DCN302_HDMI_IO:DCN302_HDMI_ROLLBACK;
    for(unsigned n=0;n<sizeof(owned)/sizeof(*owned);n++){
        const owned_reg *o=&owned[n];uint32_t mask=o->mask;if(o->reg==DCN302_HDMI_R_VBI)mask|=dcn302_hdmi_acp_mask[inst];
        if(!set_readback(io,inst,o->reg,mask,o->pulse,target[o->reg]))return restore(io,t)?DCN302_HDMI_IO:DCN302_HDMI_ROLLBACK;
    }
    t->prepared=true;return DCN302_HDMI_OK;
}
enum dcn302_hdmi_error dcn302_hdmi_commit(const dcn302_io *io,dcn302_hdmi_transaction *t){
    if(!t || !t->valid || !t->prepared || !valid(io,t->instance) || t->link>=5)return DCN302_HDMI_INPUT;
    uint32_t be,clock,route,fe;
    if(!rd(io,t->link,DCN302_HDMI_R_BE,&route) || !rd(io,t->instance,DCN302_HDMI_R_FE,&fe))return DCN302_HDMI_IO;
    if(GET(route,LINK_MODE)!=3 || GET(route,FE_SOURCE)!=(1u<<t->instance) ||
       GET(fe,PIPE)!=GET(t->registers[DCN302_HDMI_R_FE],PIPE))return DCN302_HDMI_INPUT;
    if(!rd(io,t->link,DCN302_HDMI_R_BE_ENABLE,&be) || !rd(io,t->instance,DCN302_HDMI_R_AUDIO_CLOCK,&clock))return DCN302_HDMI_IO;
    if(!GET(be,LINK_ENABLE) || !GET(be,LINK_CLOCK) || !GET(clock,CLOCK_ENABLE) || !GET(clock,CLOCK_ON))return DCN302_HDMI_CLOCK;
    if(!set_readback(io,t->instance,DCN302_HDMI_R_AFMT_PACKET,DCN302_HDMI_SAMPLE_SEND_MASK,0,t->audio?1:0) ||
       !set_readback(io,t->instance,DCN302_HDMI_R_GC,DCN302_HDMI_AVMUTE_MASK,0,0)){
        /* Keep the failed commit muted; parent must disable link and restore. */
        bool muted=set_readback(io,t->instance,DCN302_HDMI_R_AFMT_PACKET,DCN302_HDMI_SAMPLE_SEND_MASK,0,0);
        if(!set_readback(io,t->instance,DCN302_HDMI_R_GC,DCN302_HDMI_AVMUTE_MASK,0,DCN302_HDMI_AVMUTE_MASK))muted=false;
        t->committed=false;
        return muted?DCN302_HDMI_IO:DCN302_HDMI_ROLLBACK;
    }
    t->committed=true;return DCN302_HDMI_OK;
}
