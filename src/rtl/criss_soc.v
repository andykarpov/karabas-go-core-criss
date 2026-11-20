`timescale 1ns / 1ps
`default_nettype none
/*
 criss_soc.v

 The CRISS machine without the board glue (docs/PLAN.md Ф1): the Z80 (criss_z80: T80 + BIOS traps)
 with its 64 KB RAM in the board SRAM, and the helper CPU (VexRiscv rv32im, I$ 4K, D$ 4K, from
 core-doom) that serves the traps; its program and the data ROM of the .kg in the SDRAM. One clock
 (clk, 75 MHz); the Z80 runs at clk / Z80_DIV.

 The SRAM belongs to the Z80, except while the Z80 is held in reset or stopped (frozen): then the
 helper reads and writes it through the window below. Every write to Z80 F800..FFFF (by either) goes
 to the screen copy of the video (scr_*).

 Memory map of the helper (src/fw/hw_rv.c):
   0x10000000 SDRAM 32 MB: the program (ROM of the .kg at 0), the data ROM (at 0x100000)
   0x80000000 MMIO (uncached: VexRiscv IO range = bit 31), word registers:
     00 W  DBG: a byte of the debug output (dbg_uart)
     04 W  Z80: bit 0 hold in reset, bit 1 stop at the next M1 (freeze)
     08 R  Z80 state: bit 0 frozen, bits 15..8 trap code (ED opcode, FF = freeze)
     0c W  RESUME (any value)
     10 W  DIRSET (any value): the registers of DIR0..6 into the Z80
     14 R  MS: milliseconds since reset
     18 R  PS2: bit 8 a byte, bits 7..0 the byte (the read takes it from the FIFO)
     1c W  VIDEO: bits 7..0 VSTAT, bits 31..16 cursor offset (FFFF off)
     20 W  BEEP: milliseconds of the bell
     24 R  CYCLES: clocks since reset
     40..58 R REG0..6 (the 212 bits of T80 REG), W DIR0..6
     80..ac, 1000..17fc: criss_sd (sd_*)
   0x90000000 + 4 * a  the Z80 RAM byte a (bits 7..0), 64 KB
   0xA0000000 + 4 * i  the font byte i (write only), 32 KB
*/
module criss_soc #(
	parameter SDRAM_INIT_CLOCKS = 15'd16000,
	parameter MS_DIV = 17'd75000,         // clocks per millisecond
	parameter [4:0] Z80_DIV = 5'd18,      // 75 / 18 = 4.17 MHz
	parameter [31:0] RESET_VECTOR = 32'h10000000
) (
	input wire clk,
	input wire reset,

	// loader (kg_mcu2): kind 0 = the ROMs of the .kg into the SDRAM
	input wire [1:0] ld_kind,
	input wire ld_active,
	input wire [31:0] ld_addr,
	input wire [7:0] ld_data,
	input wire ld_wr,
	output wire ld_ready,

	// PS/2 keyboard bytes (kg_mcu2 PS2_CODE / PS2_WR of the keyboard)
	input wire [7:0] ps2_code,
	input wire ps2_wr,

	// criss_sd
	output wire sd_we,
	output wire [13:0] sd_off,
	output wire [31:0] sd_wd,
	input wire [31:0] sd_rd,

	// video
	output reg scr_we = 1'b0,
	output reg [10:0] scr_wa = 11'd0,
	output reg [7:0] scr_wd = 8'd0,
	output reg font_we = 1'b0,
	output reg [14:0] font_wa = 15'd0,
	output reg [7:0] font_wd = 8'd0,
	output reg [7:0] vstat = 8'hC0,
	output reg [15:0] cursor = 16'hFFFF,
	output wire beep,

	// debug
	output reg [7:0] dbg_uart = 8'd0,
	output wire cpu_run,
	output wire [15:0] dbg_z80_pc,
	output wire [7:0] dbg_state,

	// SDRAM pins (the top makes the tristate)
	output wire [12:0] sdr_a,
	output wire [1:0] sdr_ba,
	input wire [15:0] sdr_dq_i,
	output wire [15:0] sdr_dq_o,
	output wire [15:0] sdr_dq_oe_n,
	output wire [1:0] sdr_dqm,
	output wire sdr_ras_n,
	output wire sdr_cas_n,
	output wire sdr_we_n,

	// SRAM pins
	output reg [20:0] MA = 21'd0,
	input wire [15:0] MD_I,
	output reg [15:0] MD_O = 16'd0,
	output reg [15:0] MD_OE_N = 16'hFFFF,
	output reg [1:0] MWR_N = 2'b11,
	output reg [1:0] MRD_N = 2'b11
);

