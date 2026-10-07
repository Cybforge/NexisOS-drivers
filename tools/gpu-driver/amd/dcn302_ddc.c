/*
 * Native Navi23 I2C controller adapted from AMD's Linux v6.12 dce_i2c_hw.c.
 * Copyright 2018 Advanced Micro Devices, Inc.
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
#include "dcn302_ddc.h"
#include <string.h>
#define GET(v,f) (((v)&DCN302_DDC_##f##_MASK)>>DCN302_DDC_##f##_SHIFT)
#define SET(v,f,n) (((v)&~DCN302_DDC_##f##_MASK)|(((uint32_t)(n)<<DCN302_DDC_##f##_SHIFT)&DCN302_DDC_##f##_MASK))
#define CONFIG_CONTROL (DCN302_DDC_SELECT_MASK|DCN302_DDC_COUNT_MASK)
#define COMMAND_CONTROL (DCN302_DDC_GO_MASK|DCN302_DDC_RESET_MASK|DCN302_DDC_STATUS_RESET_MASK|DCN302_DDC_SEND_RESET_MASK)
#define CONFIG_SETUP (DCN302_DDC_ENABLE_MASK|DCN302_DDC_LIMIT_MASK|DCN302_DDC_DRIVE_DATA_MASK|DCN302_DDC_DRIVE_CLOCK_MASK|DCN302_DDC_DRIVE_SELECT_MASK|DCN302_DDC_BYTE_DELAY_MASK|DCN302_DDC_TRANS_DELAY_MASK|DCN302_DDC_RESET_LENGTH_MASK)
#define CONFIG_SPEED (DCN302_DDC_PRESCALE_MASK|DCN302_DDC_THRESHOLD_MASK|DCN302_DDC_START_TIMING_MASK)
#define CONFIG_TRANS (DCN302_DDC_RW_MASK|DCN302_DDC_START_MASK|DCN302_DDC_STOP_MASK|DCN302_DDC_STOP_NACK_MASK|DCN302_DDC_BYTES_MASK)
static bool rd(dcn302_ddc *d,enum dcn302_ddc_register r,uint32_t *v){return d->io.read(d->io.context,dcn302_ddc_register_bytes[d->bus][r],v);}
static bool wr(dcn302_ddc *d,enum dcn302_ddc_register r,uint32_t v){return d->io.write(d->io.context,dcn302_ddc_register_bytes[d->bus][r],v);}
static bool delay(dcn302_ddc *d,uint32_t us){return d->io.delay_us(d->io.context,us);}
static bool update(dcn302_ddc *d,enum dcn302_ddc_register r,uint32_t mask,uint32_t value){
    uint32_t v;return rd(d,r,&v) && wr(d,r,(v&~mask)|(value&mask));
}
static bool restore_one(dcn302_ddc *d,enum dcn302_ddc_register r,uint32_t mask,uint32_t value){
    uint32_t v;return update(d,r,mask,value) && rd(d,r,&v) && !((v^value)&mask);
}
bool dcn302_ddc_init(dcn302_ddc *d,const dcn302_io *io,unsigned bus,uint32_t crystal_khz){
    if(!d)return false;
    memset(d,0,sizeof(*d));d->error=DCN302_DDC_INPUT;
    if(!io || !io->read || !io->write || !io->delay_us || bus>=5 || crystal_khz<1000 || crystal_khz>100000)return false;
    d->io=*io;d->bus=bus;d->crystal_khz=crystal_khz;d->ready=true;d->error=DCN302_DDC_OK;return true;
}
static bool release(dcn302_ddc *d,const uint32_t old[DCN302_DDC_REGISTER_COUNT],bool acquired,bool changed){
    bool ok=true;uint32_t owner=0;
    if(!rd(d,DCN302_DDC_R_ARBITRATION,&owner))return false;
    if(acquired && GET(owner,OWNER)!=1 && GET(owner,OWNER)!=0){
        /* Firmware has preempted us: release our request without resetting or
         * restoring configuration under its active transfer. Quarantine this
         * context because its previous configuration could not be restored. */
        (void)update(d,DCN302_DDC_R_ARBITRATION,DCN302_DDC_REQUEST_MASK|DCN302_DDC_RELEASE_MASK,DCN302_DDC_RELEASE_MASK);
        return false;
    }
    if(changed){
        uint32_t status;
        if(!rd(d,DCN302_DDC_R_SW_STATUS,&status))return false;
        if(GET(status,STATUS)==2){
            (void)update(d,DCN302_DDC_R_ARBITRATION,DCN302_DDC_REQUEST_MASK|DCN302_DDC_RELEASE_MASK,DCN302_DDC_RELEASE_MASK);
            return false; /* Never reset a firmware-owned transfer. */
        }
        uint32_t reset=SET(SET(0,STATUS_RESET,1),RESET,GET(status,STATUS)==1?1:0);
        if(!update(d,DCN302_DDC_R_CONTROL,COMMAND_CONTROL,reset))ok=false;
        if(!delay(d,1))ok=false;
        if(!update(d,DCN302_DDC_R_CONTROL,COMMAND_CONTROL,0))ok=false;
        for(unsigned n=0;n<4;n++)if(!restore_one(d,(enum dcn302_ddc_register)(DCN302_DDC_R_TRANS0+n),CONFIG_TRANS,old[DCN302_DDC_R_TRANS0+n]))ok=false;
        if(!restore_one(d,DCN302_DDC_R_CONTROL,CONFIG_CONTROL,old[DCN302_DDC_R_CONTROL]))ok=false;
        if(!restore_one(d,DCN302_DDC_R_SPEED,CONFIG_SPEED,old[DCN302_DDC_R_SPEED]))ok=false;
        if(!restore_one(d,DCN302_DDC_R_SETUP,CONFIG_SETUP,old[DCN302_DDC_R_SETUP]))ok=false;
    }
    uint32_t value=SET(SET(old[DCN302_DDC_R_ARBITRATION],REQUEST,0),RELEASE,1);
    if(!update(d,DCN302_DDC_R_ARBITRATION,DCN302_DDC_REQUEST_MASK|DCN302_DDC_RELEASE_MASK|DCN302_DDC_QUEUE_MASK,value))ok=false;
    if(!rd(d,DCN302_DDC_R_ARBITRATION,&owner) || GET(owner,REQUEST) || GET(owner,OWNER)==1)ok=false;
    if(changed && !restore_one(d,DCN302_DDC_R_POWER,DCN302_DDC_SLEEP_FORCE_MASK,old[DCN302_DDC_R_POWER]))ok=false;
    return ok;
}
bool dcn302_ddc_transfer(dcn302_ddc *d,const dcn302_ddc_payload *p,unsigned count){
    if(!d)return false;
    if(!d->ready){d->error=DCN302_DDC_INPUT;return false;}
    if(d->busy || d->poisoned){d->error=DCN302_DDC_BUSY;return false;}
    d->error=DCN302_DDC_INPUT;
    if(!p || !count || count>4)return false;
    unsigned total=0,write_bytes=0;
    for(unsigned n=0;n<count;n++){
        if(p[n].address<8 || p[n].address>0x77 || !p[n].bytes || p[n].bytes>128 || !p[n].data || (p[n].read && n!=count-1))return false;
        total+=p[n].bytes+1;
        write_bytes+=1+(p[n].read?0:p[n].bytes);
    }
    if(total>144)return false;
    d->busy=true;d->error=DCN302_DDC_IO;
    uint32_t old[DCN302_DDC_REGISTER_COUNT]={0};bool attempted=false,acquired=false,changed=false;
    uint8_t read_data[128];
    for(unsigned n=0;n<DCN302_DDC_REGISTER_COUNT;n++){
        if(n==DCN302_DDC_R_DATA)continue; /* DATA reads move the hardware FIFO pointer. */
        if(!rd(d,(enum dcn302_ddc_register)n,&old[n]))goto done;
    }
    if(GET(old[DCN302_DDC_R_ARBITRATION],OWNER) || GET(old[DCN302_DDC_R_ARBITRATION],REQUEST) || GET(old[DCN302_DDC_R_ARBITRATION],FIRMWARE_REQUEST) ||
       GET(old[DCN302_DDC_R_HW_STATUS],HW_OWNER) || GET(old[DCN302_DDC_R_HW_STATUS],HW_REQUEST) || GET(old[DCN302_DDC_R_SW_STATUS],STATUS) ||
       (old[DCN302_DDC_R_CONTROL]&COMMAND_CONTROL)){d->error=DCN302_DDC_BUSY;goto done;}
    if(GET(old[DCN302_DDC_R_GPIO],PIN_CLOCK) || GET(old[DCN302_DDC_R_GPIO],PIN_DATA) || GET(old[DCN302_DDC_R_GPIO],AUX)){
        d->error=DCN302_DDC_PAD;goto done;
    }
    uint32_t divider=GET(old[DCN302_DDC_R_TIME_BASE],XTAL_DIV);if(!divider)divider=2;
    uint32_t ref=GET(old[DCN302_DDC_R_TIME_BASE],REF_BASE);ref=ref?ref*1000:d->crystal_khz;
    uint32_t prescale=ref/divider/100;if(!prescale || prescale>65535){d->error=DCN302_DDC_INPUT;goto done;}
    attempted=true;
    uint32_t arb=SET(SET(SET(old[DCN302_DDC_R_ARBITRATION],REQUEST,1),RELEASE,0),QUEUE,0);
    if(!wr(d,DCN302_DDC_R_ARBITRATION,arb))goto done;
    for(unsigned n=0;n<21;n++){
        uint32_t v;if(!rd(d,DCN302_DDC_R_ARBITRATION,&v))goto done;
        if(GET(v,OWNER)==1){acquired=true;break;}
        if(GET(v,OWNER)==2){d->error=DCN302_DDC_BUSY;goto done;}
        if(n==20){d->error=DCN302_DDC_TIMEOUT;goto done;}
        if(!delay(d,1))goto done;
    }
    changed=true;
    if(!update(d,DCN302_DDC_R_POWER,DCN302_DDC_SLEEP_FORCE_MASK,0))goto done;
    for(unsigned n=0;n<11;n++){
        uint32_t v;if(!rd(d,DCN302_DDC_R_POWER_STATUS,&v))goto done;
        if(!GET(v,SLEEP_STATE))break;
        if(n==10){d->error=DCN302_DDC_TIMEOUT;goto done;}
        if(!delay(d,1))goto done;
    }
    uint32_t setup=SET(SET(SET(0,ENABLE,1),LIMIT,3),RESET_LENGTH,0);
    if(!restore_one(d,DCN302_DDC_R_SETUP,CONFIG_SETUP,setup))goto done;
    uint32_t speed=SET(SET(SET(0,PRESCALE,prescale),THRESHOLD,2),START_TIMING,2);
    if(!restore_one(d,DCN302_DDC_R_SPEED,CONFIG_SPEED,speed))goto done;
    uint32_t control=SET(SET(0,SELECT,d->bus),COUNT,count-1);
    if(!update(d,DCN302_DDC_R_CONTROL,CONFIG_CONTROL|COMMAND_CONTROL,SET(control,STATUS_RESET,1)) ||
       !restore_one(d,DCN302_DDC_R_CONTROL,CONFIG_CONTROL|COMMAND_CONTROL,control))goto done;
    for(unsigned n=0;n<count;n++){
        uint32_t trans=SET(SET(SET(SET(SET(0,START,1),STOP_NACK,1),RW,p[n].read?1:0),BYTES,p[n].bytes),STOP,n==count-1?1:0);
        if(!restore_one(d,(enum dcn302_ddc_register)(DCN302_DDC_R_TRANS0+n),CONFIG_TRANS,trans))goto done;
        uint32_t data=SET(0,DATA_BYTE,(p[n].address<<1)|(p[n].read?1:0));
        if(!n)data=SET(SET(data,INDEX,0),INDEX_WRITE,1);
        if(!wr(d,DCN302_DDC_R_DATA,data))goto done;
        if(!p[n].read)for(unsigned b=0;b<p[n].bytes;b++)if(!wr(d,DCN302_DDC_R_DATA,SET(0,DATA_BYTE,p[n].data[b])))goto done;
    }
    if(!update(d,DCN302_DDC_R_CONTROL,DCN302_DDC_GO_MASK,DCN302_DDC_GO_MASK))goto done;
    /* Require explicit DONE: an idle/zero status is not successful IO. */
    unsigned timeout_us=320*(1+total*8+count*2);
    if(timeout_us>100000)timeout_us=100000;
    for(unsigned elapsed=0;;elapsed+=10){
        uint32_t status;if(!rd(d,DCN302_DDC_R_SW_STATUS,&status))goto done;
        if(status&(DCN302_DDC_NACK_MASK|DCN302_DDC_NACK0_MASK|DCN302_DDC_NACK1_MASK|DCN302_DDC_NACK2_MASK|DCN302_DDC_NACK3_MASK)){d->error=DCN302_DDC_NACK;goto done;}
        if(GET(status,TIMED_OUT)){d->error=DCN302_DDC_TIMEOUT;goto done;}
        if(GET(status,ABORTED) || GET(status,OVERFLOW)){d->error=DCN302_DDC_ABORTED;goto done;}
        if(GET(status,DONE))break;
        if(elapsed>=timeout_us){d->error=DCN302_DDC_TIMEOUT;goto done;}
        if(!delay(d,10))goto done;
    }
    if(p[count-1].read){
        uint32_t index=SET(SET(SET(0,DATA_RW,1),INDEX_WRITE,1),INDEX,write_bytes);
        if(!wr(d,DCN302_DDC_R_DATA,index))goto done;
        for(unsigned n=0;n<p[count-1].bytes;n++){
            uint32_t data;if(!rd(d,DCN302_DDC_R_DATA,&data))goto done;
            read_data[n]=(uint8_t)GET(data,DATA_BYTE);
        }
    }
    d->error=DCN302_DDC_OK;
done:
    if(attempted && !release(d,old,acquired,changed)){d->poisoned=true;d->error=DCN302_DDC_RELEASE;}
    d->busy=false;
    if(d->error!=DCN302_DDC_OK)return false;
    if(p[count-1].read)memcpy(p[count-1].data,read_data,p[count-1].bytes);
    return true;
}
bool dcn302_ddc_read_byte(dcn302_ddc *d,uint8_t address,uint8_t offset,uint8_t *value){
    dcn302_ddc_payload p[2]={{address,false,1,&offset},{address,true,1,value}};return dcn302_ddc_transfer(d,p,2);
}
bool dcn302_ddc_write_byte(dcn302_ddc *d,uint8_t address,uint8_t offset,uint8_t value){
    uint8_t bytes[2]={offset,value};dcn302_ddc_payload p={address,false,2,bytes};return dcn302_ddc_transfer(d,&p,1);
}
