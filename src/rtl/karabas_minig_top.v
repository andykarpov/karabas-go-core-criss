`timescale 1ns / 1ps
`default_nettype wire

/*
 CRISS CP/M core for Karabas Mini rev.G (HDMI, PCM5102): criss_soc (T80 + the helper VexRiscv,
 docs/PLAN.md) with the board glue of karabas-go-modules; the board top of core-doom.

 Clocks: one PLL, VCO 750 MHz (50 * 15): clk_sys 75 MHz (the SoC, the MCU link), clk_sdr 75 MHz at 225
 degrees (SDR_CLK through an ODDR2), clk_pix 25 MHz (640 x 480 at 60 Hz, the OSD, HDMI), clk_hdmi /
 clk_hdmi_n 125 MHz (the serialiser), clk_8 8.33 MHz (FT_CLK_OUT).

 Memory: the helper program and the data ROM (the ROMs of the .kg) in the SDRAM, the Z80 RAM in the SRAM.
 Video: criss_video (text, syncs and de always), the OSD of link v2 over it, HDMI. Syncs negative.
 Audio: the bell (BEL) as a 2 kHz tone on the PCM5102 and HDMI.

 Soft switches (the index of the item in the osd of core.json):
   0 Disk A (F item, slot 0)   1 Disk B (F item, slot 1)   2 Reset
*/

module karabas_minig_top (
	input wire CLK_50MHZ,

	inout wire UART_RX,
	inout wire UART_TX,
	inout wire UART_CTS,

	output wire [20:0] MA,
	inout wire [15:0] MD,
	output wire [1:0] MWR_N,
	output wire [1:0] MRD_N,

	output wire [1:0] SDR_BA,
	output wire [12:0] SDR_A,
	output wire SDR_CLK,
	output wire [1:0] SDR_DQM,
	output wire SDR_WE_N,
	output wire SDR_CAS_N,
	output wire SDR_RAS_N,
	inout wire [15:0] SDR_DQ,

	output wire SD_CS_N,
	output wire SD_CLK,
	output wire SD_DI,
	input wire SD_DO,
	input wire SD_DET_N,

	input wire [7:0] VGA_R,
	input wire [7:0] VGA_G,
	input wire [7:0] VGA_B,
	input wire VGA_HS,
	input wire VGA_VS,

	output wire [3:0] TMDS_P,
	output wire [3:0] TMDS_N,

	output wire FT_SPI_CS_N,
	output wire FT_SPI_SCK,
	input wire FT_SPI_MISO,
	output wire FT_SPI_MOSI,
	input wire FT_INT_N,
	input wire FT_CLK,
	input wire FT_DE,
	output wire FT_CLK_OUT,

	output wire [2:0] WA,
	output wire [1:0] WCS_N,
	output wire WRD_N,
	output wire WWR_N,
	output wire WRESET_N,
	inout wire [15:0] WD,

	output wire TAPE_OUT,
	input wire TAPE_IN,

	output wire DAC_BCK,
	output wire DAC_WS,
	output wire DAC_DAT,

	output wire ADC_CLK,
	inout wire ADC_BCK,
	inout wire ADC_LRCK,
	input wire ADC_DOUT,

	input wire MCU_CS_N,
	input wire MCU_SCK,
	inout wire MCU_MOSI,
	output wire MCU_MISO,
	inout wire [5:0] MCU_IO, // [0] burst cs_n, [1] burst ready, [2] sd2 cs_n, [3] ft cs_n

	output wire MIDI_TX,

	output wire ESP32_SPI_CS_N,
	input wire ESP32_PCM_BCK,
	input wire ESP32_PCM_RLCK,
	input wire ESP32_PCM_DAT,

	output wire FLASH_CS_N,
	input wire FLASH_DO,
	output wire FLASH_DI,
	output wire FLASH_SCK,
	output wire FLASH_WP_N,
	output wire FLASH_HOLD_N
);

// ------------------------------------------------------------------ unused

assign WA = 3'b000;
assign WCS_N = 2'b11;
assign WRD_N = 1'b1;
assign WWR_N = 1'b1;
assign WRESET_N = 1'b1;
assign WD = 16'bz;

assign ESP32_SPI_CS_N = 1'b1;
assign UART_RX = 1'bz;
assign UART_TX = 1'bz;
assign UART_CTS = 1'bz;

assign TAPE_OUT = 1'b0;

