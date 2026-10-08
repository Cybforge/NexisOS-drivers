/* Native DCN3.02 HDMI audio endpoint + wall-clock DTO. Sequence and register meaning follow AMD's Linux
 * v6.12 dce_audio.c; register addresses/fields come from the generated, checksum-pinned header. */
#include "dcn302_audio.h"
#include <string.h>

#define GETM(v,m) (((v)&(m))>>__builtin_ctz(m))
#define SETM(v,m,n) (((v)&~(m))|(((uint32_t)(n)<<__builtin_ctz(m))&(m)))
#define PHASE_24MHZ_100HZ 240000u /* get_azalia_clock_info_hdmi: 24 MHz in 100-Hz units */

static bool io_ok(const dcn302_io *io){return io && io->read && io->write && io->delay_us;}
static bool az_read(const dcn302_io *io,unsigned ep,enum dcn302_az_register r,uint32_t *value){
    return io->write(io->context,dcn302_az_index_bytes[ep],dcn302_az_ix[r]) && io->read(io->context,dcn302_az_data_bytes[ep],value);
}
static bool az_write(const dcn302_io *io,unsigned ep,enum dcn302_az_register r,uint32_t value){
    return io->write(io->context,dcn302_az_index_bytes[ep],dcn302_az_ix[r]) && io->write(io->context,dcn302_az_data_bytes[ep],value);
}
static const uint32_t dto_bytes[3]={DCN302_DTO_SOURCE_BYTES,DCN302_DTO0_PHASE_BYTES,DCN302_DTO0_MODULE_BYTES};
/* Bits the transaction owns per register; the rest (status, reserved, other policy) is preserved. */
static uint32_t owned(enum dcn302_az_register r){
    switch(r){
        case DCN302_AZ_R_HOT_PLUG:return DCN302_AZ_HOT_PLUG_AUDIO_ENABLED_MASK|DCN302_AZ_HOT_PLUG_CLOCK_GATING_DISABLE_MASK;
        case DCN302_AZ_R_CHANNEL_SPEAKER:return DCN302_AZ_SPEAKER_ALLOCATION_MASK|DCN302_AZ_HDMI_CONNECTION_MASK|DCN302_AZ_DP_CONNECTION_MASK;
        case DCN302_AZ_R_PIN_SENSE:return 0; /* read-only presence detect */
        case DCN302_AZ_R_SINK_INFO0:return DCN302_AZ_SINK_MANUFACTURER_ID_MASK|DCN302_AZ_SINK_PRODUCT_ID_MASK;
        case DCN302_AZ_R_SINK_INFO1:return DCN302_AZ_SINK_DESCRIPTION_LEN_MASK;
        default:return 0xffffffffu; /* AUDIO_DESCRIPTORn: whole word */
    }
}
/* Registers the apply step writes (HOT_PLUG belongs to enable/disable, PIN_SENSE is read-only). */
static bool applied_by_apply(enum dcn302_az_register r){return r!=DCN302_AZ_R_HOT_PLUG && r!=DCN302_AZ_R_PIN_SENSE;}
static bool snapshot(const dcn302_io *io,const dcn302_audio_transaction *t,uint32_t *regs,uint32_t *dto){
    for(unsigned r=0;r<DCN302_AZ_REGISTER_COUNT;r++)if(!az_read(io,t->endpoint,(enum dcn302_az_register)r,&regs[r]))return false;
    for(unsigned n=0;n<3;n++)if(!io->read(io->context,dto_bytes[n],&dto[n]))return false;
    return true;
}
static bool usable(const dcn302_io *io,const dcn302_audio_transaction *t){
    return io_ok(io) && t && t->prepared && t->endpoint<DCN302_AZ_ENDPOINTS && t->guard && io->context==t->owner.context &&
        io->read==t->owner.read && io->write==t->owner.write && io->delay_us==t->owner.delay_us;
}
static bool matches(const uint32_t *regs,const uint32_t *dto,const uint32_t *want_regs,const uint32_t *want_dto,bool include_hot_plug){
    for(unsigned r=0;r<DCN302_AZ_REGISTER_COUNT;r++){
        if(r==DCN302_AZ_R_PIN_SENSE)continue;
        if(r==DCN302_AZ_R_HOT_PLUG && !include_hot_plug)continue;
        uint32_t mask=owned((enum dcn302_az_register)r);
        if((regs[r]^want_regs[r])&mask)return false;
    }
    for(unsigned n=0;n<3;n++){
        uint32_t mask=n==0?(DCN302_AZ_DTO0_SOURCE_SEL_MASK|DCN302_AZ_DTO_SEL_MASK):0xffffffffu;
        if((dto[n]^want_dto[n])&mask)return false;
    }
    return true;
}
enum dcn302_error dcn302_audio_prepare(const dcn302_io *io,unsigned endpoint,unsigned otg,uint32_t pixel_khz,
    const nx_audio_caps *caps,bool (*guard)(void *),dcn302_audio_transaction *t){
    if(!t)return DCN302_INPUT;
    memset(t,0,sizeof(*t));
    if(!io_ok(io) || endpoint>=DCN302_AZ_ENDPOINTS || otg>=5 || !guard || !caps || !nx_audio_caps_stereo48(caps) ||
       pixel_khz<25000 || pixel_khz>600000)return t->error=DCN302_INPUT;
    t->owner=*io;t->guard=guard;t->endpoint=endpoint;t->otg=otg;
    uint32_t again[DCN302_AZ_REGISTER_COUNT],dto_again[3];
    if(!snapshot(io,t,t->before,t->dto_before) || !snapshot(io,t,again,dto_again))return t->error=DCN302_IO;
    for(unsigned r=0;r<DCN302_AZ_REGISTER_COUNT;r++)
        if(r!=DCN302_AZ_R_PIN_SENSE && ((t->before[r]^again[r])&~DCN302_AZ_HOT_PLUG_CLOCK_ON_STATE_MASK)&owned((enum dcn302_az_register)r))return t->error=DCN302_READBACK;
    for(unsigned n=0;n<3;n++)if(t->dto_before[n]!=dto_again[n])return t->error=DCN302_READBACK;
    memcpy(t->after,t->before,sizeof(t->after));
    uint32_t cs=t->before[DCN302_AZ_R_CHANNEL_SPEAKER];
    cs=SETM(cs,DCN302_AZ_SPEAKER_ALLOCATION_MASK,caps->speakers);
    cs=SETM(cs,DCN302_AZ_HDMI_CONNECTION_MASK,1);
    cs=SETM(cs,DCN302_AZ_DP_CONNECTION_MASK,0);
    t->after[DCN302_AZ_R_CHANNEL_SPEAKER]=cs;
    /* Descriptor 0 = linear PCM: stereo (max channels field is channels-1), the monitor's rates and sample sizes. */
    uint32_t d=0;
    d=SETM(d,DCN302_AZ_DESC_MAX_CHANNELS_MASK,1);
    d=SETM(d,DCN302_AZ_DESC_FREQUENCIES_MASK,caps->lpcm_rates);
    d=SETM(d,DCN302_AZ_DESC_BYTE2_MASK,caps->lpcm_sizes);
    d=SETM(d,DCN302_AZ_DESC_FREQUENCIES_STEREO_MASK,caps->lpcm_rates);
    t->after[DCN302_AZ_R_DESCRIPTOR0]=d;
    for(unsigned n=1;n<=13;n++)t->after[DCN302_AZ_R_DESCRIPTOR0+n]=0; /* no compressed formats */
    uint32_t s0=t->before[DCN302_AZ_R_SINK_INFO0];
    s0=SETM(s0,DCN302_AZ_SINK_MANUFACTURER_ID_MASK,caps->manufacturer);
    s0=SETM(s0,DCN302_AZ_SINK_PRODUCT_ID_MASK,caps->product);
    t->after[DCN302_AZ_R_SINK_INFO0]=s0;
    t->after[DCN302_AZ_R_SINK_INFO1]=SETM(t->before[DCN302_AZ_R_SINK_INFO1],DCN302_AZ_SINK_DESCRIPTION_LEN_MASK,caps->name_length);
    /* Wall clock DTO0: 24 MHz phase over the pixel clock (100-Hz units), selected from this pipe's OTG. */
    t->dto_after[0]=SETM(SETM(t->dto_before[0],DCN302_AZ_DTO0_SOURCE_SEL_MASK,otg),DCN302_AZ_DTO_SEL_MASK,0);
    t->dto_after[1]=PHASE_24MHZ_100HZ;
    t->dto_after[2]=pixel_khz*10u;
    t->prepared=true;
    return t->error=DCN302_OK;
}
static enum dcn302_error write_set(const dcn302_io *io,dcn302_audio_transaction *t,const uint32_t *regs,const uint32_t *dto,bool reverse){
    uint32_t now_regs[DCN302_AZ_REGISTER_COUNT],now_dto[3];
    if(!t->guard(io->context))return DCN302_BUSY;
    if(!snapshot(io,t,now_regs,now_dto))return DCN302_IO;
    /* DTO source/select first, then module and phase (Linux comment: otherwise no HDMI audio at boot). */
    for(unsigned k=0;k<3;k++){
        unsigned n=reverse?2-k:k;
        uint32_t mask=n==0?(DCN302_AZ_DTO0_SOURCE_SEL_MASK|DCN302_AZ_DTO_SEL_MASK):0xffffffffu;
        uint32_t value=(now_dto[n]&~mask)|(dto[n]&mask);
        if(value==now_dto[n])continue;
        t->dirty=true;
        if(!io->write(io->context,dto_bytes[n],value))return DCN302_IO;
    }
    for(unsigned r=0;r<DCN302_AZ_REGISTER_COUNT;r++){
        if(!applied_by_apply((enum dcn302_az_register)r))continue;
        uint32_t mask=owned((enum dcn302_az_register)r),value=(now_regs[r]&~mask)|(regs[r]&mask);
        if(value==now_regs[r])continue;
        t->dirty=true;
        if(!az_write(io,t->endpoint,(enum dcn302_az_register)r,value))return DCN302_IO;
    }
    if(!snapshot(io,t,now_regs,now_dto))return DCN302_IO;
    if(!matches(now_regs,now_dto,regs,dto,false))return DCN302_READBACK;
    return t->guard(io->context)?DCN302_OK:DCN302_BUSY;
}
enum dcn302_error dcn302_audio_apply(const dcn302_io *io,dcn302_audio_transaction *t){
    if(!usable(io,t))return DCN302_INPUT;
    if(t->dirty || t->applied || t->poisoned)return t->error=DCN302_BUSY;
    enum dcn302_error e=write_set(io,t,t->after,t->dto_after,false);
    if(e){
        if(t->dirty && dcn302_audio_restore(io,t))return t->error=DCN302_ROLLBACK;
        return t->error=e;
    }
    t->applied=true;
    return t->error=DCN302_OK;
}
enum dcn302_error dcn302_audio_restore(const dcn302_io *io,dcn302_audio_transaction *t){
    if(!usable(io,t))return DCN302_INPUT;
    /* Endpoint off first, then the old configuration (including the old HOT_PLUG state last). */
    uint32_t hot=0;
    bool ok=az_read(io,t->endpoint,DCN302_AZ_R_HOT_PLUG,&hot);
    if(ok && (hot&DCN302_AZ_HOT_PLUG_AUDIO_ENABLED_MASK))ok=!dcn302_audio_disable(io,t->endpoint);
    enum dcn302_error e=ok?write_set(io,t,t->before,t->dto_before,true):DCN302_IO;
    if(!e){
        uint32_t cur=0;
        uint32_t want=(t->before[DCN302_AZ_R_HOT_PLUG]&DCN302_AZ_HOT_PLUG_AUDIO_ENABLED_MASK);
        if(!az_read(io,t->endpoint,DCN302_AZ_R_HOT_PLUG,&cur))e=DCN302_IO;
        else if((cur&DCN302_AZ_HOT_PLUG_AUDIO_ENABLED_MASK)!=want){
            if(!az_write(io,t->endpoint,DCN302_AZ_R_HOT_PLUG,(cur&~DCN302_AZ_HOT_PLUG_AUDIO_ENABLED_MASK)|want) ||
               !az_read(io,t->endpoint,DCN302_AZ_R_HOT_PLUG,&cur) || (cur&DCN302_AZ_HOT_PLUG_AUDIO_ENABLED_MASK)!=want)e=DCN302_READBACK;
        }
    }
    if(e){t->poisoned=true;return t->error=DCN302_ROLLBACK;}
    t->dirty=t->applied=t->enabled=t->poisoned=false;
    return t->error=DCN302_OK;
}
enum dcn302_error dcn302_audio_enable_endpoint(const dcn302_io *io,unsigned endpoint){
    if(!io_ok(io) || endpoint>=DCN302_AZ_ENDPOINTS)return DCN302_INPUT;
    uint32_t v=0;
    if(!az_read(io,endpoint,DCN302_AZ_R_HOT_PLUG,&v))return DCN302_IO;
    /* az_enable: gating off + enabled in one write, then release the clock-gating override. */
    v|=DCN302_AZ_HOT_PLUG_CLOCK_GATING_DISABLE_MASK|DCN302_AZ_HOT_PLUG_AUDIO_ENABLED_MASK;
    if(!az_write(io,endpoint,DCN302_AZ_R_HOT_PLUG,v))return DCN302_IO;
    v&=~DCN302_AZ_HOT_PLUG_CLOCK_GATING_DISABLE_MASK;
    if(!az_write(io,endpoint,DCN302_AZ_R_HOT_PLUG,v) || !az_read(io,endpoint,DCN302_AZ_R_HOT_PLUG,&v))return DCN302_IO;
    return (v&DCN302_AZ_HOT_PLUG_AUDIO_ENABLED_MASK)?DCN302_OK:DCN302_READBACK;
}
enum dcn302_error dcn302_audio_enable(const dcn302_io *io,dcn302_audio_transaction *t){
    if(!usable(io,t) || !t->applied || t->poisoned)return DCN302_INPUT;
    /* No parent guard here: the endpoint is switched on while video already runs (the guards describe the stopped pipeline). */
    t->dirty=true;
    enum dcn302_error e=dcn302_audio_enable_endpoint(io,t->endpoint);
    if(e)return t->error=e;
    t->enabled=true;
    return t->error=DCN302_OK;
}
enum dcn302_error dcn302_audio_disable(const dcn302_io *io,unsigned endpoint){
    if(!io_ok(io) || endpoint>=DCN302_AZ_ENDPOINTS)return DCN302_INPUT;
    uint32_t v=0;
    if(!az_read(io,endpoint,DCN302_AZ_R_HOT_PLUG,&v))return DCN302_IO;
    v|=DCN302_AZ_HOT_PLUG_CLOCK_GATING_DISABLE_MASK;
    if(!az_write(io,endpoint,DCN302_AZ_R_HOT_PLUG,v))return DCN302_IO;
    v&=~DCN302_AZ_HOT_PLUG_AUDIO_ENABLED_MASK;
    if(!az_write(io,endpoint,DCN302_AZ_R_HOT_PLUG,v))return DCN302_IO;
    v&=~DCN302_AZ_HOT_PLUG_CLOCK_GATING_DISABLE_MASK;
    if(!az_write(io,endpoint,DCN302_AZ_R_HOT_PLUG,v) || !az_read(io,endpoint,DCN302_AZ_R_HOT_PLUG,&v))return DCN302_IO;
    return (v&DCN302_AZ_HOT_PLUG_AUDIO_ENABLED_MASK)?DCN302_READBACK:DCN302_OK;
}
