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
/* DIG0_DIG_FE_CNTL__DIG_START_MASK (Linux v6.12 dcn302 sh_mask): bit 10 of the DIG front-end control register. */
#define DCN302_HDMI_DIG_START_MASK 0x00000400u
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
/* The AFMT memory power state is a status that follows access, not a condition for programming the packet registers. */
static bool quiescent(const dcn302_io *io,unsigned inst,unsigned link){
    uint32_t be,clock;
    return rd(io,link,DCN302_HDMI_R_BE_ENABLE,&be) && !GET(be,LINK_ENABLE) &&
        rd(io,inst,DCN302_HDMI_R_AUDIO_CLOCK,&clock) && GET(clock,CLOCK_ENABLE) && GET(clock,CLOCK_ON);
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
    /* The link was a DVI link before we made it HDMI: give it back as it was (the link is disabled here). */
    if(t->mode_changed){
        if(set_readback(io,t->link,DCN302_HDMI_R_BE,DCN302_HDMI_LINK_MODE_MASK,0,t->registers[DCN302_HDMI_R_BE]))t->mode_changed=false;
        else ok=false;
    }
    /* The audio clock was off before (UEFI GOP leaves it off in DVI mode): switch it off again, last. */
    if(t->clock_changed){
        /* Best effort: an AFMT clock left running is harmless, and a failure here must never keep the picture from coming back. */
        (void)set_readback(io,t->instance,DCN302_HDMI_R_AUDIO_CLOCK,DCN302_HDMI_CLOCK_ENABLE_MASK,0,t->registers[DCN302_HDMI_R_AUDIO_CLOCK]);
        t->clock_changed=false;
    }
    if(ok){t->prepared=false;t->committed=false;}
    return ok;
}
enum dcn302_hdmi_error dcn302_hdmi_restore(const dcn302_io *io,dcn302_hdmi_transaction *t){
    if(!t || !t->valid || !valid(io,t->instance) || t->link>=5)return DCN302_HDMI_INPUT;
    /* A failed prepare rolls itself back; the sequencer's undo of that step must then find nothing left to do. */
    if(!t->prepared && !t->mode_changed && !t->clock_changed)return DCN302_HDMI_OK;
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
    /* TMDS only: HDMI (3) as the video BIOS usually leaves it, or DVI (2) as UEFI GOP does for many monitors; DVI is
     * switched to HDMI below (data islands, infoframes and audio need DIG_MODE 3, Linux dcn10_link_encoder_setup). */
    if((GET(be,LINK_MODE)!=3 && GET(be,LINK_MODE)!=2) || GET(be,FE_SOURCE)!=(1u<<inst))return DCN302_HDMI_INPUT;
    (void)clock_control;(void)power;   /* the AFMT audio clock is switched on below when it is off (Linux enc1_se_enable_audio_clock) */
    for(unsigned n=0;n<DCN302_HDMI_REGISTER_COUNT;n++){
        unsigned index=(n==DCN302_HDMI_R_BE || n==DCN302_HDMI_R_BE_ENABLE)?link:inst;
        if(!rd(io,index,(enum dcn302_hdmi_register)n,&t->registers[n]))return DCN302_HDMI_IO;
    }
    /* Do not publish a valid snapshot after routing/clock ownership changed
     * between the initial checks and the saved-register reads. */
    if(GET(t->registers[DCN302_HDMI_R_BE_ENABLE],LINK_ENABLE))return DCN302_HDMI_BUSY;
    if((GET(t->registers[DCN302_HDMI_R_BE],LINK_MODE)!=3 && GET(t->registers[DCN302_HDMI_R_BE],LINK_MODE)!=2) ||
       GET(t->registers[DCN302_HDMI_R_BE],FE_SOURCE)!=(1u<<inst))return DCN302_HDMI_INPUT;
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
    /* Linux enc1_se_enable_audio_clock: REG_UPDATE(AFMT_CNTL, AFMT_AUDIO_CLOCK_EN, 1), then wait for AFMT_AUDIO_CLOCK_ON (10 us x 1000).
     * The video BIOS / GOP leaves it off while the link runs in DVI mode (no data islands), so requiring it on made every real HDMI setup fail. */
    if(!GET(t->registers[DCN302_HDMI_R_AUDIO_CLOCK],CLOCK_ENABLE)){
        t->clock_changed=true;
        if(!set_readback(io,inst,DCN302_HDMI_R_AUDIO_CLOCK,DCN302_HDMI_CLOCK_ENABLE_MASK,0,DCN302_HDMI_CLOCK_ENABLE_MASK))
            return restore(io,t)?DCN302_HDMI_IO:DCN302_HDMI_ROLLBACK;
    }
    {
        bool on=false;
        for(unsigned n=0;n<=1000 && !on;n++){
            uint32_t c;
            if(!rd(io,inst,DCN302_HDMI_R_AUDIO_CLOCK,&c))return restore(io,t)?DCN302_HDMI_IO:DCN302_HDMI_ROLLBACK;
            on=GET(c,CLOCK_ENABLE) && GET(c,CLOCK_ON);
            if(!on && io->delay_us && !io->delay_us(io->context,10))break;
        }
        if(!on)return restore(io,t)?DCN302_HDMI_CLOCK:DCN302_HDMI_ROLLBACK;
        /* The packet memory wakes on first access; give it a moment but never fail on its power state. */
        for(unsigned n=0;n<100;n++){
            uint32_t p;
            if(!rd(io,inst,DCN302_HDMI_R_AFMT_POWER,&p))return restore(io,t)?DCN302_HDMI_IO:DCN302_HDMI_ROLLBACK;
            if(!GET(p,POWER_STATE))break;
            if(io->delay_us && !io->delay_us(io->context,10))break;
        }
    }
    if(GET(t->registers[DCN302_HDMI_R_BE],LINK_MODE)==2){
        t->mode_changed=true;       /* set first: a half-done write is reverted by restore() */
        if(!set_readback(io,link,DCN302_HDMI_R_BE,DCN302_HDMI_LINK_MODE_MASK,0,SET(t->registers[DCN302_HDMI_R_BE],LINK_MODE,3)))
            return restore(io,t)?DCN302_HDMI_IO:DCN302_HDMI_ROLLBACK;
    }
    if(!set_readback(io,inst,DCN302_HDMI_R_GC,DCN302_HDMI_AVMUTE_MASK,0,DCN302_HDMI_AVMUTE_MASK) ||
       !set_readback(io,inst,DCN302_HDMI_R_AFMT_PACKET,DCN302_HDMI_SAMPLE_SEND_MASK,0,0))return restore(io,t)?DCN302_HDMI_IO:DCN302_HDMI_ROLLBACK;
    for(unsigned n=0;n<sizeof(owned)/sizeof(*owned);n++){
        const owned_reg *o=&owned[n];uint32_t mask=o->mask;if(o->reg==DCN302_HDMI_R_VBI)mask|=dcn302_hdmi_acp_mask[inst];
        if(!set_readback(io,inst,o->reg,mask,o->pulse,target[o->reg]))return restore(io,t)?DCN302_HDMI_IO:DCN302_HDMI_ROLLBACK;
    }
    /* Reset the DIG front-end FIFO after the pixel clock changed: Linux (enc3_stream_encoder_hdmi_set_stream_attribute) does this through the
     * VBIOS ENCODER_CONTROL SETUP table, or - without the table - DIG_FE_CNTL.DIG_START = 1, 1 us, 0, 1 us. Without it the FIFO keeps the
     * phase of the old clock and the first picture can come out garbled or not at all although every register reads back correctly. */
    {
        uint32_t fe;
        if(!rd(io,inst,DCN302_HDMI_R_FE,&fe))return restore(io,t)?DCN302_HDMI_IO:DCN302_HDMI_ROLLBACK;
        bool ok=wr(io,inst,DCN302_HDMI_R_FE,fe|DCN302_HDMI_DIG_START_MASK);
        if(ok && io->delay_us)ok=io->delay_us(io->context,1);
        if(!wr(io,inst,DCN302_HDMI_R_FE,fe&~DCN302_HDMI_DIG_START_MASK)){ok=false;(void)wr(io,inst,DCN302_HDMI_R_FE,fe&~DCN302_HDMI_DIG_START_MASK);}   /* never leave the FIFO held in reset */
        if(ok && io->delay_us)ok=io->delay_us(io->context,1);
        uint32_t after=DCN302_HDMI_DIG_START_MASK;
        for(unsigned attempt=0;attempt<3;attempt++){   /* DIG_START must read back 0: a lost clearing write is repeated */
            if(!rd(io,inst,DCN302_HDMI_R_FE,&after)){ok=false;break;}
            if(!(after&DCN302_HDMI_DIG_START_MASK))break;
            ok=false;
            if(!wr(io,inst,DCN302_HDMI_R_FE,after&~DCN302_HDMI_DIG_START_MASK))continue;
        }
        if(after&DCN302_HDMI_DIG_START_MASK)ok=false;
        if(!ok)return restore(io,t)?DCN302_HDMI_IO:DCN302_HDMI_ROLLBACK;
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
/* Digital back end on/off without the VBIOS transmitter table. On Navi23 Linux runs that table inside the display microcontroller
 * (DMCUB); this board's VBIOS does not even list it. Only the back-end enable bit is touched: the PHY and the pixel clock stay as the
 * firmware left them, which is what an audio-only change at the running mode needs. */
enum dcn302_hdmi_error dcn302_hdmi_link_disable(const dcn302_io *io,unsigned link,uint32_t *saved){
    if(!valid(io,link) || !saved)return DCN302_HDMI_INPUT;
    uint32_t v;
    if(!rd(io,link,DCN302_HDMI_R_BE_ENABLE,&v))return DCN302_HDMI_IO;
    *saved=v;
    if(!GET(v,LINK_ENABLE))return DCN302_HDMI_OK;
    if(!set_readback(io,link,DCN302_HDMI_R_BE_ENABLE,DCN302_HDMI_LINK_ENABLE_MASK,0,v&~DCN302_HDMI_LINK_ENABLE_MASK)){
        (void)wr(io,link,DCN302_HDMI_R_BE_ENABLE,v);   /* a failed disable must not leave the link half off */
        return DCN302_HDMI_READBACK;
    }
    return DCN302_HDMI_OK;
}
enum dcn302_hdmi_error dcn302_hdmi_link_restore(const dcn302_io *io,unsigned link,uint32_t saved){
    if(!valid(io,link))return DCN302_HDMI_INPUT;
    uint32_t v;
    if(!rd(io,link,DCN302_HDMI_R_BE_ENABLE,&v))return DCN302_HDMI_IO;
    if((v&DCN302_HDMI_LINK_ENABLE_MASK)==(saved&DCN302_HDMI_LINK_ENABLE_MASK))return DCN302_HDMI_OK;
    if(!set_readback(io,link,DCN302_HDMI_R_BE_ENABLE,DCN302_HDMI_LINK_ENABLE_MASK,0,(v&~DCN302_HDMI_LINK_ENABLE_MASK)|(saved&DCN302_HDMI_LINK_ENABLE_MASK)))return DCN302_HDMI_READBACK;
    return DCN302_HDMI_OK;
}
