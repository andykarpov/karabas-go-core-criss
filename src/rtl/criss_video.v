`timescale 1ns / 1ps
`default_nettype none
/*
 criss_video.v

 The text video of CRISS (the VGA MCU + the ATtiny13 sync generator, docs/re/periph.md 2): 640 x 480
 at 60 Hz (800 x 525, 25 MHz, syncs negative: the top inverts them; syncs and de always, also in the
 sleep mode VSTAT = 0, where the picture is black).

 VSTAT = LINES << 3 | code page. LINES 0..24 -> 24 x 80 (20 raster lines a row), 25..29 -> 25 x 80
 (19), 30 -> 30 x 64 (16), 31 -> 31 x 64 (15); 64-column rows are shown whole (64 columns, centred;
 the original shows 61, user decision 2026-11-20). Code page: 0..4 CP0..CP4 from raster line 0 of the
 font, 5 / 6 CP0 / CP4 from raster line 2, 7 the page of each row = low bits of the screen byte
 1920 + row (Z80 FF80 + row) in 24 x 80 (1..4 -> CP1..CP4, else CP0).

 Screen: a copy of Z80 F800..FFFF, written by every write to that RAM (scr_we on clk_sys). Fonts:
 32 KB, byte (page * 256 + char), page = font * 20 + raster line (fw/rom.h layout), written by the
 helper (font_we on clk_sys). Cursor: the character 0 of the font at the cursor offset (0xFFFF: off),
 0.5 s on / 0.5 s off (the peripheral MCU of CRISS does it the same way).
*/
module criss_video (
	input wire clk_sys,
	input wire scr_we,
	input wire [10:0] scr_wa,
	input wire [7:0] scr_wd,
	input wire font_we,
	input wire [14:0] font_wa,
	input wire [7:0] font_wd,
	input wire [7:0] vstat_i,     // clk_sys, static
	input wire [15:0] cursor_i,   // clk_sys, static

	input wire clk_pix,
	output reg [23:0] rgb = 24'd0,
	output reg hs = 1'b0,          // positive here
	output reg vs = 1'b0,
	output reg de = 1'b0
);

// ------------------------------------------------------------------ memories

reg [10:0] scr_ra = 11'd0;
wire [7:0] scr_q;
kg_sdpram #(.DW(8), .AW(11)) scr (
	.clka(clk_sys), .wea(scr_we), .addra(scr_wa), .dina(scr_wd),
	.clkb(clk_pix), .enb(1'b1), .addrb(scr_ra), .doutb(scr_q)
);

reg [14:0] font_ra = 15'd0;
wire [7:0] font_q;
kg_sdpram #(.DW(8), .AW(15)) font (
	.clka(clk_sys), .wea(font_we), .addra(font_wa), .dina(font_wd),
	.clkb(clk_pix), .enb(1'b1), .addrb(font_ra), .doutb(font_q)
);

