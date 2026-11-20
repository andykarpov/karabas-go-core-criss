/*
 The UART to the ESP8266 of the board (docs/PLAN.md Ф4 step 1): 8N1 at clk / DIV (75 MHz / 651 =
 115207 baud, the stock AT firmware runs at 115200 with CTS flow control), a 2 KB receive FIFO (the
 helper reads it), CTS to the module high (stop) while more than 1792 bytes wait.

 tx_wr with tx_data starts a byte when !tx_busy. rx_rd takes the byte shown on rx_data (rx_valid).
 GPL-3 (LICENSE).
*/
module criss_esp #(
	parameter [15:0] DIV = 16'd651
)(
	input wire clk,
	input wire reset,

	input wire rxd,
	output reg txd = 1'b1,
	output wire cts_n,

	input wire [7:0] tx_data,
	input wire tx_wr,
	output wire tx_busy,

	output reg [7:0] rx_data = 8'd0,
	output wire rx_valid,
	input wire rx_rd,
	output reg rx_over = 1'b0
);

// ------------------------------------------------------------------ transmitter
reg [15:0] t_cnt = 16'd0;
reg [3:0] t_bit = 4'd0;            // 0 idle, 1..10 start, 8 data, stop
reg [8:0] t_sh = 9'h1FF;
assign tx_busy = t_bit != 4'd0;

always @(posedge clk) begin
	if (reset) begin
		t_bit <= 4'd0; txd <= 1'b1;
	end else if (t_bit == 4'd0) begin
		if (tx_wr) begin
			t_sh <= { tx_data, 1'b0 };
			t_bit <= 4'd1;
			t_cnt <= 16'd0;
			txd <= 1'b0;
		end
	end else if (t_cnt == DIV - 16'd1) begin
		t_cnt <= 16'd0;
		if (t_bit == 4'd10) begin
			t_bit <= 4'd0; txd <= 1'b1;
		end else begin
			t_sh <= { 1'b1, t_sh[8:1] };
			txd <= t_bit == 4'd9 ? 1'b1 : t_sh[1];
			t_bit <= t_bit + 4'd1;
		end
	end else
		t_cnt <= t_cnt + 16'd1;
end

// ------------------------------------------------------------------ receiver
reg [2:0] r_sync = 3'b111;
always @(posedge clk) r_sync <= { r_sync[1:0], rxd };
wire r_in = r_sync[2];

reg [15:0] r_cnt = 16'd0;
reg [3:0] r_bit = 4'd0;            // 0 idle, 1 start, 2..9 data, 10 stop
reg [7:0] r_sh = 8'd0;
reg r_done = 1'b0;

always @(posedge clk) begin
	r_done <= 1'b0;
	if (reset) r_bit <= 4'd0;
	else if (r_bit == 4'd0) begin
		if (!r_in) begin r_bit <= 4'd1; r_cnt <= { 1'b0, DIV[15:1] }; end   // to the middle of the start bit
	end else if (r_cnt == 16'd0) begin
		r_cnt <= DIV - 16'd1;
		case (r_bit)
		4'd1: r_bit <= r_in ? 4'd0 : 4'd2;                  // a glitch: back to idle
		4'd10: begin r_bit <= 4'd0; r_done <= r_in; end     // a framing error drops the byte
		default: begin r_sh <= { r_in, r_sh[7:1] }; r_bit <= r_bit + 4'd1; end
		endcase
	end else
		r_cnt <= r_cnt - 16'd1;
end

// ------------------------------------------------------------------ receive FIFO (block RAM)
reg [7:0] fifo [0:2047];
reg [10:0] f_w = 11'd0, f_r = 11'd0;
reg [10:0] f_wv = 11'd0;           // f_w a clock later: a byte is shown once rx_data has it
wire [10:0] f_n = f_w - f_r;
assign rx_valid = f_wv != f_r;
assign cts_n = f_n > 11'd1792;

always @(posedge clk) begin
	if (reset) begin
		f_w <= 11'd0; f_wv <= 11'd0; f_r <= 11'd0; rx_over <= 1'b0;
	end else begin
		if (r_done) begin
			if (f_n == 11'd2047) rx_over <= 1'b1;
			else begin fifo[f_w] <= r_sh; f_w <= f_w + 11'd1; end
		end
		if (rx_rd && rx_valid) f_r <= f_r + 11'd1;
		f_wv <= f_w;
	end
	rx_data <= fifo[rx_rd && rx_valid ? f_r + 11'd1 : f_r];
end

endmodule
