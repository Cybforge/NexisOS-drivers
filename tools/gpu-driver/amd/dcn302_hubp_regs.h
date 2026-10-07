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
/* Generated pinned AMD DCN302 HUBP fields; never infer prefixes/widths. */
#ifndef NEXIS_DCN302_HUBP_REGS_H
#define NEXIS_DCN302_HUBP_REGS_H
#include "dcn302_dml.h"
#include <stddef.h>
enum dcn302_hubp_register {
 DCN302_HUBP_R_HUBPRET_CONTROL,
 DCN302_HUBP_R_DCN_EXPANSION_MODE,
 DCN302_HUBP_R_DCHUBP_REQ_SIZE_CONFIG,
 DCN302_HUBP_R_DCHUBP_REQ_SIZE_CONFIG_C,
 DCN302_HUBP_R_BLANK_OFFSET_0,
 DCN302_HUBP_R_BLANK_OFFSET_1,
 DCN302_HUBP_R_DST_DIMENSIONS,
 DCN302_HUBP_R_DST_AFTER_SCALER,
 DCN302_HUBP_R_REF_FREQ_TO_PIX_FREQ,
 DCN302_HUBP_R_VBLANK_PARAMETERS_1,
 DCN302_HUBP_R_NOM_PARAMETERS_0,
 DCN302_HUBP_R_NOM_PARAMETERS_1,
 DCN302_HUBP_R_NOM_PARAMETERS_4,
 DCN302_HUBP_R_NOM_PARAMETERS_5,
 DCN302_HUBP_R_PER_LINE_DELIVERY,
 DCN302_HUBP_R_VBLANK_PARAMETERS_2,
 DCN302_HUBP_R_NOM_PARAMETERS_2,
 DCN302_HUBP_R_NOM_PARAMETERS_3,
 DCN302_HUBP_R_NOM_PARAMETERS_6,
 DCN302_HUBP_R_NOM_PARAMETERS_7,
 DCN302_HUBP_R_DCN_TTU_QOS_WM,
 DCN302_HUBP_R_DCN_SURF0_TTU_CNTL0,
 DCN302_HUBP_R_DCN_SURF1_TTU_CNTL0,
 DCN302_HUBP_R_DCN_CUR0_TTU_CNTL0,
 DCN302_HUBP_R_FLIP_PARAMETERS_1,
 DCN302_HUBP_R_DCN_DMDATA_VM_CNTL,
 DCN302_HUBP_R_PREFETCH_SETTINGS,
 DCN302_HUBP_R_PREFETCH_SETTINGS_C,
 DCN302_HUBP_R_VBLANK_PARAMETERS_0,
 DCN302_HUBP_R_FLIP_PARAMETERS_0,
 DCN302_HUBP_R_VBLANK_PARAMETERS_3,
 DCN302_HUBP_R_VBLANK_PARAMETERS_4,
 DCN302_HUBP_R_FLIP_PARAMETERS_2,
 DCN302_HUBP_R_PER_LINE_DELIVERY_PRE,
 DCN302_HUBP_R_DCN_SURF0_TTU_CNTL1,
 DCN302_HUBP_R_DCN_SURF1_TTU_CNTL1,
 DCN302_HUBP_R_DCN_CUR0_TTU_CNTL1,
 DCN302_HUBP_R_DCN_CUR1_TTU_CNTL1,
 DCN302_HUBP_R_DCN_GLOBAL_TTU_CNTL,
 DCN302_HUBP_R_DCHUBP_CNTL,
 DCN302_HUBP_R_HUBPREQ_DEBUG_DB,
 DCN302_HUBP_R_HUBPREQ_DEBUG,
 DCN302_HUBP_REGISTER_COUNT
};
static const uint32_t dcn302_hubp_register_bytes[5][DCN302_HUBP_REGISTER_COUNT]={
 {0x0ecb0u,0x0eba4u,0x0eac4u,0x0eac8u,0x0ec10u,0x0ec14u,0x0ec18u,0x0ec1cu,0x0ec74u,0x0ec2cu,0x0ec48u,0x0ec4cu,0x0ec58u,0x0ec5cu,0x0ec6cu,0x0ec30u,0x0ec50u,0x0ec54u,0x0ec60u,0x0ec64u,0x0eba8u,0x0ebb0u,0x0ebb8u,0x0ebc0u,0x0ec40u,0x0ebd0u,0x0ec20u,0x0ec24u,0x0ec28u,0x0ec3cu,0x0ec34u,0x0ec38u,0x0ec44u,0x0ec68u,0x0ebb4u,0x0ebbcu,0x0ebc4u,0x0ebccu,0x0ebacu,0x0eaccu,0x0ead8u,0x0eadcu},
 {0x0f020u,0x0ef14u,0x0ee34u,0x0ee38u,0x0ef80u,0x0ef84u,0x0ef88u,0x0ef8cu,0x0efe4u,0x0ef9cu,0x0efb8u,0x0efbcu,0x0efc8u,0x0efccu,0x0efdcu,0x0efa0u,0x0efc0u,0x0efc4u,0x0efd0u,0x0efd4u,0x0ef18u,0x0ef20u,0x0ef28u,0x0ef30u,0x0efb0u,0x0ef40u,0x0ef90u,0x0ef94u,0x0ef98u,0x0efacu,0x0efa4u,0x0efa8u,0x0efb4u,0x0efd8u,0x0ef24u,0x0ef2cu,0x0ef34u,0x0ef3cu,0x0ef1cu,0x0ee3cu,0x0ee48u,0x0ee4cu},
 {0x0f390u,0x0f284u,0x0f1a4u,0x0f1a8u,0x0f2f0u,0x0f2f4u,0x0f2f8u,0x0f2fcu,0x0f354u,0x0f30cu,0x0f328u,0x0f32cu,0x0f338u,0x0f33cu,0x0f34cu,0x0f310u,0x0f330u,0x0f334u,0x0f340u,0x0f344u,0x0f288u,0x0f290u,0x0f298u,0x0f2a0u,0x0f320u,0x0f2b0u,0x0f300u,0x0f304u,0x0f308u,0x0f31cu,0x0f314u,0x0f318u,0x0f324u,0x0f348u,0x0f294u,0x0f29cu,0x0f2a4u,0x0f2acu,0x0f28cu,0x0f1acu,0x0f1b8u,0x0f1bcu},
 {0x0f700u,0x0f5f4u,0x0f514u,0x0f518u,0x0f660u,0x0f664u,0x0f668u,0x0f66cu,0x0f6c4u,0x0f67cu,0x0f698u,0x0f69cu,0x0f6a8u,0x0f6acu,0x0f6bcu,0x0f680u,0x0f6a0u,0x0f6a4u,0x0f6b0u,0x0f6b4u,0x0f5f8u,0x0f600u,0x0f608u,0x0f610u,0x0f690u,0x0f620u,0x0f670u,0x0f674u,0x0f678u,0x0f68cu,0x0f684u,0x0f688u,0x0f694u,0x0f6b8u,0x0f604u,0x0f60cu,0x0f614u,0x0f61cu,0x0f5fcu,0x0f51cu,0x0f528u,0x0f52cu},
 {0x0fa70u,0x0f964u,0x0f884u,0x0f888u,0x0f9d0u,0x0f9d4u,0x0f9d8u,0x0f9dcu,0x0fa34u,0x0f9ecu,0x0fa08u,0x0fa0cu,0x0fa18u,0x0fa1cu,0x0fa2cu,0x0f9f0u,0x0fa10u,0x0fa14u,0x0fa20u,0x0fa24u,0x0f968u,0x0f970u,0x0f978u,0x0f980u,0x0fa00u,0x0f990u,0x0f9e0u,0x0f9e4u,0x0f9e8u,0x0f9fcu,0x0f9f4u,0x0f9f8u,0x0fa04u,0x0fa28u,0x0f974u,0x0f97cu,0x0f984u,0x0f98cu,0x0f96cu,0x0f88cu,0x0f898u,0x0f89cu},
};
static const uint32_t dcn302_hubp_owned[DCN302_HUBP_REGISTER_COUNT]={0x00000fffu,0x000000ffu,0x077f1f77u,0x007f1f77u,0x7fff1fffu,0x0003ffffu,0x001fffffu,0x00071fffu,0x001fffffu,0x007fffffu,0x0001ffffu,0x007fffffu,0x0001ffffu,0x007fffffu,0x1fff1fffu,0x007fffffu,0x0001ffffu,0x007fffffu,0x0001ffffu,0x007fffffu,0x3fff3fffu,0x1f7fffffu,0x1f7fffffu,0x1f7fffffu,0x007fffffu,0x0000ffffu,0xff3fffffu,0x003fffffu,0x00003f7fu,0x00003f7fu,0x007fffffu,0x007fffffu,0x007fffffu,0x1fff1fffu,0x007fffffu,0x007fffffu,0x007fffffu,0x007fffffu,0xf0ffffffu,0x00000100u,0x00000100u,0x04000000u};
static const uint32_t dcn302_hubp_forbidden[DCN302_HUBP_REGISTER_COUNT]={0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x04100000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x84000000u,0x00000000u,0x00000000u};
static const uint32_t dcn302_hubp_readonly[DCN302_HUBP_REGISTER_COUNT]={0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x830f0000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x70ff000au,0x00000000u,0x00000000u};
typedef struct {uint32_t mask;uint16_t offset;uint8_t reg,shift;} dcn302_hubp_field;
static const dcn302_hubp_field dcn302_hubp_fields[]={
 {0x00000fffu,offsetof(dcn302_dml_output,rq.plane1_base_address),0,0}, /* HUBPRET_CONTROL.DET_BUF_PLANE1_BASE_ADDRESS */
 {0x00000003u,offsetof(dcn302_dml_output,rq.drq_expansion_mode),1,0}, /* DCN_EXPANSION_MODE.DRQ_EXPANSION_MODE */
 {0x000000c0u,offsetof(dcn302_dml_output,rq.prq_expansion_mode),1,6}, /* DCN_EXPANSION_MODE.PRQ_EXPANSION_MODE */
 {0x00000030u,offsetof(dcn302_dml_output,rq.mrq_expansion_mode),1,4}, /* DCN_EXPANSION_MODE.MRQ_EXPANSION_MODE */
 {0x0000000cu,offsetof(dcn302_dml_output,rq.crq_expansion_mode),1,2}, /* DCN_EXPANSION_MODE.CRQ_EXPANSION_MODE */
 {0x00000700u,offsetof(dcn302_dml_output,rq.rq_regs_l.chunk_size),2,8}, /* DCHUBP_REQ_SIZE_CONFIG.CHUNK_SIZE */
 {0x00001800u,offsetof(dcn302_dml_output,rq.rq_regs_l.min_chunk_size),2,11}, /* DCHUBP_REQ_SIZE_CONFIG.MIN_CHUNK_SIZE */
 {0x00030000u,offsetof(dcn302_dml_output,rq.rq_regs_l.meta_chunk_size),2,16}, /* DCHUBP_REQ_SIZE_CONFIG.META_CHUNK_SIZE */
 {0x000c0000u,offsetof(dcn302_dml_output,rq.rq_regs_l.min_meta_chunk_size),2,18}, /* DCHUBP_REQ_SIZE_CONFIG.MIN_META_CHUNK_SIZE */
 {0x00700000u,offsetof(dcn302_dml_output,rq.rq_regs_l.dpte_group_size),2,20}, /* DCHUBP_REQ_SIZE_CONFIG.DPTE_GROUP_SIZE */
 {0x07000000u,offsetof(dcn302_dml_output,rq.rq_regs_l.mpte_group_size),2,24}, /* DCHUBP_REQ_SIZE_CONFIG.VM_GROUP_SIZE */
 {0x00000007u,offsetof(dcn302_dml_output,rq.rq_regs_l.swath_height),2,0}, /* DCHUBP_REQ_SIZE_CONFIG.SWATH_HEIGHT */
 {0x00000070u,offsetof(dcn302_dml_output,rq.rq_regs_l.pte_row_height_linear),2,4}, /* DCHUBP_REQ_SIZE_CONFIG.PTE_ROW_HEIGHT_LINEAR */
 {0x00000700u,offsetof(dcn302_dml_output,rq.rq_regs_c.chunk_size),3,8}, /* DCHUBP_REQ_SIZE_CONFIG_C.CHUNK_SIZE_C */
 {0x00001800u,offsetof(dcn302_dml_output,rq.rq_regs_c.min_chunk_size),3,11}, /* DCHUBP_REQ_SIZE_CONFIG_C.MIN_CHUNK_SIZE_C */
 {0x00030000u,offsetof(dcn302_dml_output,rq.rq_regs_c.meta_chunk_size),3,16}, /* DCHUBP_REQ_SIZE_CONFIG_C.META_CHUNK_SIZE_C */
 {0x000c0000u,offsetof(dcn302_dml_output,rq.rq_regs_c.min_meta_chunk_size),3,18}, /* DCHUBP_REQ_SIZE_CONFIG_C.MIN_META_CHUNK_SIZE_C */
 {0x00700000u,offsetof(dcn302_dml_output,rq.rq_regs_c.dpte_group_size),3,20}, /* DCHUBP_REQ_SIZE_CONFIG_C.DPTE_GROUP_SIZE_C */
 {0x00000007u,offsetof(dcn302_dml_output,rq.rq_regs_c.swath_height),3,0}, /* DCHUBP_REQ_SIZE_CONFIG_C.SWATH_HEIGHT_C */
 {0x00000070u,offsetof(dcn302_dml_output,rq.rq_regs_c.pte_row_height_linear),3,4}, /* DCHUBP_REQ_SIZE_CONFIG_C.PTE_ROW_HEIGHT_LINEAR_C */
 {0x00001fffu,offsetof(dcn302_dml_output,dlg.refcyc_h_blank_end),4,0}, /* BLANK_OFFSET_0.REFCYC_H_BLANK_END */
 {0x7fff0000u,offsetof(dcn302_dml_output,dlg.dlg_vblank_end),4,16}, /* BLANK_OFFSET_0.DLG_V_BLANK_END */
 {0x0003ffffu,offsetof(dcn302_dml_output,dlg.min_dst_y_next_start),5,0}, /* BLANK_OFFSET_1.MIN_DST_Y_NEXT_START */
 {0x001fffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_htotal),6,0}, /* DST_DIMENSIONS.REFCYC_PER_HTOTAL */
 {0x00001fffu,offsetof(dcn302_dml_output,dlg.refcyc_x_after_scaler),7,0}, /* DST_AFTER_SCALER.REFCYC_X_AFTER_SCALER */
 {0x00070000u,offsetof(dcn302_dml_output,dlg.dst_y_after_scaler),7,16}, /* DST_AFTER_SCALER.DST_Y_AFTER_SCALER */
 {0x001fffffu,offsetof(dcn302_dml_output,dlg.ref_freq_to_pix_freq),8,0}, /* REF_FREQ_TO_PIX_FREQ.REF_FREQ_TO_PIX_FREQ */
 {0x007fffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_pte_group_vblank_l),9,0}, /* VBLANK_PARAMETERS_1.REFCYC_PER_PTE_GROUP_VBLANK_L */
 {0x0001ffffu,offsetof(dcn302_dml_output,dlg.dst_y_per_pte_row_nom_l),10,0}, /* NOM_PARAMETERS_0.DST_Y_PER_PTE_ROW_NOM_L */
 {0x007fffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_pte_group_nom_l),11,0}, /* NOM_PARAMETERS_1.REFCYC_PER_PTE_GROUP_NOM_L */
 {0x0001ffffu,offsetof(dcn302_dml_output,dlg.dst_y_per_meta_row_nom_l),12,0}, /* NOM_PARAMETERS_4.DST_Y_PER_META_ROW_NOM_L */
 {0x007fffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_meta_chunk_nom_l),13,0}, /* NOM_PARAMETERS_5.REFCYC_PER_META_CHUNK_NOM_L */
 {0x00001fffu,offsetof(dcn302_dml_output,dlg.refcyc_per_line_delivery_l),14,0}, /* PER_LINE_DELIVERY.REFCYC_PER_LINE_DELIVERY_L */
 {0x1fff0000u,offsetof(dcn302_dml_output,dlg.refcyc_per_line_delivery_c),14,16}, /* PER_LINE_DELIVERY.REFCYC_PER_LINE_DELIVERY_C */
 {0x007fffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_pte_group_vblank_c),15,0}, /* VBLANK_PARAMETERS_2.REFCYC_PER_PTE_GROUP_VBLANK_C */
 {0x0001ffffu,offsetof(dcn302_dml_output,dlg.dst_y_per_pte_row_nom_c),16,0}, /* NOM_PARAMETERS_2.DST_Y_PER_PTE_ROW_NOM_C */
 {0x007fffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_pte_group_nom_c),17,0}, /* NOM_PARAMETERS_3.REFCYC_PER_PTE_GROUP_NOM_C */
 {0x0001ffffu,offsetof(dcn302_dml_output,dlg.dst_y_per_meta_row_nom_c),18,0}, /* NOM_PARAMETERS_6.DST_Y_PER_META_ROW_NOM_C */
 {0x007fffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_meta_chunk_nom_c),19,0}, /* NOM_PARAMETERS_7.REFCYC_PER_META_CHUNK_NOM_C */
 {0x00003fffu,offsetof(dcn302_dml_output,ttu.qos_level_low_wm),20,0}, /* DCN_TTU_QOS_WM.QoS_LEVEL_LOW_WM */
 {0x3fff0000u,offsetof(dcn302_dml_output,ttu.qos_level_high_wm),20,16}, /* DCN_TTU_QOS_WM.QoS_LEVEL_HIGH_WM */
 {0x007fffffu,offsetof(dcn302_dml_output,ttu.refcyc_per_req_delivery_l),21,0}, /* DCN_SURF0_TTU_CNTL0.REFCYC_PER_REQ_DELIVERY */
 {0x0f000000u,offsetof(dcn302_dml_output,ttu.qos_level_fixed_l),21,24}, /* DCN_SURF0_TTU_CNTL0.QoS_LEVEL_FIXED */
 {0x10000000u,offsetof(dcn302_dml_output,ttu.qos_ramp_disable_l),21,28}, /* DCN_SURF0_TTU_CNTL0.QoS_RAMP_DISABLE */
 {0x007fffffu,offsetof(dcn302_dml_output,ttu.refcyc_per_req_delivery_c),22,0}, /* DCN_SURF1_TTU_CNTL0.REFCYC_PER_REQ_DELIVERY */
 {0x0f000000u,offsetof(dcn302_dml_output,ttu.qos_level_fixed_c),22,24}, /* DCN_SURF1_TTU_CNTL0.QoS_LEVEL_FIXED */
 {0x10000000u,offsetof(dcn302_dml_output,ttu.qos_ramp_disable_c),22,28}, /* DCN_SURF1_TTU_CNTL0.QoS_RAMP_DISABLE */
 {0x007fffffu,offsetof(dcn302_dml_output,ttu.refcyc_per_req_delivery_cur0),23,0}, /* DCN_CUR0_TTU_CNTL0.REFCYC_PER_REQ_DELIVERY */
 {0x0f000000u,offsetof(dcn302_dml_output,ttu.qos_level_fixed_cur0),23,24}, /* DCN_CUR0_TTU_CNTL0.QoS_LEVEL_FIXED */
 {0x10000000u,offsetof(dcn302_dml_output,ttu.qos_ramp_disable_cur0),23,28}, /* DCN_CUR0_TTU_CNTL0.QoS_RAMP_DISABLE */
 {0x007fffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_pte_group_flip_l),24,0}, /* FLIP_PARAMETERS_1.REFCYC_PER_PTE_GROUP_FLIP_L */
 {0x0000ffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_vm_dmdata),25,0}, /* DCN_DMDATA_VM_CNTL.REFCYC_PER_VM_DMDATA */
 {0xff000000u,offsetof(dcn302_dml_output,dlg.dst_y_prefetch),26,24}, /* PREFETCH_SETTINGS.DST_Y_PREFETCH */
 {0x003fffffu,offsetof(dcn302_dml_output,dlg.vratio_prefetch),26,0}, /* PREFETCH_SETTINGS.VRATIO_PREFETCH */
 {0x003fffffu,offsetof(dcn302_dml_output,dlg.vratio_prefetch_c),27,0}, /* PREFETCH_SETTINGS_C.VRATIO_PREFETCH_C */
 {0x0000007fu,offsetof(dcn302_dml_output,dlg.dst_y_per_vm_vblank),28,0}, /* VBLANK_PARAMETERS_0.DST_Y_PER_VM_VBLANK */
 {0x00003f00u,offsetof(dcn302_dml_output,dlg.dst_y_per_row_vblank),28,8}, /* VBLANK_PARAMETERS_0.DST_Y_PER_ROW_VBLANK */
 {0x0000007fu,offsetof(dcn302_dml_output,dlg.dst_y_per_vm_flip),29,0}, /* FLIP_PARAMETERS_0.DST_Y_PER_VM_FLIP */
 {0x00003f00u,offsetof(dcn302_dml_output,dlg.dst_y_per_row_flip),29,8}, /* FLIP_PARAMETERS_0.DST_Y_PER_ROW_FLIP */
 {0x007fffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_meta_chunk_vblank_l),30,0}, /* VBLANK_PARAMETERS_3.REFCYC_PER_META_CHUNK_VBLANK_L */
 {0x007fffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_meta_chunk_vblank_c),31,0}, /* VBLANK_PARAMETERS_4.REFCYC_PER_META_CHUNK_VBLANK_C */
 {0x007fffffu,offsetof(dcn302_dml_output,dlg.refcyc_per_meta_chunk_flip_l),32,0}, /* FLIP_PARAMETERS_2.REFCYC_PER_META_CHUNK_FLIP_L */
 {0x00001fffu,offsetof(dcn302_dml_output,dlg.refcyc_per_line_delivery_pre_l),33,0}, /* PER_LINE_DELIVERY_PRE.REFCYC_PER_LINE_DELIVERY_PRE_L */
 {0x1fff0000u,offsetof(dcn302_dml_output,dlg.refcyc_per_line_delivery_pre_c),33,16}, /* PER_LINE_DELIVERY_PRE.REFCYC_PER_LINE_DELIVERY_PRE_C */
 {0x007fffffu,offsetof(dcn302_dml_output,ttu.refcyc_per_req_delivery_pre_l),34,0}, /* DCN_SURF0_TTU_CNTL1.REFCYC_PER_REQ_DELIVERY_PRE */
 {0x007fffffu,offsetof(dcn302_dml_output,ttu.refcyc_per_req_delivery_pre_c),35,0}, /* DCN_SURF1_TTU_CNTL1.REFCYC_PER_REQ_DELIVERY_PRE */
 {0x007fffffu,offsetof(dcn302_dml_output,ttu.refcyc_per_req_delivery_pre_cur0),36,0}, /* DCN_CUR0_TTU_CNTL1.REFCYC_PER_REQ_DELIVERY_PRE */
 {0x007fffffu,offsetof(dcn302_dml_output,ttu.refcyc_per_req_delivery_pre_cur1),37,0}, /* DCN_CUR1_TTU_CNTL1.REFCYC_PER_REQ_DELIVERY_PRE */
 {0x00ffffffu,offsetof(dcn302_dml_output,ttu.min_ttu_vblank),38,0}, /* DCN_GLOBAL_TTU_CNTL.MIN_TTU_VBLANK */
 {0xf0000000u,offsetof(dcn302_dml_output,ttu.qos_level_flip),38,28}, /* DCN_GLOBAL_TTU_CNTL.QoS_LEVEL_FLIP */
};
#define DCN302_HUBP_FIELD_COUNT (sizeof(dcn302_hubp_fields)/sizeof(dcn302_hubp_fields[0]))
#define DCN302_HUBP_HUBP_BLANK_EN_MASK 0x00000001u
#define DCN302_HUBP_HUBP_BLANK_EN_SHIFT 0u
#define DCN302_HUBP_HUBP_TTU_DISABLE_MASK 0x00001000u
#define DCN302_HUBP_HUBP_TTU_DISABLE_SHIFT 12u
#define DCN302_HUBP_HUBP_NO_OUTSTANDING_REQ_MASK 0x00000002u
#define DCN302_HUBP_HUBP_NO_OUTSTANDING_REQ_SHIFT 1u
#define DCN302_HUBP_HUBP_VREADY_AT_OR_AFTER_VSYNC_MASK 0x00000100u
#define DCN302_HUBP_HUBP_VREADY_AT_OR_AFTER_VSYNC_SHIFT 8u
#define DCN302_HUBP_HUBP_UNDERFLOW_STATUS_MASK 0x70000000u
#define DCN302_HUBP_HUBP_UNDERFLOW_STATUS_SHIFT 28u
#define DCN302_HUBP_HUBP_TIMEOUT_STATUS_MASK 0x00f00000u
#define DCN302_HUBP_HUBP_TIMEOUT_STATUS_SHIFT 20u
#define DCN302_HUBP_HUBP_DISABLE_MASK 0x00000004u
#define DCN302_HUBP_HUBP_DISABLE_SHIFT 2u
#define DCN302_HUBP_HUBP_CLOCK_ENABLE_MASK 0x00000001u
#define DCN302_HUBP_HUBP_CLOCK_ENABLE_SHIFT 0u
#define DCN302_HUBP_HUBP_DISPCLK_R_CLOCK_ON_MASK 0x00100000u
#define DCN302_HUBP_HUBP_DISPCLK_R_CLOCK_ON_SHIFT 20u
#define DCN302_HUBP_HUBP_DPPCLK_G_CLOCK_ON_MASK 0x00200000u
#define DCN302_HUBP_HUBP_DPPCLK_G_CLOCK_ON_SHIFT 21u
#define DCN302_HUBP_HUBP_DCFCLK_R_CLOCK_ON_MASK 0x00400000u
#define DCN302_HUBP_HUBP_DCFCLK_R_CLOCK_ON_SHIFT 22u
#define DCN302_HUBP_HUBP_DCFCLK_G_CLOCK_ON_MASK 0x00800000u
#define DCN302_HUBP_HUBP_DCFCLK_G_CLOCK_ON_SHIFT 23u
static const uint32_t dcn302_hubp_clock_bytes[5]={0x0ead0u,0x0ee40u,0x0f1b0u,0x0f520u,0x0f890u};
#endif
