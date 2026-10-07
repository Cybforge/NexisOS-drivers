/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */
/* ASIC model only. Every board/clock value is supplied separately. */
static const ip_params_st dcn302_ip_template = {
		.use_min_dcfclk = 0,
		.clamp_min_dcfclk = 0,
		.odm_capable = 1,
		.gpuvm_enable = 1,
		.hostvm_enable = 0,
		.gpuvm_max_page_table_levels = 4,
		.hostvm_max_page_table_levels = 4,
		.hostvm_cached_page_table_levels = 0,
		.pte_group_size_bytes = 2048,
		.num_dsc = 5,
		.rob_buffer_size_kbytes = 184,
		.det_buffer_size_kbytes = 184,
		.dpte_buffer_size_in_pte_reqs_luma = 64,
		.dpte_buffer_size_in_pte_reqs_chroma = 34,
		.pde_proc_buffer_size_64k_reqs = 48,
		.dpp_output_buffer_pixels = 2560,
		.opp_output_buffer_lines = 1,
		.pixel_chunk_size_kbytes = 8,
		.pte_enable = 1,
		.max_page_table_levels = 2,
		.pte_chunk_size_kbytes = 2,  // ?
		.meta_chunk_size_kbytes = 2,
		.writeback_chunk_size_kbytes = 8,
		.line_buffer_size_bits = 789504,
		.is_line_buffer_bpp_fixed = 0,  // ?
		.line_buffer_fixed_bpp = 0,     // ?
		.dcc_supported = true,
		.writeback_interface_buffer_size_kbytes = 90,
		.writeback_line_buffer_buffer_size = 0,
		.max_line_buffer_lines = 12,
		.writeback_luma_buffer_size_kbytes = 12,  // writeback_line_buffer_buffer_size = 656640
		.writeback_chroma_buffer_size_kbytes = 8,
		.writeback_chroma_line_buffer_width_pixels = 4,
		.writeback_max_hscl_ratio = 1,
		.writeback_max_vscl_ratio = 1,
		.writeback_min_hscl_ratio = 1,
		.writeback_min_vscl_ratio = 1,
		.writeback_max_hscl_taps = 1,
		.writeback_max_vscl_taps = 1,
		.writeback_line_buffer_luma_buffer_size = 0,
		.writeback_line_buffer_chroma_buffer_size = 14643,
		.cursor_buffer_size = 8,
		.cursor_chunk_size = 2,
		.max_num_otg = 5,
		.max_num_dpp = 5,
		.max_num_wb = 1,
		.max_dchub_pscl_bw_pix_per_clk = 4,
		.max_pscl_lb_bw_pix_per_clk = 2,
		.max_lb_vscl_bw_pix_per_clk = 4,
		.max_vscl_hscl_bw_pix_per_clk = 4,
		.max_hscl_ratio = 6,
		.max_vscl_ratio = 6,
		.hscl_mults = 4,
		.vscl_mults = 4,
		.max_hscl_taps = 8,
		.max_vscl_taps = 8,
		.dispclk_ramp_margin_percent = 1,
		.underscan_factor = 1.11,
		.min_vblank_lines = 32,
		.dppclk_delay_subtotal = 46,
		.dynamic_metadata_vm_enabled = true,
		.dppclk_delay_scl_lb_only = 16,
		.dppclk_delay_scl = 50,
		.dppclk_delay_cnvc_formatter = 27,
		.dppclk_delay_cnvc_cursor = 6,
		.dispclk_delay_subtotal = 119,
		.dcfclk_cstate_latency = 5.2, // SRExitTime
		.max_inter_dcn_tile_repeaters = 8,
		.max_num_hdmi_frl_outputs = 1,
		.odm_combine_4to1_supported = true,

		.xfc_supported = false,
		.xfc_fill_bw_overhead_percent = 10.0,
		.xfc_fill_constant_bytes = 0,
		.gfx7_compat_tiling_supported = 0,
		.number_of_cursors = 1,
};
static const soc_bounding_box_st dcn302_soc_template = {
		

		 /* TODO: set this to actual min DCFCLK */
		
		.sr_exit_time_us = 26.5,
		.sr_enter_plus_exit_time_us = 31,
		.urgent_latency_us = 4.0,
		.urgent_latency_pixel_data_only_us = 4.0,
		.urgent_latency_pixel_mixed_with_vm_data_us = 4.0,
		.urgent_latency_vm_data_only_us = 4.0,
		.urgent_out_of_order_return_per_channel_pixel_only_bytes = 4096,
		.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = 4096,
		.urgent_out_of_order_return_per_channel_vm_only_bytes = 4096,
		.pct_ideal_dram_sdp_bw_after_urgent_pixel_only = 80.0,
		.pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm = 60.0,
		.pct_ideal_dram_sdp_bw_after_urgent_vm_only = 40.0,
		.max_avg_sdp_bw_use_normal_percent = 60.0,
		.max_avg_dram_bw_use_normal_percent = 40.0,
		.writeback_latency_us = 12.0,
		.max_request_size_bytes = 256,
		.fabric_datapath_to_dcn_data_return_bytes = 64,
		.dcn_downspread_percent = 0.5,
		.downspread_percent = 0.38,
		.dram_page_open_time_ns = 50.0,
		.dram_rw_turnaround_time_ns = 17.5,
		.dram_return_buffer_per_channel_bytes = 8192,
		.round_trip_ping_latency_dcfclk_cycles = 156,
		.urgent_out_of_order_return_per_channel_bytes = 4096,
		.channel_interleave_bytes = 256,
		.num_banks = 8,
		.gpuvm_min_page_size_bytes = 4096,
		.hostvm_min_page_size_bytes = 4096,
		.dram_clock_change_latency_us = 404,
		.dummy_pstate_latency_us = 5,
		.writeback_dram_clock_change_latency_us = 23.0,
		.return_bus_width_bytes = 64,
		
		.xfc_bus_transport_time_us = 20,      // ?
		.xfc_xbuf_latency_tolerance_us = 4,  // ?
		.use_urgent_burst_bw = 1,            // ?
		.do_urgent_latency_adjustment = true,
		.urgent_latency_adjustment_fabric_clock_component_us = 1.0,
		.urgent_latency_adjustment_fabric_clock_reference_mhz = 1000,
};