// the code pages of the rows for page 7 (Z80 FF80..FF98), snooped on the writes
reg [2:0] rowcp [0:31];
always @(posedge clk_sys)
	if (scr_we && scr_wa[10:5] == 6'b111100)   // 0x780..0x79F = FF80..FF9F
		rowcp[scr_wa[4:0]] <= scr_wd[2:0];

// ------------------------------------------------------------------ mode (clk_pix)

reg [7:0] vstat_p = 8'd0, vstat = 8'h00;
reg [15:0] cur_p = 16'hFFFF, cursor = 16'hFFFF;
always @(posedge clk_pix) begin
	vstat_p <= vstat_i; cur_p <= cursor_i;
end

wire [4:0] lines_v = vstat[7:3];
reg [4:0] rows = 5'd24;
reg [4:0] rowh = 5'd20;
reg [6:0] chars = 7'd80;
reg sleep = 1'b0;

// ------------------------------------------------------------------ timing

reg [9:0] hc = 10'd0, vc = 10'd0;
reg [5:0] frames = 6'd0;
wire blink_on = frames < 6'd30;

// text position of the line
reg [4:0] row = 5'd0;           // text row
reg [4:0] rr = 5'd0;            // raster line in the row
reg [10:0] row_base = 11'd0;    // row * chars
reg vtext = 1'b0;               // inside the text rows

always @(posedge clk_pix) begin
	if (hc == 10'd799) begin
		hc <= 10'd0;
		if (vc == 10'd524) begin
			vc <= 10'd0;
			frames <= (frames == 6'd59) ? 6'd0 : frames + 6'd1;
			// a new mode takes effect at the frame start
			vstat <= vstat_p;
			cursor <= cur_p;
			sleep <= vstat_p == 8'd0;
			case (vstat_p[7:3])
			5'd30: begin rows <= 5'd30; rowh <= 5'd16; chars <= 7'd64; end
			5'd31: begin rows <= 5'd31; rowh <= 5'd15; chars <= 7'd64; end
			5'd25, 5'd26, 5'd27, 5'd28, 5'd29: begin rows <= 5'd25; rowh <= 5'd19; chars <= 7'd80; end
			default: begin rows <= 5'd24; rowh <= 5'd20; chars <= 7'd80; end
			endcase
			row <= 5'd0; rr <= 5'd0; row_base <= 11'd0; vtext <= 1'b1;
		end else begin
			vc <= vc + 10'd1;
			if (vtext) begin
				if (rr == rowh - 5'd1) begin
					rr <= 5'd0;
					row_base <= row_base + chars;
					if (row == rows - 5'd1) vtext <= 1'b0;
					row <= row + 5'd1;
				end else
					rr <= rr + 5'd1;
			end
		end
	end else
		hc <= hc + 10'd1;
end

// ------------------------------------------------------------------ fetch pipeline
// column c (pixels x0 + 8c ..) : the screen address at hc = x0 + 8c - 8 (stage 0), its byte one
// clock later (stage 1), the font address (stage 2), the font byte (stage 3), kept in next (stage 4);
// the shift register takes next at hc = x0 + 8c - 1, so its bit 7 is pixel x0 + 8c on the output.

wire wide = chars == 7'd80;
wire [9:0] x0 = wide ? 10'd0 : 10'd64;
wire [9:0] xw = wide ? 10'd640 : 10'd512;
wire [9:0] hf = hc + 10'd8 - x0;          // fetch position: column = hf >> 3
wire [9:0] hl = hc + 10'd1 - x0;          // load position
wire vis_line = vc < 10'd480 && vtext;
wire fetch0 = (hc + 10'd8 >= x0) && (hf < xw) && hf[2:0] == 3'd0 && vis_line;
wire load = (hc + 10'd1 >= x0) && (hl < xw) && hl[2:0] == 3'd0 && vis_line;

// the page of the row: CP0..4 -> font * 20, 5 / 6 -> CP0 / CP4 + 2, 7 -> per row
reg [2:0] cp_row;
always @(*) begin
	cp_row = vstat[2:0];
	if (vstat[2:0] == 3'd7) cp_row = (rowcp[row] >= 3'd1 && rowcp[row] <= 3'd4) ? rowcp[row] : 3'd0;
end
reg [6:0] page_base;
always @(*) begin
	case (cp_row)
	3'd1: page_base = 7'd20;
	3'd2: page_base = 7'd40;
	3'd3: page_base = 7'd60;
	3'd4: page_base = 7'd80;
	3'd5: page_base = 7'd2;
	3'd6: page_base = 7'd82;
	default: page_base = 7'd0;
	endcase
end
wire [6:0] page = page_base + rr;
wire [10:0] addr_c = row_base + { 4'd0, hf[9:3] };

reg [3:0] st = 4'd0;            // stage flags 1..3
reg cur1 = 1'b0, cur2 = 1'b0;
reg [7:0] next = 8'd0, sh = 8'd0;
always @(posedge clk_pix) begin
	st <= { st[2:0], fetch0 };
	if (fetch0) begin
		scr_ra <= addr_c;
		cur1 <= blink_on && { 5'd0, addr_c } == cursor;
	end
	cur2 <= cur1;
	if (st[1])          // scr_q = the byte of scr_ra (stage 1 read it)
		font_ra <= { page, 8'd0 } + { 7'd0, cur2 ? 8'd0 : scr_q };
	if (st[3])          // font_q = the byte of font_ra
		next <= font_q;
	sh <= load ? next : { sh[6:0], 1'b0 };
end

// ------------------------------------------------------------------ output (one clock after sh)

always @(posedge clk_pix) begin
	hs <= hc >= 10'd656 && hc < 10'd752;
	vs <= vc >= 10'd490 && vc < 10'd492;
	de <= hc < 10'd640 && vc < 10'd480;
	rgb <= (!sleep && sh[7]) ? 24'h00FF00 : 24'h000000;
end

endmodule