// FLASH_CS_N (T3) is the SDRAM /CS of Mini G2 (rev G: tied low on the board): always selected
assign FLASH_CS_N = 1'b0;
assign FLASH_DI = 1'b1;
assign FLASH_SCK = 1'b0;
assign FLASH_WP_N = 1'b1;
assign FLASH_HOLD_N = 1'b1;

// ------------------------------------------------------------------ clocks

wire [5:0] pll_clk;
wire pll_locked;
kg_pll #(
	.CLKIN_PERIOD(20.0),
	.CLKFBOUT_MULT(15),      // VCO 750 MHz
	.CLKOUT0_DIVIDE(10),     // clk_sys 75 MHz
	.CLKOUT1_DIVIDE(10),     // clk_sdr 75 MHz, 225 degrees
	.CLKOUT1_PHASE(225.0),
	.CLKOUT2_DIVIDE(30),     // clk_pix 25 MHz
	.CLKOUT3_DIVIDE(6),      // clk_hdmi 125 MHz
	.CLKOUT4_DIVIDE(6),      // clk_hdmi_n 125 MHz, 180 degrees
	.CLKOUT4_PHASE(180.0),
	.CLKOUT5_DIVIDE(90)      // clk_8 8.33 MHz
) pll (
	.clk_in(CLK_50MHZ),
	.rst(1'b0),
	.clk_out(pll_clk),
	.locked(pll_locked)
);
wire clk_sys = pll_clk[0];
wire clk_sdr = pll_clk[1];
wire clk_pix = pll_clk[2];
wire clk_hdmi = pll_clk[3];
wire clk_hdmi_n = pll_clk[4];
wire clk_8 = pll_clk[5];

(* KEEP = "TRUE" *) wire areset_async = ~pll_locked;
wire areset, areset_pix;
kg_sync #(.W(1), .STAGES(2), .INIT(1'b1)) sync_areset (.clk(clk_sys), .d(areset_async), .q(areset));
kg_sync #(.W(1), .STAGES(2), .INIT(1'b1)) sync_areset_pix (.clk(clk_pix), .d(areset_async), .q(areset_pix));

ODDR2 oddr_ft_clk (.Q(FT_CLK_OUT), .C0(clk_8), .C1(~clk_8), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));
ODDR2 oddr_sdr_clk (.Q(SDR_CLK), .C0(clk_sdr), .C1(~clk_sdr), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

// ------------------------------------------------------------------ MCU

wire mcu_sck_ibufg, mcu_sck;
IBUFG ibufg_mcu_sck (.I(MCU_SCK), .O(mcu_sck_ibufg));
BUFG bufg_mcu_sck (.I(mcu_sck_ibufg), .O(mcu_sck));

wire mcu_int;
assign MCU_IO[1] = mcu_int;
assign MCU_IO[0] = 1'bz;
assign MCU_IO[5:2] = 4'bzzzz;

wire [7:0] ps2_code;
wire ps2_kind, ps2_wr;
wire [1:0] ld_kind;
wire [7:0] ld_slot, ld_data;
wire [31:0] ld_addr;
wire ld_start, ld_active, ld_end, ld_wr, ld_ready;
wire [8*3-1:0] sw;
wire [7:0] sdc_img_mounted, sdc_rd, sdc_wr, sdc_byte_in_data, sdc_byte_out_data;
wire [63:0] sdc_img_size;
wire [31:0] sdc_sector;
wire sdc_busy, sdc_done, sdc_error, sdc_byte_in_strobe;
wire [2:0] sdc_slot;
wire [8:0] sdc_byte_addr;
wire dvi_only;
wire ft_cs_n, ft_mosi, ft_spi_on, sd2_cs_n, sd2_mosi;
wire [15:0] dbg_addr;
reg [15:0] dbg_data = 16'd0;
wire osd_wr, osd_en, osd_zoom;
wire [11:0] osd_waddr;
wire [7:0] osd_wdata;
wire [23:0] osd_fg, osd_bg;
wire [1:0] osd_alpha, osd_place;
wire [4:0] osd_px, osd_ph;
wire [3:0] osd_py;
wire [5:0] osd_pw;

kg_mcu2 #(
	.SW_N(3),
	.CS_FILTER(3),
	.CAPS(16'h0010),     // bit 4: the core drives LD_READY
	.ENABLE_OSD(1),
	.ENABLE_IMG(1),
	.IMG_OUT_LATENCY(3), // the address register of kg_l2_img + criss_sd (two registers), as core-doom
	.ENABLE_AUX(1),      // DEBUG_*: the state of the core (AT+KARABAS=debug:1, kg_board.py log)
	.ENABLE_FT(1),
	.ENABLE_SD2(1)
) mcu (
	.CLK(clk_sys),
	.N_RESET(~areset),

	.MCU_SCK(mcu_sck),
	.MCU_MOSI(MCU_MOSI),
	.MCU_MISO(MCU_MISO),
	.MCU_SS(MCU_CS_N),
	.MCU_INT(mcu_int),
	.MCU_SPI_FT_SS(MCU_IO[3]),
	.MCU_SPI_SD2_SS(MCU_IO[2]),

	.SW(sw), .SW_WR(), .SW_INDEX(), .SW_VALUE(), .HW_ID(), .DVI_ONLY(dvi_only), .CORE_BUILD(), .FW_BUILD(),
	.BUSY(), .BTNS(), .RELOAD_REQ(1'b0), .RELOAD_CORE(8'd0),

	.KBD(), .KBD_WR(),
	.PS2_CODE(ps2_code), .PS2_KIND(ps2_kind), .PS2_WR(ps2_wr),
	.MS_X(), .MS_Y(), .MS_Z(), .MS_B(), .MS_WR(),
	.JOY_L(), .JOY_R(), .JOY_WR(),
	.PS2_CMD(16'h0000), .PS2_CMD_WR(1'b0),

	.LD_KIND(ld_kind), .LD_SLOT(ld_slot), .LD_SIZE(), .LD_EXT(), .LD_START(ld_start), .LD_ACTIVE(ld_active), .LD_END(ld_end),
	.LD_ADDR(ld_addr), .LD_DATA(ld_data), .LD_WR(ld_wr), .LD_READY(ld_ready),

	.OSD_WR(osd_wr), .OSD_WADDR(osd_waddr), .OSD_WDATA(osd_wdata), .OSD_EN(osd_en),
	.OSD_FG(osd_fg), .OSD_BG(osd_bg), .OSD_ALPHA(osd_alpha), .OSD_PX(osd_px), .OSD_PY(osd_py),
	.OSD_PW(osd_pw), .OSD_PH(osd_ph), .OSD_PLACE(osd_place), .OSD_ZOOM(osd_zoom),

	.SDC_IMG_MOUNTED(sdc_img_mounted), .SDC_IMG_SIZE(sdc_img_size), .SDC_RD(sdc_rd), .SDC_WR(sdc_wr),
	.SDC_SECTOR(sdc_sector), .SDC_BUSY(sdc_busy), .SDC_DONE(sdc_done), .SDC_ERROR(sdc_error), .SDC_SLOT(sdc_slot),
	.SDC_BYTE_IN_STROBE(sdc_byte_in_strobe), .SDC_BYTE_ADDR(sdc_byte_addr), .SDC_BYTE_IN_DATA(sdc_byte_in_data),
	.SDC_BYTE_OUT_DATA(sdc_byte_out_data), .IMG_WP(), .IMG_PHYS_WR(), .IMG_PHYS(), .IMG_FLUSH(), .FDD_CHNG_WR(), .FDD_CHNG(),
	.BK_MOUNT(1'b0), .BK_FLUSHED(1'b0), .BK_SLOT(3'd0), .BK_SIZE(32'd0), .BK_KIND(8'd0), .BK_RESULT(8'd0),
	.FDD_CHNG_REQ(1'b0), .FDD_CHNG_DS(2'd0), .IMG_ACTIVE(),
	.PORT_OUT_DATA(), .PORT_OUT_WR(), .PORT_OUT_READY(4'b1111), .PORT_IN_DATA(32'd0), .PORT_IN_WR(4'd0),
	.PORT_CFG(128'd0), .PORT_IN_FULL(), .PORT_STAT(), .PORT_STAT_WR(),
	.RTC_A(8'd0), .RTC_DI(8'd0), .RTC_CS(1'b0), .RTC_WR_N(1'b1), .MATRIX_CMD(24'd0), .MATRIX_CMD_WR(1'b0),
	.DEBUG_ADDR(dbg_addr), .DEBUG_DATA(dbg_data), .RTC_DO(),

	.FT_SPI_ON(ft_spi_on), .FT_VGA_ON(), .FT_RESET(), .FT_SCK(), .FT_MOSI(ft_mosi), .FT_MISO(FT_SPI_MISO),
	.FT_CS_N(ft_cs_n),
	.SD2_SCK(), .SD2_MOSI(sd2_mosi), .SD2_MISO(SD_DO), .SD2_CS_N(sd2_cs_n)
);

assign FT_SPI_CS_N = ft_spi_on ? ft_cs_n : 1'b1;
assign FT_SPI_SCK = ft_spi_on ? mcu_sck_ibufg : 1'b1;
assign FT_SPI_MOSI = ft_spi_on ? ft_mosi : 1'b1;
assign SD_CS_N = sd2_cs_n;
assign SD_DI = sd2_mosi;
assign SD_CLK = mcu_sck_ibufg;

wire sw_reset = sw[2*8];

// ------------------------------------------------------------------ the SoC

wire [15:0] sdr_dq_o, sdr_dq_oe_n, md_o, md_oe_n;
genvar gi;
generate
	for (gi = 0; gi < 16; gi = gi + 1) begin : g_io
		assign SDR_DQ[gi] = sdr_dq_oe_n[gi] ? 1'bz : sdr_dq_o[gi];
		assign MD[gi] = md_oe_n[gi] ? 1'bz : md_o[gi];
	end
endgenerate

wire scr_we, font_we, col_we, beep, cpu_run;
wire [10:0] scr_wa, col_wa;
wire [2:0] col_wd;
wire [14:0] font_wa;
wire [7:0] scr_wd, font_wd, vstat, dbg_uart, dbg_state, sd_dbg;
wire [15:0] cursor, dbg_z80_pc;
wire sd_we;
wire [13:0] sd_off;
wire [31:0] sd_wd, sd_rd;

criss_soc soc (
	.clk(clk_sys),
	.reset(areset | sw_reset),
	.ld_kind(ld_kind), .ld_active(ld_active), .ld_addr(ld_addr), .ld_data(ld_data), .ld_wr(ld_wr), .ld_ready(ld_ready),
	.ps2_code(ps2_code), .ps2_wr(ps2_wr & ~ps2_kind),
	.sd_we(sd_we), .sd_off(sd_off), .sd_wd(sd_wd), .sd_rd(sd_rd),
	.scr_we(scr_we), .scr_wa(scr_wa), .scr_wd(scr_wd), .font_we(font_we), .font_wa(font_wa), .font_wd(font_wd),
	.col_we(col_we), .col_wa(col_wa), .col_wd(col_wd),
	.vstat(vstat), .cursor(cursor), .beep(beep),
	.dbg_uart(dbg_uart), .cpu_run(cpu_run), .dbg_z80_pc(dbg_z80_pc), .dbg_state(dbg_state),
	.sdr_a(SDR_A), .sdr_ba(SDR_BA), .sdr_dq_i(SDR_DQ), .sdr_dq_o(sdr_dq_o), .sdr_dq_oe_n(sdr_dq_oe_n),
	.sdr_dqm(SDR_DQM), .sdr_ras_n(SDR_RAS_N), .sdr_cas_n(SDR_CAS_N), .sdr_we_n(SDR_WE_N),
	.MA(MA), .MD_I(MD), .MD_O(md_o), .MD_OE_N(md_oe_n), .MWR_N(MWR_N), .MRD_N(MRD_N)
);

// ------------------------------------------------------------------ floppy images (slots 0..3)

criss_sd sd (
	.clk(clk_sys),
	.reset(areset),
	.io_we(sd_we), .io_off(sd_off), .io_wd(sd_wd), .io_rd(sd_rd),
	.sdc_img_mounted(sdc_img_mounted), .sdc_img_size(sdc_img_size), .sdc_rd(sdc_rd), .sdc_wr(sdc_wr),
	.sdc_sector(sdc_sector), .sdc_busy(sdc_busy), .sdc_done(sdc_done), .sdc_error(sdc_error), .sdc_slot(sdc_slot),
	.sdc_byte_in_strobe(sdc_byte_in_strobe), .sdc_byte_addr(sdc_byte_addr), .sdc_byte_in_data(sdc_byte_in_data),
	.sdc_byte_out_data(sdc_byte_out_data), .dbg(sd_dbg)
);

// ------------------------------------------------------------------ video

wire [23:0] vid_rgb;
wire vid_hs, vid_vs, vid_de;
criss_video video (
	.clk_sys(clk_sys),
	.scr_we(scr_we), .scr_wa(scr_wa), .scr_wd(scr_wd),
	.font_we(font_we), .font_wa(font_wa), .font_wd(font_wd),
	.col_we(col_we), .col_wa(col_wa), .col_wd(col_wd),
	.vstat_i(vstat), .cursor_i(cursor),
	.clk_pix(clk_pix), .rgb(vid_rgb), .hs(vid_hs), .vs(vid_vs), .de(vid_de)
);

wire [23:0] osd_rgb;
wire osd_hs, osd_vs, osd_de;
kg_l2_osd_overlay osd (
	.wclk(clk_sys),
	.osd_wr(osd_wr), .osd_waddr(osd_waddr), .osd_wdata(osd_wdata), .osd_en(osd_en),
	.osd_fg(osd_fg), .osd_bg(osd_bg), .osd_alpha(osd_alpha), .osd_px(osd_px), .osd_py(osd_py),
	.osd_pw(osd_pw), .osd_ph(osd_ph), .osd_place(osd_place), .osd_zoom(osd_zoom),
	.icon_on(8'd0), .icon_act(8'd0),
	.clk(clk_pix),
	.rgb_i(vid_rgb), .hs_i(vid_hs), .vs_i(vid_vs), .de_i(vid_de),
	.rgb_o(osd_rgb), .hs_o(osd_hs), .vs_o(osd_vs), .de_o(osd_de)
);

// ------------------------------------------------------------------ audio: the bell, 2 kHz

reg [1:0] beep_s = 2'd0;
reg [12:0] tone_cnt = 13'd0;
reg tone = 1'b0;
always @(posedge clk_pix) begin
	beep_s <= { beep_s[0], beep };
	if (tone_cnt == 13'd6249) begin tone_cnt <= 13'd0; tone <= ~tone; end
	else tone_cnt <= tone_cnt + 13'd1;
end
wire signed [15:0] snd_l = beep_s[1] ? (tone ? 16'sd6000 : -16'sd6000) : 16'sd0;
wire signed [15:0] snd_r = snd_l;

kg_pcm5102 #(.DIV_BITS(2)) dac (  // bck 3.1 MHz
	.clk(clk_pix),
	.reset(areset_pix),
	.left(snd_l),
	.right(snd_r),
	.din(DAC_DAT),
	.bck(DAC_BCK),
	.lrck(DAC_WS)
);
assign ADC_CLK = 1'b0;
assign ADC_BCK = 1'bz;
assign ADC_LRCK = 1'bz;
assign MIDI_TX = 1'b1;

// 48 kHz for hdmi_tx on clk_pix: + 6 per clock of 25 MHz, modulo 3125 (exact)
reg [11:0] aud_acc = 12'd0;
reg aud_stb = 1'b0;
reg [23:0] hdmi_l = 24'd0, hdmi_r = 24'd0;
always @(posedge clk_pix) begin
	aud_stb <= 1'b0;
	if (aud_acc >= 12'd3119) begin
		aud_acc <= aud_acc + 12'd6 - 12'd3125;
		aud_stb <= 1'b1;
		hdmi_l <= { snd_l, 8'd0 };
		hdmi_r <= { snd_r, 8'd0 };
	end else
		aud_acc <= aud_acc + 12'd6;
end

// ------------------------------------------------------------------ HDMI

reg dvi_only_p = 1'b0;
always @(posedge clk_pix)
	dvi_only_p <= dvi_only;

// negative syncs (640 x 480 at 60 Hz)
reg hdmi_hs = 1'b1, hdmi_vs = 1'b1, hdmi_de = 1'b0;
reg [23:0] hdmi_rgb = 24'd0;
always @(posedge clk_pix) begin
	hdmi_hs <= ~osd_hs;
	hdmi_vs <= ~osd_vs;
	hdmi_de <= osd_de;
	hdmi_rgb <= osd_rgb;
end

hdmi_tx #(.SAMPLE_FREQ(48000)) hdmi (
	.clk(clk_pix),
	.sclk(clk_hdmi),
	.sclk_n(clk_hdmi_n),
	.reset(areset_pix),
	.rgb(hdmi_rgb),
	.vsync(hdmi_vs),
	.hsync(hdmi_hs),
	.de(hdmi_de),
	.audio_en(~dvi_only_p),
	.audio_l(hdmi_l),
	.audio_r(hdmi_r),
	.audio_clk(aud_stb),
	.tx_clk_n(TMDS_N[3]),
	.tx_clk_p(TMDS_P[3]),
	.tx_d_n(TMDS_N[2:0]),
	.tx_d_p(TMDS_P[2:0])
);

// ------------------------------------------------------------------ debug

// DEBUG_ADDR: the Z80 PC (REG); DEBUG_DATA: { the SoC state, the state of criss_sd }
assign dbg_addr = dbg_z80_pc;
always @(posedge clk_sys)
	dbg_data <= { dbg_state, sd_dbg };

endmodule
