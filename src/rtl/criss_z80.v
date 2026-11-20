`timescale 1ns / 1ps
`default_nettype none
/*
 criss_z80.v

 The Z80 of the CRISS core: T80s (Mode 0) with the BIOS traps of the CRISS firmware (docs/PLAN.md,
 docs/re/cpu-core.md).

 Trap opcodes: ED 00..27 (BIOS 0..38, BDOS), ED 77 (MUL), ED 7F (DIV), ED A4..A7 (FL32 0..3). T80
 runs them as NOPs. The opcode stream is followed on the M1 cycles (prefix state: ED ED 02 is not a
 trap, DD / FD before ED is ignored as on a Z80, CB xx and DD CB d xx are not ED instructions); after
 a trap opcode the CPU is stopped with WAIT on T2 of the next M1 (PC = the address after the trap),
 INT and NMI closed from the trap opcode to the release. The helper then reads REG, may write DIR
 (dir_set: one clock, all registers in the REG order, PC included) and ends with resume.

 freeze_req (level) stops the CPU the same way at its next M1 without a trap (trap_code 0xFF): the
 boot (PC = SP = 0x0100 through DIR), a stop asked by the helper.

 frozen: the CPU waits on an M1 and the helper may use the memory; it drops with resume, WAIT is
 released REL_CE Z80 clocks later (the memory is the Z80's again for a whole clock first).
*/
module criss_z80 #(
	parameter [1:0] REL_CE = 2'd2
) (
	input wire clk,
	input wire cen,                    // the Z80 clock enable
	input wire reset_n,

	output wire [15:0] a,
	input wire [7:0] di,
	output wire [7:0] dout,
	output wire m1_n,
	output wire mreq_n,
	output wire iorq_n,
	output wire rd_n,
	output wire wr_n,
	output wire halt_n,
	input wire int_n,
	input wire nmi_n,

	input wire freeze_req,
	output reg frozen = 1'b0,
	output reg [7:0] trap_code = 8'hFF,  // the ED opcode of the trap, FF: freeze_req
	output wire [211:0] reg_q,
	input wire dir_set,
	input wire [211:0] dir_d,
	input wire resume,
	output wire [1:0] dbg_state
);

// ------------------------------------------------------------------ opcode stream

localparam [1:0] S_N = 2'd0, S_E = 2'd1, S_X = 2'd2, S_C = 2'd3;
reg [1:0] st = S_N;
reg [7:0] opc = 8'h00;
reg m1_rd = 1'b0;          // an opcode read of this M1 was seen (not an interrupt acknowledge)
reg m1_prev = 1'b1;
reg trap_pend = 1'b0;
reg [7:0] pend_code = 8'hFF;
reg wait_on = 1'b0;
reg [1:0] rel_n = 2'd0;
reg relq = 1'b0;           // resume taken, WAIT held for REL_CE clocks
reg ce_seen = 1'b0;        // a Z80 clock went by with WAIT low on this M1 (the CPU is in its wait)

assign dbg_state = st;

function is_trap(input [7:0] b);
	is_trap = (b <= 8'h27) || b == 8'h77 || b == 8'h7F || (b >= 8'hA4 && b <= 8'hA7);
endfunction

always @(posedge clk) begin
	if (!reset_n) begin
		st <= S_N;
		m1_rd <= 1'b0;
		m1_prev <= 1'b1;
		trap_pend <= 1'b0;
		wait_on <= 1'b0;
		frozen <= 1'b0;
		relq <= 1'b0;
		rel_n <= 2'd0;
		ce_seen <= 1'b0;
	end else begin
		m1_prev <= m1_n;
		// the opcode byte: the data of the M1 read (stable while RD is low)
		if (!m1_n && !mreq_n && !rd_n && !wait_on) begin
			opc <= di;
			m1_rd <= 1'b1;
		end
		// end of an M1: follow the prefixes
		if (m1_n && !m1_prev) begin
			m1_rd <= 1'b0;
			if (!m1_rd)
				st <= S_N;               // interrupt acknowledge
			else case (st)
			S_E: begin
				st <= S_N;
				if (is_trap(opc)) begin
					trap_pend <= 1'b1;
					pend_code <= opc;
				end
			end
			S_C: st <= S_N;
			S_X: st <= (opc == 8'hED) ? S_E : (opc == 8'hDD || opc == 8'hFD) ? S_X : S_N;
			default: st <= (opc == 8'hED) ? S_E : (opc == 8'hDD || opc == 8'hFD) ? S_X :
				(opc == 8'hCB) ? S_C : S_N;
			endcase
		end
		// stop on the next M1 (WAIT is sampled by T80 on T2)
		if (!m1_n && m1_prev && !wait_on && (trap_pend || freeze_req)) begin
			wait_on <= 1'b1;
			ce_seen <= 1'b0;
			trap_code <= trap_pend ? pend_code : 8'hFF;
			trap_pend <= 1'b0;
		end
		if (wait_on && !relq && cen) ce_seen <= 1'b1;
		if (wait_on && !relq && ce_seen && cen) frozen <= 1'b1;
		if (frozen && resume) begin
			frozen <= 1'b0;
			relq <= 1'b1;
			rel_n <= REL_CE;
		end
		if (relq && cen) begin
			if (rel_n == 2'd0) begin
				relq <= 1'b0;
				wait_on <= 1'b0;
			end else
				rel_n <= rel_n - 2'd1;
		end
	end
end

// interrupts closed from the trap opcode (ED seen) to the release
wire closed = st == S_E || trap_pend || wait_on;
// WAIT from the first clock of the M1 (T80 samples it on T2, which may be the next clock)
wire stop_now = !m1_n && m1_prev && (trap_pend || freeze_req);

T80s #(.Mode(0), .T2Write(1), .IOWait(1)) cpu (
	.RESET_n(reset_n),
	.CLK(clk),
	.CEN(cen),
	.WAIT_n(~(wait_on | stop_now)),
	.INT_n(int_n | closed),
	.NMI_n(nmi_n | closed),
	.BUSRQ_n(1'b1),
	.M1_n(m1_n),
	.MREQ_n(mreq_n),
	.IORQ_n(iorq_n),
	.RD_n(rd_n),
	.WR_n(wr_n),
	.RFSH_n(),
	.HALT_n(halt_n),
	.BUSAK_n(),
	.OUT0(1'b0),
	.A(a),
	.DI(di),
	.DO(dout),
	.REG(reg_q),
	.DIRSet(dir_set),
	.DIR(dir_d)
);

endmodule
