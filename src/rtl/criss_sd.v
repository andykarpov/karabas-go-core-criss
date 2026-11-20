`timescale 1ns / 1ps
`default_nettype none
/*
 criss_sd.v

 The floppy images of the CRISS core: files mounted by the OSD items (kg_mcu2 file mounter, slots
 0..3 = drives A..D), sectors read and written by the helper. From doom_sd.v of core-doom (one slot,
 the backup RAM request dropped).

 MMIO word registers (offsets of criss_soc):
   0x80 W  SECTOR  the sector (512 bytes) of the next command
   0x84 W  CMD     bit 0: read the sector into the buffer, bit 1: write the buffer to it; bits 6..4 slot
                   (0..3 the drives A..D, 4 the backup RAM file)
   0x88 R  STAT    bit 0 busy, bit 1 the last command failed
   0xA0 + 4 * s R  SIZE of slot s in bytes (low 32 bits, 0: nothing mounted), s = 0..4
   0x90 W  BK_REQ  ask the firmware for the backup RAM file (kg_mcu2 BK_MOUNT, kind 0: <save_dir of
                   the .kg>/game.sav, made with the asked size): the value = its size in sectors
   0x1000 + 4 * i  R/W  byte i of the 512-byte sector buffer (bits 7..0)
 Every write goes to the file at once, so a flush of the MCU (IMG_FLUSH of slot 4) is answered as soon
 as no sector is in progress.
*/
module criss_sd (
	input wire clk,
	input wire reset,

	input wire io_we,
	input wire [13:0] io_off,
	input wire [31:0] io_wd,
	output reg [31:0] io_rd,

	// kg_mcu2 (ENABLE_IMG)
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
	output reg [7:0] sdc_byte_out_data = 8'd0,
	input wire [7:0] img_flush,
	output reg bk_mount = 1'b0,
	output reg bk_flushed = 1'b0,
	output reg [31:0] bk_size = 32'd0,
	output wire [7:0] dbg
);

reg [2:0] slot = 3'd0;
wire mine = sdc_slot == slot;
wire buf_sel = io_off[13:11] == 3'b010;   // 0x1000..0x17ff

reg [7:0] sbuf [0:511];
reg [31:0] sector = 32'd0;
reg rd_req = 1'b0, wr_req = 1'b0;
reg busy = 1'b0, wait_done = 1'b0, err = 1'b0;
reg [31:0] size0 = 32'd0, size1 = 32'd0, size2 = 32'd0, size3 = 32'd0, size4 = 32'd0;
reg flush_pend = 1'b0;
assign dbg = { rd_req, wr_req, busy, wait_done, err, slot };

assign sdc_rd = { 7'd0, rd_req } << slot;
assign sdc_wr = { 7'd0, wr_req } << slot;
assign sdc_sector = sector;

// port A (write and an asynchronous read): a byte of a read from the MCU, else the CPU (it reads and
// writes the buffer only while no command runs); port B (read): the byte of a write for the MCU.
wire mcu_w = busy && wait_done && sdc_byte_in_strobe && mine;
wire [8:0] a_addr = mcu_w ? sdc_byte_addr : io_off[10:2];
wire [7:0] a_q = sbuf[a_addr];
always @(posedge clk) begin
	if (mcu_w)
		sbuf[a_addr] <= sdc_byte_in_data;
	else if (io_we && buf_sel && !busy)
		sbuf[a_addr] <= io_wd[7:0];
end

// the byte for the MCU (a write): two clocks after its address (kg_mcu2 IMG_OUT_LATENCY 3)
reg [7:0] out1 = 8'd0;
always @(posedge clk) begin
	out1 <= sbuf[sdc_byte_addr];
	sdc_byte_out_data <= out1;
end

always @(*) begin
	if (buf_sel) io_rd = { 24'd0, a_q };
	else case (io_off)
		14'h0088: io_rd = { 30'd0, err, busy };
		14'h00A0: io_rd = size0;
		14'h00A4: io_rd = size1;
		14'h00A8: io_rd = size2;
		14'h00AC: io_rd = size3;
		14'h00B0: io_rd = size4;
		default: io_rd = 32'd0;
	endcase
end

always @(posedge clk) begin
	if (sdc_img_mounted[0]) size0 <= sdc_img_size[31:0];
	if (sdc_img_mounted[1]) size1 <= sdc_img_size[31:0];
	if (sdc_img_mounted[2]) size2 <= sdc_img_size[31:0];
	if (sdc_img_mounted[3]) size3 <= sdc_img_size[31:0];
	if (sdc_img_mounted[4]) size4 <= sdc_img_size[31:0];
	bk_mount <= 1'b0;
	bk_flushed <= 1'b0;
	if (img_flush[4]) flush_pend <= 1'b1;
	if (flush_pend && !busy) begin flush_pend <= 1'b0; bk_flushed <= 1'b1; end
	if (io_we && io_off == 14'h0090) begin bk_size <= io_wd; bk_mount <= 1'b1; end
	if (reset) begin
		busy <= 1'b0;
		wait_done <= 1'b0;
		rd_req <= 1'b0;
		wr_req <= 1'b0;
		err <= 1'b0;
	end else begin
		if (io_we && io_off == 14'h0080) sector <= io_wd;
		if (io_we && io_off == 14'h0084 && !busy) begin
			busy <= 1'b1;
			wait_done <= 1'b0;
			err <= 1'b0;
			rd_req <= io_wd[0];
			wr_req <= io_wd[1];
			slot <= io_wd[6:4];
		end
		// the request is held until the MCU takes it (busy of this slot), then its end (done)
		if (busy && !wait_done && sdc_busy && mine) begin
			rd_req <= 1'b0;
			wr_req <= 1'b0;
			wait_done <= 1'b1;
		end
		if (busy && wait_done && sdc_done && mine) begin
			busy <= 1'b0;
			wait_done <= 1'b0;
			err <= sdc_error;
		end
	end
end

endmodule
