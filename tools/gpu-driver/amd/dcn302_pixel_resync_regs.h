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
/* Generated native DCN302 PHYPLL pixel-resync definitions. */
#ifndef NEXIS_DCN302_PIXEL_RESYNC_REGS_H
#define NEXIS_DCN302_PIXEL_RESYNC_REGS_H
#include <stdint.h>
static const uint32_t dcn302_pixel_resync_bytes[5]={0x00400u,0x00404u,0x00408u,0x0040cu,0x00430u};
#define DCN302_PIXEL_RESYNC_RESYNC_ENABLE_MASK 0x00000001u
#define DCN302_PIXEL_RESYNC_DEEP_COLOR_MASK 0x00000030u
#define DCN302_PIXEL_RESYNC_PIXCLK_ENABLE_MASK 0x00000100u
#define DCN302_PIXEL_RESYNC_DOUBLE_RATE_MASK 0x00000200u
#define DCN302_PIXEL_RESYNC_OWNED_MASK DCN302_PIXEL_RESYNC_DEEP_COLOR_MASK
#endif
