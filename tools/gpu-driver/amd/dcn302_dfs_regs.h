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
/* Generated Linux v6.12 Navi23 display DFS/DTO registers. */
#ifndef NEXIS_DCN302_DFS_REGS_H
#define NEXIS_DCN302_DFS_REGS_H
#include <stdint.h>
#define DCN302_DFS_PLL_BYTES 0x5c040u
#define DCN302_DFS_DENTIST_BYTES 0x00490u
#define DCN302_DFS_DTO_CTRL_BYTES 0x005d8u
static const uint32_t dcn302_dfs_dto_bytes[5]={0x00564u,0x00568u,0x0056cu,0x00570u,0x00574u};
#define DCN302_DFS_DISP_WRITE_MASK 0x0000007fu
#define DCN302_DFS_DISP_WRITE_SHIFT 0u
#define DCN302_DFS_DISP_READ_MASK 0x00007f00u
#define DCN302_DFS_DISP_READ_SHIFT 8u
#define DCN302_DFS_MODE_MASK 0x00018000u
#define DCN302_DFS_MODE_SHIFT 15u
#define DCN302_DFS_DISP_DONE_MASK 0x00080000u
#define DCN302_DFS_DISP_DONE_SHIFT 19u
#define DCN302_DFS_DPP_DONE_MASK 0x00100000u
#define DCN302_DFS_DPP_DONE_SHIFT 20u
#define DCN302_DFS_DPP_WRITE_MASK 0x7f000000u
#define DCN302_DFS_DPP_WRITE_SHIFT 24u
#define DCN302_DFS_DISP_CHANGE_TOGGLE_MASK 0x00020000u
#define DCN302_DFS_DISP_CHANGE_TOGGLE_SHIFT 17u
#define DCN302_DFS_DISP_DONE_TOGGLE_MASK 0x00040000u
#define DCN302_DFS_DISP_DONE_TOGGLE_SHIFT 18u
#define DCN302_DFS_DPP_CHANGE_TOGGLE_MASK 0x00200000u
#define DCN302_DFS_DPP_CHANGE_TOGGLE_SHIFT 21u
#define DCN302_DFS_DPP_DONE_TOGGLE_MASK 0x00400000u
#define DCN302_DFS_DPP_DONE_TOGGLE_SHIFT 22u
#define DCN302_DFS_PHASE_MASK 0x000000ffu
#define DCN302_DFS_PHASE_SHIFT 0u
#define DCN302_DFS_MODULO_MASK 0x00ff0000u
#define DCN302_DFS_MODULO_SHIFT 16u
#define DCN302_DFS_INTEGER_MASK 0x000001ffu
#define DCN302_DFS_FRACTION_MASK 0xffff0000u
static const uint32_t dcn302_dfs_dto_enable[5]={0x00000001u,0x00000010u,0x00000100u,0x00001000u,0x00010000u};
static const uint32_t dcn302_dfs_dto_db[5]={0x00000002u,0x00000020u,0x00000200u,0x00002000u,0x00020000u};
#endif
