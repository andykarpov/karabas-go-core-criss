`timescale 1ns / 1ps
`default_nettype none
// Ф1 step 3 bench top: criss_soc + criss_sd with the pins of the memories and the mounter (tb_soc.cpp).
module sim_top #(parameter [4:0] Z80_DIV = 5'd18) (
	input wire clk,
	input wire reset,
	input wire [7:0] ps2_code,
	input wire ps2_wr,
	output wire [12:0] sdr_a,
	output wire [1:0] sdr_ba,
	input wire [15:0] sdr_dq_i,
	output wire [15:0] sdr_dq_o,
	output wire [15:0] sdr_dq_oe_n,
	output wire [1:0] sdr_dqm,
	output wire sdr_ras_n,
	output wire sdr_cas_n,
	output wire sdr_we_n,
	output wire [20:0] MA,
	input wire [15:0] MD_I,
	output wire [15:0] MD_O,
	output wire [15:0] MD_OE_N,
	output wire [1:0] MWR_N,
	output wire [1:0] MRD_N,
	input wire [7:0] sdc_img_mounted,
	input wire [63:0] sdc_img_size,
	output wire [7:0] sdc_rd,
	output wire [7:0] sdc_wr,
	output wire [31:0] sdc_sector,
	input wire sdc_busy,
	input wire sdc_done,
	input wire sdc_error,
	input wire [2:0] sdc_slot,
	input wire sdc_byte_in_strobe,
	input wire [8:0] sdc_byte_addr,
	input wire [7:0] sdc_byte_in_data,
	output wire [7:0] sdc_byte_out_data,
	output wire [7:0] vstat,
	output wire [15:0] cursor,
	output wire [7:0] dbg_uart,
	output wire [15:0] dbg_z80_pc,
	output wire [7:0] dbg_state
);
wire sd_we;
wire [13:0] sd_off;
wire [31:0] sd_wd, sd_rd;
criss_soc #(.SDRAM_INIT_CLOCKS(15'd100), .Z80_DIV(Z80_DIV)) soc (
	.clk(clk), .reset(reset),
	.ld_kind(2'd0), .ld_active(1'b0), .ld_addr(32'd0), .ld_data(8'd0), .ld_wr(1'b0), .ld_ready(),
	.ps2_code(ps2_code), .ps2_wr(ps2_wr),
	.sd_we(sd_we), .sd_off(sd_off), .sd_wd(sd_wd), .sd_rd(sd_rd),
	.scr_we(), .scr_wa(), .scr_wd(), .font_we(), .font_wa(), .font_wd(), .col_we(), .col_wa(), .col_wd(), .vstat(vstat), .cursor(cursor), .beep(),
	.dbg_uart(dbg_uart), .cpu_run(), .dbg_z80_pc(dbg_z80_pc), .dbg_state(dbg_state),
	.sdr_a(sdr_a), .sdr_ba(sdr_ba), .sdr_dq_i(sdr_dq_i), .sdr_dq_o(sdr_dq_o), .sdr_dq_oe_n(sdr_dq_oe_n),
	.sdr_dqm(sdr_dqm), .sdr_ras_n(sdr_ras_n), .sdr_cas_n(sdr_cas_n), .sdr_we_n(sdr_we_n),
	.MA(MA), .MD_I(MD_I), .MD_O(MD_O), .MD_OE_N(MD_OE_N), .MWR_N(MWR_N), .MRD_N(MRD_N)
);
criss_sd sd (
	.clk(clk), .reset(reset),
	.io_we(sd_we), .io_off(sd_off), .io_wd(sd_wd), .io_rd(sd_rd),
	.sdc_img_mounted(sdc_img_mounted), .sdc_img_size(sdc_img_size), .sdc_rd(sdc_rd), .sdc_wr(sdc_wr),
	.sdc_sector(sdc_sector), .sdc_busy(sdc_busy), .sdc_done(sdc_done), .sdc_error(sdc_error), .sdc_slot(sdc_slot),
	.sdc_byte_in_strobe(sdc_byte_in_strobe), .sdc_byte_addr(sdc_byte_addr), .sdc_byte_in_data(sdc_byte_in_data),
	.sdc_byte_out_data(sdc_byte_out_data), .dbg()
);
endmodule
