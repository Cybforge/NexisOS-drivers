/*
 * Copyright (C) 2020  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/* Generated pinned AMD DCN302 HUBBUB fields; exact native widths. */
#ifndef NEXIS_DCN302_HUBBUB_REGS_H
#define NEXIS_DCN302_HUBBUB_REGS_H
#include <stdint.h>
enum dcn302_hubbub_register {
 DCN302_HUBBUB_R_DCHUBBUB_ARB_DRAM_STATE_CNTL,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_FRAC_URG_BW_NOM_A,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_FRAC_URG_BW_NOM_B,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_FRAC_URG_BW_FLIP_C,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_FRAC_URG_BW_NOM_C,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_C,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_FRAC_URG_BW_FLIP_D,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_FRAC_URG_BW_NOM_D,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_D,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_SAT_LEVEL,
 DCN302_HUBBUB_R_DCHUBBUB_ARB_DF_REQ_OUTSTAND,
 DCN302_HUBBUB_REGISTER_COUNT
};
static const uint32_t dcn302_hubbub_register_bytes[]={0x0e720u,0x0e724u,0x0e800u,0x0e7fcu,0x0e728u,0x0e72cu,0x0e730u,0x0e734u,0x0e738u,0x0e808u,0x0e804u,0x0e73cu,0x0e740u,0x0e744u,0x0e748u,0x0e74cu,0x0e810u,0x0e80cu,0x0e750u,0x0e754u,0x0e758u,0x0e75cu,0x0e760u,0x0e818u,0x0e814u,0x0e764u,0x0e768u,0x0e76cu,0x0e770u,0x0e718u,0x0e714u};
static const uint32_t dcn302_hubbub_owned[]={0x00000033u,0x3fff3fffu,0x000003ffu,0x000003ffu,0x00003fffu,0xffffffffu,0xffffffffu,0xffffffffu,0x3fff3fffu,0x000003ffu,0x000003ffu,0x00003fffu,0xffffffffu,0xffffffffu,0xffffffffu,0x3fff3fffu,0x000003ffu,0x000003ffu,0x00003fffu,0xffffffffu,0xffffffffu,0xffffffffu,0x3fff3fffu,0x000003ffu,0x000003ffu,0x00003fffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0x001ff000u};
#define DCN302_HUBBUB_REF_ENABLE_MASK 0x00000001u
#define DCN302_HUBBUB_REF_ENABLE_SHIFT 0u
#define DCN302_HUBBUB_REF_SELECT_MASK 0x00000002u
#define DCN302_HUBBUB_REF_SELECT_SHIFT 1u
#define DCN302_HUBBUB_TIMER_DIV_MASK 0x0000000fu
#define DCN302_HUBBUB_TIMER_DIV_SHIFT 0u
#define DCN302_HUBBUB_TIMER_ENABLE_MASK 0x00001000u
#define DCN302_HUBBUB_TIMER_ENABLE_SHIFT 12u
#define DCN302_HUBBUB_REF_BYTES 0x00424u
#define DCN302_HUBBUB_TIMER_BYTES 0x0e77cu
#define DCN302_HUBBUB_FIELDS(X) \
 X(1,0x00003fffu,0,urgent_ns,1) \
 X(1,0x3fff0000u,16,urgent_ns,1) \
 X(2,0x000003ffu,0,frac_urg_flip,0) \
 X(3,0x000003ffu,0,frac_urg_nom,0) \
 X(4,0x00003fffu,0,memory_trip_ns,1) \
 X(5,0x0000ffffu,0,stutter_enter_exit_ns,1) \
 X(5,0xffff0000u,16,stutter_enter_exit_ns,1) \
 X(6,0x0000ffffu,0,stutter_exit_ns,1) \
 X(6,0xffff0000u,16,stutter_exit_ns,1) \
 X(7,0x0000ffffu,0,dram_change_ns,1) \
 X(7,0xffff0000u,16,dram_change_ns,1) \
 X(8,0x00003fffu,0,urgent_ns,1) \
 X(8,0x3fff0000u,16,urgent_ns,1) \
 X(9,0x000003ffu,0,frac_urg_flip,0) \
 X(10,0x000003ffu,0,frac_urg_nom,0) \
 X(11,0x00003fffu,0,memory_trip_ns,1) \
 X(12,0x0000ffffu,0,stutter_enter_exit_ns,1) \
 X(12,0xffff0000u,16,stutter_enter_exit_ns,1) \
 X(13,0x0000ffffu,0,stutter_exit_ns,1) \
 X(13,0xffff0000u,16,stutter_exit_ns,1) \
 X(14,0x0000ffffu,0,dram_change_ns,1) \
 X(14,0xffff0000u,16,dram_change_ns,1) \
 X(15,0x00003fffu,0,urgent_ns,1) \
 X(15,0x3fff0000u,16,urgent_ns,1) \
 X(16,0x000003ffu,0,frac_urg_flip,0) \
 X(17,0x000003ffu,0,frac_urg_nom,0) \
 X(18,0x00003fffu,0,memory_trip_ns,1) \
 X(19,0x0000ffffu,0,stutter_enter_exit_ns,1) \
 X(19,0xffff0000u,16,stutter_enter_exit_ns,1) \
 X(20,0x0000ffffu,0,stutter_exit_ns,1) \
 X(20,0xffff0000u,16,stutter_exit_ns,1) \
 X(21,0x0000ffffu,0,dram_change_ns,1) \
 X(21,0xffff0000u,16,dram_change_ns,1) \
 X(22,0x00003fffu,0,urgent_ns,1) \
 X(22,0x3fff0000u,16,urgent_ns,1) \
 X(23,0x000003ffu,0,frac_urg_flip,0) \
 X(24,0x000003ffu,0,frac_urg_nom,0) \
 X(25,0x00003fffu,0,memory_trip_ns,1) \
 X(26,0x0000ffffu,0,stutter_enter_exit_ns,1) \
 X(26,0xffff0000u,16,stutter_enter_exit_ns,1) \
 X(27,0x0000ffffu,0,stutter_exit_ns,1) \
 X(27,0xffff0000u,16,stutter_exit_ns,1) \
 X(28,0x0000ffffu,0,dram_change_ns,1) \
 X(28,0xffff0000u,16,dram_change_ns,1) \
 X(29,0xffffffffu,0,sat_cycles,0) \
 X(30,0x001ff000u,12,min_outstanding,0) \
 X(0,0x00000001u,0,sr_value,0) \
 X(0,0x00000002u,1,sr_force,0) \
 X(0,0x00000010u,4,pstate_value,0) \
 X(0,0x00000020u,5,pstate_force,0)
#endif
