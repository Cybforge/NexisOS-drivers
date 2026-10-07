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
/* Generated checksum-pinned AMD v6.12 DCN302 native routing. */
#ifndef NEXIS_DCN302_ROUTE_REGS_H
#define NEXIS_DCN302_ROUTE_REGS_H
#include <stdint.h>
#define DCN302_GPIO_HPD_BYTES 0x176d4u
static const uint32_t dcn302_hpd_mask[5]={0x00000001u,0x00000100u,0x00010000u,0x01000000u,0x04000000u};
static const uint32_t dcn302_hpd_status_bytes[5]={0x14f50u,0x14f70u,0x14f90u,0x14fb0u,0x14fd0u};
#define DCN302_HPD_SENSE_MASK 0x00000002u
#define DCN302_HPD_SENSE_SHIFT 1u
#define DCN302_HPD_DELAYED_MASK 0x00000010u
#define DCN302_HPD_DELAYED_SHIFT 4u
static const uint32_t dcn302_ddc_gpio_bytes[5]={0x17644u,0x17654u,0x17664u,0x17674u,0x17684u};
static const uint32_t dcn302_ddc_clk_mask[5]={0x00000001u,0x00000001u,0x00000001u,0x00000001u,0x00000001u};
#endif
