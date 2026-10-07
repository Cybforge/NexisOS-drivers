/*
 * NexisOS SCDC transport transaction. Definitions follow Linux v6.12
 * include/drm/display/drm_scdc.h.
 * Copyright (c) 2015 NVIDIA Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "hdmi_scdc.h"
#include <string.h>
#define SCDC_ADDRESS 0x54
#define SCDC_SINK_VERSION 0x01
#define SCDC_SOURCE_VERSION 0x02
#define SCDC_TMDS_CONFIG 0x20
#define SCDC_SCRAMBLER_STATUS 0x21
#define SCDC_STATUS_FLAGS 0x40
static bool valid(const hdmi_scdc_io *io){return io && io->read_byte && io->write_byte && io->delay_us;}
static bool clock_valid(uint32_t khz){return khz>=25000 && khz<=600000;}
static uint8_t config(uint32_t clock,bool low){return (uint8_t)((clock>340000?3u:low?1u:0u));}
enum hdmi_scdc_error hdmi_scdc_restore(const hdmi_scdc_io *io,const hdmi_scdc_snapshot *old){
    if(!valid(io) || !old || !old->valid)return HDMI_SCDC_INPUT;
    bool ok=io->write_byte(io->context,SCDC_ADDRESS,SCDC_TMDS_CONFIG,old->tmds_config);
    if(!io->write_byte(io->context,SCDC_ADDRESS,SCDC_SOURCE_VERSION,old->source_version))ok=false;
    uint8_t c=0,v=0;
    if(!io->read_byte(io->context,SCDC_ADDRESS,SCDC_TMDS_CONFIG,&c) || c!=old->tmds_config)ok=false;
    if(!io->read_byte(io->context,SCDC_ADDRESS,SCDC_SOURCE_VERSION,&v) || v!=old->source_version)ok=false;
    return ok?HDMI_SCDC_OK:HDMI_SCDC_ROLLBACK;
}
enum hdmi_scdc_error hdmi_scdc_configure(const hdmi_scdc_io *io,uint32_t clock,bool low,hdmi_scdc_snapshot *old){
    if(!old)return HDMI_SCDC_INPUT;
    memset(old,0,sizeof(*old));
    if(!valid(io) || !clock_valid(clock))return HDMI_SCDC_INPUT;
    uint8_t version;
    if(!io->read_byte(io->context,SCDC_ADDRESS,SCDC_SINK_VERSION,&version))return HDMI_SCDC_IO;
    if(!version)return HDMI_SCDC_VERSION;
    if(!io->read_byte(io->context,SCDC_ADDRESS,SCDC_SOURCE_VERSION,&old->source_version) ||
       !io->read_byte(io->context,SCDC_ADDRESS,SCDC_TMDS_CONFIG,&old->tmds_config))return HDMI_SCDC_IO;
    old->valid=true;
    uint8_t target=(uint8_t)((old->tmds_config&~3u)|config(clock,low));
    enum hdmi_scdc_error error=HDMI_SCDC_IO;
    if(io->write_byte(io->context,SCDC_ADDRESS,SCDC_SOURCE_VERSION,1) &&
       io->write_byte(io->context,SCDC_ADDRESS,SCDC_TMDS_CONFIG,target)){
        uint8_t value,source;
        if(io->read_byte(io->context,SCDC_ADDRESS,SCDC_TMDS_CONFIG,&value) &&
           io->read_byte(io->context,SCDC_ADDRESS,SCDC_SOURCE_VERSION,&source))error=(value==target && source==1)?HDMI_SCDC_OK:HDMI_SCDC_READBACK;
    }
    if(error!=HDMI_SCDC_OK && hdmi_scdc_restore(io,old)!=HDMI_SCDC_OK)return HDMI_SCDC_ROLLBACK;
    return error;
}
enum hdmi_scdc_error hdmi_scdc_verify_link(const hdmi_scdc_io *io,uint32_t clock,bool low){
    if(!valid(io) || !clock_valid(clock))return HDMI_SCDC_INPUT;
    uint8_t expected=config(clock,low);
    for(unsigned n=0;n<101;n++){
        uint8_t c,s,locks;
        if(!io->read_byte(io->context,SCDC_ADDRESS,SCDC_TMDS_CONFIG,&c) ||
           !io->read_byte(io->context,SCDC_ADDRESS,SCDC_SCRAMBLER_STATUS,&s) ||
           !io->read_byte(io->context,SCDC_ADDRESS,SCDC_STATUS_FLAGS,&locks))return HDMI_SCDC_IO;
        if((c&3)!=expected)return HDMI_SCDC_READBACK;
        if((s&1)==(expected&1) && (locks&15)==15)return HDMI_SCDC_OK;
        if(n<100 && !io->delay_us(io->context,1000))return HDMI_SCDC_IO;
    }
    return HDMI_SCDC_TIMEOUT;
}