// ------------------------------------------------------------------ reset / loader

wire sdram_ready;
reg cpu_reset = 1'b1;
assign cpu_run = ~cpu_reset;
always @(posedge clk)
	cpu_reset <= reset | ld_active | ~sdram_ready;

reg ld_pend = 1'b0;
reg [24:0] ld_a = 25'd0;
reg [7:0] ld_d = 8'd0;
assign ld_ready = ~ld_pend;

// ------------------------------------------------------------------ helper CPU

wire i_cyc, i_stb, d_cyc, d_stb, d_we;
wire [29:0] i_adr, d_adr;
wire [31:0] d_dat_o;
wire [3:0] d_sel;
wire [2:0] i_cti, d_cti;
reg [31:0] i_dat_i, d_dat_i;
reg i_ack, d_ack;

VexRiscv cpu (
	.externalResetVector(RESET_VECTOR),
	.timerInterrupt(1'b0),
	.softwareInterrupt(1'b0),
	.externalInterruptArray(32'd0),
	.iBusWishbone_CYC(i_cyc),
	.iBusWishbone_STB(i_stb),
	.iBusWishbone_ACK(i_ack),
	.iBusWishbone_WE(),
	.iBusWishbone_ADR(i_adr),
	.iBusWishbone_DAT_MISO(i_dat_i),
	.iBusWishbone_DAT_MOSI(),
	.iBusWishbone_SEL(),
	.iBusWishbone_ERR(1'b0),
	.iBusWishbone_CTI(i_cti),
	.iBusWishbone_BTE(),
	.dBusWishbone_CYC(d_cyc),
	.dBusWishbone_STB(d_stb),
	.dBusWishbone_ACK(d_ack),
	.dBusWishbone_WE(d_we),
	.dBusWishbone_ADR(d_adr),
	.dBusWishbone_DAT_MISO(d_dat_i),
	.dBusWishbone_DAT_MOSI(d_dat_o),
	.dBusWishbone_SEL(d_sel),
	.dBusWishbone_ERR(1'b0),
	.dBusWishbone_CTI(d_cti),
	.dBusWishbone_BTE(),
	.clk(clk),
	.reset(cpu_reset)
);

wire d_req = d_cyc & d_stb;
wire i_req = i_cyc & i_stb;
wire d_io = d_adr[29];                       // 0x80000000
wire d_sd = ~d_io;                           // 0x10000000 (and anything below 0x80000000)
wire [2:0] d_rgn = d_adr[28:26];             // 0x8 regs, 0x9 Z80 RAM, 0xA fonts

// ------------------------------------------------------------------ SDRAM and its owner

localparam [1:0] O_NONE = 2'd0, O_D = 2'd1, O_I = 2'd2, O_LD = 2'd3;
reg [1:0] own = O_NONE;
wire sd_ack;
wire [31:0] sd_dat;
reg sdr_cyc, sdr_we;
reg [3:0] sdr_sel;
reg [24:2] sdr_adr;
reg [2:0] sdr_cti;
reg [31:0] sdr_wdat;

always @(*) begin
	sdr_cyc = 1'b0; sdr_we = 1'b0; sdr_sel = 4'hF; sdr_adr = 23'd0; sdr_cti = 3'd0; sdr_wdat = 32'd0;
	case (own)
	O_D: begin sdr_cyc = d_req; sdr_we = d_we; sdr_sel = d_sel; sdr_adr = d_adr[22:0]; sdr_cti = d_cti; sdr_wdat = d_dat_o; end
	O_I: begin sdr_cyc = i_req; sdr_we = 1'b0; sdr_sel = 4'hF; sdr_adr = i_adr[22:0]; sdr_cti = i_cti; end
	O_LD: begin sdr_cyc = ld_pend; sdr_we = 1'b1; sdr_sel = 4'b0001 << ld_a[1:0]; sdr_adr = ld_a[24:2]; sdr_cti = 3'd0; sdr_wdat = {4{ld_d}}; end
	default: ;
	endcase
end

always @(posedge clk) begin
	if (cpu_reset) begin
		own <= ld_pend ? O_LD : O_NONE;
		if (own == O_LD && sd_ack) own <= O_NONE;
	end else if (own == O_NONE) begin
		if (d_req && d_sd) own <= O_D;
		else if (i_req) own <= O_I;
	end else if (sd_ack && sdr_cti != 3'b010)
		own <= O_NONE;
end

doom_sdram #(.INIT_CLOCKS(SDRAM_INIT_CLOCKS)) sdram (
	.clk(clk),
	.reset(reset),
	.ready(sdram_ready),
	.wb_cyc(sdr_cyc), .wb_stb(sdr_cyc), .wb_we(sdr_we), .wb_sel(sdr_sel), .wb_adr(sdr_adr), .wb_cti(sdr_cti),
	.wb_dat_i(sdr_wdat), .wb_dat_o(sd_dat), .wb_ack(sd_ack),
	.sdr_a(sdr_a), .sdr_ba(sdr_ba), .sdr_dq_i(sdr_dq_i), .sdr_dq_o(sdr_dq_o), .sdr_dq_oe_n(sdr_dq_oe_n),
	.sdr_dqm(sdr_dqm), .sdr_ras_n(sdr_ras_n), .sdr_cas_n(sdr_cas_n), .sdr_we_n(sdr_we_n)
);

// ------------------------------------------------------------------ Z80

reg z_hold = 1'b1, z_freeze = 1'b0;
reg z_resume = 1'b0, z_dirset = 1'b0;
reg [211:0] z_dir = 212'd0;
wire [211:0] z_reg;
wire z_frozen;
wire [7:0] z_trap;
wire [15:0] z_a;
wire [7:0] z_do;
reg [7:0] z_di;
wire z_m1_n, z_mreq_n, z_iorq_n, z_rd_n, z_wr_n, z_halt_n;
wire z_reset_n = ~(cpu_reset | z_hold);

reg [4:0] z_div = 5'd0;
reg z_cen = 1'b0;
always @(posedge clk) begin
	z_cen <= 1'b0;
	if (z_div == Z80_DIV - 5'd1) begin
		z_div <= 5'd0;
		z_cen <= 1'b1;
	end else
		z_div <= z_div + 5'd1;
end

criss_z80 z80 (
	.clk(clk), .cen(z_cen), .reset_n(z_reset_n),
	.a(z_a), .di(z_di), .dout(z_do), .m1_n(z_m1_n), .mreq_n(z_mreq_n), .iorq_n(z_iorq_n),
	.rd_n(z_rd_n), .wr_n(z_wr_n), .halt_n(z_halt_n), .int_n(1'b1), .nmi_n(1'b1),
	.freeze_req(z_freeze), .frozen(z_frozen), .trap_code(z_trap), .reg_q(z_reg),
	.dir_set(z_dirset), .dir_d(z_dir), .resume(z_resume), .dbg_state()
);
assign dbg_z80_pc = z_reg[79:64];
assign dbg_state = { z_frozen, z_hold, z_freeze, ~z_halt_n, own, cpu_reset, sdram_ready };

// the helper owns the SRAM while the Z80 is in reset or stopped
wire h_own = ~z_reset_n | z_frozen;

// ------------------------------------------------------------------ SRAM

// helper access: a byte, IO_RD_CLK clocks for a read, a write pulse of IO_WR_CLK clocks
reg [2:0] hm_cnt = 3'd0;
reg hm_busy = 1'b0, hm_ack = 1'b0;
reg [7:0] hm_q = 8'd0;
wire h_mem = d_req && d_io && d_rgn == 3'b001 && !cpu_reset;
wire [15:0] h_a = d_adr[15:0];

always @(posedge clk) begin
	hm_ack <= 1'b0;
	scr_we <= 1'b0;
	if (h_own) begin
		if (h_mem && !hm_busy && !hm_ack) begin
			hm_busy <= 1'b1;
			hm_cnt <= 3'd0;
			MA <= { 5'd0, h_a[15:1] };
			MD_O <= { d_dat_o[7:0], d_dat_o[7:0] };
			MRD_N <= 2'b11;
			MWR_N <= 2'b11;
			MD_OE_N <= 16'hFFFF;
			if (d_we) begin
				MD_OE_N <= 16'h0000;
				if (h_a >= 16'hF800) begin
					scr_we <= 1'b1;
					scr_wa <= h_a[10:0];
					scr_wd <= d_dat_o[7:0];
				end
			end
		end else if (hm_busy) begin
			hm_cnt <= hm_cnt + 3'd1;
			if (d_we) begin
				MWR_N <= (hm_cnt >= 3'd1 && hm_cnt <= 3'd3) ? (h_a[0] ? 2'b01 : 2'b10) : 2'b11;
				if (hm_cnt == 3'd5) begin hm_busy <= 1'b0; hm_ack <= 1'b1; MD_OE_N <= 16'hFFFF; end
			end else begin
				MRD_N <= 2'b00;
				if (hm_cnt == 3'd3) begin
					hm_q <= h_a[0] ? MD_I[15:8] : MD_I[7:0];
					hm_busy <= 1'b0;
					hm_ack <= 1'b1;
				end
			end
		end else begin
			MRD_N <= 2'b11;
			MWR_N <= 2'b11;
			MD_OE_N <= 16'hFFFF;
		end
	end else begin
		hm_busy <= 1'b0;
		// a helper access while the Z80 runs (a firmware error): answered at once with FF
		if (h_mem && !hm_ack) begin hm_ack <= 1'b1; hm_q <= 8'hFF; end
		// the Z80: address all the time, /OE low except while writing (T80 takes the read data on
		// a Z80 clock edge: core-rc2014 lesson), /WE while MREQ and WR
		MA <= { 5'd0, z_a[15:1] };
		MD_O <= { z_do, z_do };
		if (!z_mreq_n && !z_wr_n) begin
			MRD_N <= 2'b11;
			MD_OE_N <= 16'h0000;
			MWR_N <= z_a[0] ? 2'b01 : 2'b10;
			if (z_a >= 16'hF800) begin
				scr_we <= 1'b1;
				scr_wa <= z_a[10:0];
				scr_wd <= z_do;
			end
		end else begin
			MWR_N <= 2'b11;
			MD_OE_N <= 16'hFFFF;
			MRD_N <= 2'b00;
		end
	end
end

// the Z80 data: memory (the byte lane of A0), I/O reads FF (Ф3: the ports)
always @(*) begin
	if (!z_iorq_n) z_di = 8'hFF;
	else z_di = z_a[0] ? MD_I[15:8] : MD_I[7:0];
end

// ------------------------------------------------------------------ MMIO

reg [31:0] cycles = 32'd0, ms = 32'd0;
reg [16:0] ms_cnt = 17'd0;
reg io_ack = 1'b0;
reg [31:0] io_dat = 32'd0;
wire [13:0] io_off = { d_adr[11:0], 2'b00 };
wire io_regs = d_req && d_io && d_rgn == 3'b000 && !cpu_reset;
wire io_font = d_req && d_io && d_rgn == 3'b010 && !cpu_reset;
wire sd_sel = io_off[13:11] == 3'b010 || (io_off[13:8] == 6'd0 && io_off[7] == 1'b1);   // 0x1000.., 0x80..0xfc
assign sd_we = io_regs && !io_ack && d_we && sd_sel;
assign sd_off = io_off;
assign sd_wd = d_dat_o;

// PS/2 FIFO, 16 bytes
reg [7:0] kq [0:15];
reg [3:0] kq_w = 4'd0, kq_r = 4'd0;
wire kq_empty = kq_w == kq_r;

reg [15:0] beep_ms = 16'd0;
assign beep = beep_ms != 16'd0;

always @(posedge clk) begin
	io_ack <= 1'b0;
	z_resume <= 1'b0;
	z_dirset <= 1'b0;
	font_we <= 1'b0;
	if (ps2_wr && (kq_w + 4'd1 != kq_r)) begin
		kq[kq_w] <= ps2_code;
		kq_w <= kq_w + 4'd1;
	end
	if (cpu_reset) begin
		cycles <= 32'd0;
		ms <= 32'd0;
		ms_cnt <= 17'd0;
		z_hold <= 1'b1;
		z_freeze <= 1'b0;
	end else begin
		cycles <= cycles + 32'd1;
		if (ms_cnt == MS_DIV - 17'd1) begin
			ms_cnt <= 17'd0;
			ms <= ms + 32'd1;
			if (beep_ms != 16'd0) beep_ms <= beep_ms - 16'd1;
		end else
			ms_cnt <= ms_cnt + 17'd1;
	end

	if (io_font && !io_ack) begin
		io_ack <= 1'b1;
		font_we <= d_we;
		font_wa <= d_adr[14:0];
		font_wd <= d_dat_o[7:0];
	end

	if (io_regs && !io_ack) begin
		io_ack <= 1'b1;
		if (d_we) begin
			if (!sd_sel) case (io_off[7:0])
				8'h00: dbg_uart <= d_dat_o[7:0];
				8'h04: begin z_hold <= d_dat_o[0]; z_freeze <= d_dat_o[1]; end
				8'h0c: z_resume <= 1'b1;
				8'h10: z_dirset <= 1'b1;
				8'h1c: begin vstat <= d_dat_o[7:0]; cursor <= d_dat_o[31:16]; end
				8'h20: beep_ms <= d_dat_o[15:0];
				8'h40: z_dir[31:0] <= d_dat_o;
				8'h44: z_dir[63:32] <= d_dat_o;
				8'h48: z_dir[95:64] <= d_dat_o;
				8'h4c: z_dir[127:96] <= d_dat_o;
				8'h50: z_dir[159:128] <= d_dat_o;
				8'h54: z_dir[191:160] <= d_dat_o;
				8'h58: z_dir[211:192] <= d_dat_o[19:0];
				default: ;
			endcase
		end else if (sd_sel)
			io_dat <= sd_rd;
		else case (io_off[7:0])
			8'h08: io_dat <= { 16'd0, z_trap, 7'd0, z_frozen };
			8'h14: io_dat <= ms;
			8'h18: begin
				io_dat <= { 23'd0, ~kq_empty, kq[kq_r] };
				if (!kq_empty) kq_r <= kq_r + 4'd1;
			end
			8'h24: io_dat <= cycles;
			8'h40: io_dat <= z_reg[31:0];
			8'h44: io_dat <= z_reg[63:32];
			8'h48: io_dat <= z_reg[95:64];
			8'h4c: io_dat <= z_reg[127:96];
			8'h50: io_dat <= z_reg[159:128];
			8'h54: io_dat <= z_reg[191:160];
			8'h58: io_dat <= { 12'd0, z_reg[211:192] };
			default: io_dat <= 32'd0;
		endcase
	end

	// loader
	if (ld_wr && !ld_pend && ld_kind == 2'd0) begin
		ld_pend <= 1'b1;
		ld_d <= ld_data;
		ld_a <= ld_addr[24:0];
	end
	if (own == O_LD && sd_ack) ld_pend <= 1'b0;
end

// ------------------------------------------------------------------ CPU bus answers

always @(*) begin
	i_ack = (own == O_I) & sd_ack;
	i_dat_i = sd_dat;
	d_ack = ((own == O_D) & sd_ack) | io_ack | hm_ack;
	d_dat_i = !d_io ? sd_dat : (d_rgn == 3'b001) ? { 24'd0, hm_q } : io_dat;
end

endmodule
