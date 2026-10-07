/* Native DAL SMU protocol adapted from AMD Linux v6.12.
 * Copyright 2020 Advanced Micro Devices, Inc.
 * Copyright 2022-2024 Advanced Micro Devices, Inc.
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
#include "dcn302_smu.h"
#include <string.h>
enum {MSG_TEST=1,MSG_VERSION=2,MSG_INTERFACE=3,MSG_HEADER=4,MSG_MIN=9,MSG_DPM=11};
static bool supported(unsigned clock){return clock==2 || (clock>=8 && clock<=11);}
static bool error(dcn302_smu *s,enum dcn302_smu_error e,bool poison){s->error=e;s->poisoned|=poison;return false;}
static bool wait_response(dcn302_smu *s,uint32_t *response){
    uint64_t start=s->time_us(s->io.context),last=start;
    for(unsigned n=0;n<=200000;n++){
        uint64_t now=s->time_us(s->io.context);
        if(now<last || now-start>2000000)return error(s,now<last?DCN302_SMU_PROTOCOL:DCN302_SMU_TIMEOUT,true);
        last=now;
        if(!s->io.read(s->io.context,DCN302_SMU_RESPONSE_BYTES,response))return error(s,DCN302_SMU_IO,true);
        if(*response)return true;
        if(n==200000)return error(s,DCN302_SMU_TIMEOUT,true);
        if(!s->io.delay_us(s->io.context,10))return error(s,DCN302_SMU_IO,true);
    }
    return error(s,DCN302_SMU_TIMEOUT,true);
}
static bool response_status(dcn302_smu *s,uint32_t r){
    switch(r){
        case 1:return true;
        case 0xfc:return error(s,DCN302_SMU_BUSY,false);
        case 0xfd:return error(s,DCN302_SMU_PREREQUISITE,false);
        case 0xfe:return error(s,DCN302_SMU_UNSUPPORTED,false);
        case 0xff:return error(s,DCN302_SMU_FAILED,false);
        default:return error(s,DCN302_SMU_PROTOCOL,true);
    }
}
static bool restore_untriggered(dcn302_smu *s,uint32_t argument,uint32_t response){
    /* Never rewrite the message register: that would replay a command. Before
     * our trigger only the argument/response data registers were changed. */
    uint32_t v;bool ok=s->io.write(s->io.context,DCN302_SMU_ARGUMENT_BYTES,argument);
    if(!s->io.read(s->io.context,DCN302_SMU_ARGUMENT_BYTES,&v) || v!=argument)ok=false;
    if(!s->io.write(s->io.context,DCN302_SMU_RESPONSE_BYTES,response))ok=false;
    if(!s->io.read(s->io.context,DCN302_SMU_RESPONSE_BYTES,&v) || v!=response)ok=false;
    return ok;
}
static bool send(dcn302_smu *s,unsigned message,uint32_t parameter,uint32_t *out){
    *out=0;
    if(s->busy || s->poisoned)return error(s,s->busy?DCN302_SMU_BUSY:DCN302_SMU_PROTOCOL,false);
    s->busy=true;s->error=DCN302_SMU_OK;s->dispatched=false;
    uint32_t prior_response,prior_argument,value;
    bool result=false;
    if(!wait_response(s,&prior_response))goto done;
    /* A completed previous error is a ready mailbox too; it belongs to the
     * prior caller. Unrecognized status values do not authorize takeover. */
    if(prior_response!=1 && (prior_response<0xfc || prior_response>0xff)){error(s,DCN302_SMU_PROTOCOL,true);goto done;}
    if(!s->io.read(s->io.context,DCN302_SMU_ARGUMENT_BYTES,&prior_argument)){error(s,DCN302_SMU_IO,true);goto done;}
    if(!s->io.write(s->io.context,DCN302_SMU_RESPONSE_BYTES,0) ||
       !s->io.read(s->io.context,DCN302_SMU_RESPONSE_BYTES,&value) || value ||
       !s->io.write(s->io.context,DCN302_SMU_ARGUMENT_BYTES,parameter) ||
       !s->io.read(s->io.context,DCN302_SMU_ARGUMENT_BYTES,&value) || value!=parameter){
        error(s,DCN302_SMU_READBACK,false);
        if(!restore_untriggered(s,prior_argument,prior_response))error(s,DCN302_SMU_ROLLBACK,true);
        goto done;
    }
    /* A failed posted trigger can still have reached hardware. Do not fake
     * completion or resend/reset its mailbox; the owner must prove state. */
    s->dispatched=true;
    if(!s->io.write(s->io.context,DCN302_SMU_MESSAGE_BYTES,message)){error(s,DCN302_SMU_IO,true);goto done;}
    if(!wait_response(s,&value) || !response_status(s,value))goto done;
    if(!s->io.read(s->io.context,DCN302_SMU_ARGUMENT_BYTES,out)){error(s,DCN302_SMU_IO,true);goto done;}
    result=true;
done:
    s->busy=false;if(!result)*out=0;else s->error=DCN302_SMU_OK;return result;
}
bool dcn302_smu_open(dcn302_smu *s,const dcn302_io *io,uint64_t (*time_us)(void *)){
    if(!s)return false;
    memset(s,0,sizeof(*s));
    if(!io || !io->read || !io->write || !io->delay_us || !time_us)return error(s,DCN302_SMU_INPUT,false);
    s->io=*io;s->time_us=time_us;
    uint32_t value;
    if(!send(s,MSG_TEST,0x4e455849,&value))return false;
    if(value!=0x4e45584a)return error(s,DCN302_SMU_PROTOCOL,true);
    if(!send(s,MSG_VERSION,0,&value))return false;
    if(!value || value==UINT32_MAX)return error(s,DCN302_SMU_VERSION,false);
    s->version=value;
    if(!send(s,MSG_HEADER,0,&value))return false;
    if(value!=1)return error(s,DCN302_SMU_VERSION,false);
    if(!send(s,MSG_INTERFACE,0,&value))return false;
    if(value!=DCN302_SMU_INTERFACE)return error(s,DCN302_SMU_VERSION,false);
    s->ready=true;s->error=DCN302_SMU_OK;return true;
}
bool dcn302_smu_clock_limits(dcn302_smu *s,enum dcn302_smu_clock clock,dcn302_smu_limits *out){
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!s || !s->ready || !supported(clock))return s?error(s,DCN302_SMU_INPUT,false):false;
    if(s->busy || s->poisoned)return error(s,s->busy?DCN302_SMU_BUSY:DCN302_SMU_PROTOCOL,false);
    /* A failed refresh must not leave stale limits authorizing a setter. */
    memset(&s->clocks[clock],0,sizeof(s->clocks[clock]));
    uint32_t features;
    if(!send(s,MSG_DPM,(uint32_t)clock<<16|0xff,&features))return false;
    unsigned count=features&0xff;bool fine=(features&0x80000000u)!=0;
    if(!count || count>DCN302_SMU_MAX_LEVELS || (features&~0xf00000ffu) || (fine && count!=2))return error(s,DCN302_SMU_PROTOCOL,false);
    dcn302_smu_limits limits={0};limits.count=(uint8_t)count;limits.features=features;limits.fine_grained=fine;
    for(unsigned n=0;n<count;n++){
        uint32_t mhz;
        if(!send(s,MSG_DPM,(uint32_t)clock<<16|n,&mhz))return false;
        if(!mhz || mhz>UINT16_MAX || (n && mhz<=limits.frequency_mhz[n-1]))return error(s,DCN302_SMU_PROTOCOL,false);
        limits.frequency_mhz[n]=(uint16_t)mhz;
    }
    limits.valid=true;s->clocks[clock]=limits;*out=limits;return true;
}
bool dcn302_smu_set_floor(dcn302_smu *s,enum dcn302_smu_clock clock,uint32_t mhz,uint32_t *out){
    if(!out)return false;
    *out=0;
    if(!s || !s->ready || !supported(clock) || !mhz || mhz>UINT16_MAX)return s?error(s,DCN302_SMU_INPUT,false):false;
    if(s->busy || s->poisoned)return error(s,s->busy?DCN302_SMU_BUSY:DCN302_SMU_PROTOCOL,false);
    const dcn302_smu_limits *limits=&s->clocks[clock];
    if(!limits->valid || !limits->count || mhz<limits->frequency_mhz[0] || mhz>limits->frequency_mhz[limits->count-1])return error(s,DCN302_SMU_INPUT,false);
    uint32_t actual;
    if(!send(s,MSG_MIN,(uint32_t)clock<<16|mhz,&actual)){
        if(s->dispatched && s->error!=DCN302_SMU_BUSY && s->error!=DCN302_SMU_PREREQUISITE && s->error!=DCN302_SMU_UNSUPPORTED)s->floor_known[clock]=false;
        return false;
    }
    if(actual<mhz || actual>limits->frequency_mhz[limits->count-1]){
        s->floor_known[clock]=false;return error(s,DCN302_SMU_READBACK,true);
    }
    s->floor_known[clock]=true;s->floor_mhz[clock]=(uint16_t)actual;*out=actual;return true;
}
