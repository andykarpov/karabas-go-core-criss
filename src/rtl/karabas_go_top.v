`timescale 1ns / 1ps
`default_nettype wire

/*
 CRISS CP/M core for Karabas Go (VGA, PCM5102). Made by tools/mktops.py from karabas_minig_top.v
 (do not edit: change that one and run it): the same except the pins, the MCU link (MCU_SCK not
 on a clock pin) and the video: 24 bit RGB to the VGA DAC with its pixel clock on V_CLK (clk_pix),
 the FT812 off that bus (FT_OE_N = 1), no HDMI. The SAM2695 and the floppy connector are not used.
 criss_soc (T80 + the helper VexRiscv,
 docs/PLAN.md) with the board glue of karabas-go-modules; the board top of core-doom.

 Clocks: one PLL, VCO 750 MHz (50 * 15): clk_sys 75 MHz (the SoC, the MCU link), clk_sdr 75 MHz at 225
 degrees (SDR_CLK through an ODDR2), clk_pix 25 MHz (640 x 480 at 60 Hz, the OSD, HDMI), clk_hdmi /
 clk_hdmi_n 125 MHz (the serialiser), clk_8 8.33 MHz (FT_CLK_OUT).

 Memory: the helper program and the data ROM (the ROMs of the .kg) in the SDRAM, the Z80 RAM in the SRAM.
 Video: criss_video (text, syncs and de always), the OSD of link v2 over it, HDMI. Syncs negative.
 Audio: the bell (BEL) as a 2 kHz tone on the PCM5102 and HDMI.

 Soft switches (the index of the item in the osd of core.json):
   0 Disk A (F item, slot 0)   1 Disk B (F item, slot 1)   2 NVRAM (F item, slot 4: settings and the RAM
   snapshots, /criss/criss.nvr)   3 Reset   4 Platform (CRISS / Robotron RU / EN / Kaypro / TRS-80 / Kaypro 8 bit /
   VT100)   5 Boot (CP/M / snapshot 1 / snapshot 2); 4 and 5 are read by the helper (MMIO SW)
*/

module karabas_go_top (
	input wire CLK_50MHZ,

	inout wire UART_RX,
	inout wire UART_TX,
	inout wire UART_CTS,
	inout wire ESP_RESET_N,
	inout wire ESP_BOOT_N,

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

	output wire [7:0] VGA_R,
	output wire [7:0] VGA_G,
	output wire [7:0] VGA_B,
	output wire VGA_HS,
	output wire VGA_VS,
	output wire V_CLK,

	output wire FT_SPI_CS_N,
	output wire FT_SPI_SCK,
	input wire FT_SPI_MISO,
	output wire FT_SPI_MOSI,
	input wire FT_INT_N,
	input wire FT_CLK,
	output wire FT_OE_N,

	output wire [2:0] WA,
	output wire [1:0] WCS_N,
	output wire WRD_N,
	output wire WWR_N,
	output wire WRESET_N,
	inout wire [15:0] WD,

	input wire FDC_INDEX,
	output wire [1:0] FDC_DRIVE,
	output wire FDC_MOTOR,
	output wire FDC_DIR,
	output wire FDC_STEP,
	output wire FDC_WDATA,
	output wire FDC_WGATE,
	input wire FDC_TR00,
	input wire FDC_WPRT,
	input wire FDC_RDATA,
	output wire FDC_SIDE_N,

	output wire TAPE_OUT,
	input wire TAPE_IN,
	output wire BEEPER,

	output wire DAC_LRCK,
	output wire DAC_DAT,
	output wire DAC_BCK,
	output wire DAC_MUTE,

	input wire MCU_CS_N,
	input wire MCU_SCK,
	inout wire MCU_MOSI,
	output wire MCU_MISO,
	input wire MCU_SPI_FT_CS_N,
	input wire MCU_SPI_SD2_CS_N,
	inout wire [1:0] MCU_SPI_IO, // [0] INT of link v2 (firmware IO1, GPIO2), [1] unused (IO0, GPIO14)

	output wire MIDI_TX,
	output wire MIDI_CLK,
	output wire MIDI_RESET_N,

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

assign ESP_RESET_N = 1'bz;
assign ESP_BOOT_N = 1'bz;
// the ESP8266 (stock AT firmware, 115200 8N1, CTS flow control): criss_esp in criss_soc
wire esp_txd, esp_cts_n;
assign UART_RX = 1'bz;
assign UART_TX = esp_txd;
assign UART_CTS = esp_cts_n;

assign TAPE_OUT = 1'b0;
assign FT_OE_N = 1'b1;     // the FT812 does not drive the video DAC
assign BEEPER = 1'b0;
assign DAC_MUTE = 1'b1;    // soft mute of the PCM5102: 1 = sound on

// the floppy connector is not used: its outputs 0 (the board inverts them: high = active)
assign FDC_DRIVE = 2'b00;
assign FDC_MOTOR = 1'b0;
assign FDC_DIR = 1'b0;
assign FDC_STEP = 1'b0;
assign FDC_WDATA = 1'b0;
assign FDC_WGATE = 1'b0;
assign FDC_SIDE_N = 1'b0;

// the flash chip (the SDRAM /CS is hard wired on Go): not selected
assign FLASH_CS_N = 1'b1;
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

// the pixel clock of the VGA DAC (no FT_CLK_OUT on Go: the FT812 has its crystal)
ODDR2 oddr_v_clk (.Q(V_CLK), .C0(clk_pix), .C1(~clk_pix), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));
ODDR2 oddr_sdr_clk (.Q(SDR_CLK), .C0(clk_sdr), .C1(~clk_sdr), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

// the SAM2695 synth is not used: held in reset, no clock
assign MIDI_CLK = 1'b0;
assign MIDI_RESET_N = 1'b0;

// ------------------------------------------------------------------ MCU

// MCU_SCK is not on a clock pin before rev.G (CLOCK_DEDICATED_ROUTE = FALSE in the ucf)
wire mcu_sck = MCU_SCK, mcu_sck_ibufg = MCU_SCK;

wire mcu_int;
assign MCU_SPI_IO[0] = mcu_int;   // INT of link v2 on N9 (IO1 of the firmware)
assign MCU_SPI_IO[1] = 1'bz;

wire [7:0] ps2_code;
wire ps2_kind, ps2_wr;
wire [7:0] ms_x, ms_y, ms_b;
wire ms_wr;
// the USB serial port (port 0) <-> RS-232 of CRISS
wire [31:0] port_out_data;
wire [3:0] port_out_wr, port_in_full;
wire ser_rx_ready, ser_tx_wr;
wire [7:0] ser_tx_data;
// the RTC RAM of kg_mcu2 (rtc_mode 1: DS1307 registers, BCD)
wire [7:0] rtc_a, rtc_di, rtc_do;
wire rtc_cs, rtc_wr_n;
// the backup RAM file (slot 4)
wire [7:0] img_flush;
wire bk_mount, bk_flushed;
wire [31:0] bk_size;
wire [1:0] ld_kind;
wire [7:0] ld_slot, ld_data;
wire [31:0] ld_addr;
wire ld_start, ld_active, ld_end, ld_wr, ld_ready;
wire [8*6-1:0] sw;
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
	.SW_N(6),
	.CS_FILTER(3),
	.CAPS(16'h0010),     // bit 4: the core drives LD_READY (settings / snapshots: the NVRAM image, slot 4)
	.ENABLE_RTC(1),      // the DS1305 of CRISS on the clock of the board (rtc_mode 1)
	.ENABLE_OSD(1),
	.ENABLE_IMG(1),
	.IMG_OUT_LATENCY(3), // the address register of kg_l2_img + criss_sd (two registers), as core-doom
	.ENABLE_AUX(1),      // DEBUG_*: the state of the core (AT+KARABAS=debug:1, kg_board.py log)
	.ENABLE_FT(1),
	.ENABLE_SD2(1),
	.ENABLE_PORTS(4'b0001) // port 0: the USB serial port (RS-232 of CRISS)
) mcu (
	.CLK(clk_sys),
	.N_RESET(~areset),

	.MCU_SCK(mcu_sck),
	.MCU_MOSI(MCU_MOSI),
	.MCU_MISO(MCU_MISO),
	.MCU_SS(MCU_CS_N),
	.MCU_INT(mcu_int),
	.MCU_SPI_FT_SS(MCU_SPI_FT_CS_N),
	.MCU_SPI_SD2_SS(MCU_SPI_SD2_CS_N),

	.SW(sw), .SW_WR(), .SW_INDEX(), .SW_VALUE(), .HW_ID(), .DVI_ONLY(dvi_only), .CORE_BUILD(), .FW_BUILD(),
	.BUSY(), .BTNS(), .RELOAD_REQ(1'b0), .RELOAD_CORE(8'd0),

	.KBD(), .KBD_WR(),
	.PS2_CODE(ps2_code), .PS2_KIND(ps2_kind), .PS2_WR(ps2_wr),
	.MS_X(ms_x), .MS_Y(ms_y), .MS_Z(), .MS_B(ms_b), .MS_WR(ms_wr),
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
	.SDC_BYTE_OUT_DATA(sdc_byte_out_data), .IMG_WP(), .IMG_PHYS_WR(), .IMG_PHYS(), .IMG_FLUSH(img_flush), .FDD_CHNG_WR(), .FDD_CHNG(),
	.BK_MOUNT(1'b0), .BK_FLUSHED(1'b0), .BK_SLOT(3'd0), .BK_SIZE(32'd0), .BK_KIND(8'd0), .BK_RESULT(8'h00),
	.FDD_CHNG_REQ(1'b0), .FDD_CHNG_DS(2'd0), .IMG_ACTIVE(),
	.PORT_OUT_DATA(port_out_data), .PORT_OUT_WR(port_out_wr), .PORT_OUT_READY({ 3'b111, ser_rx_ready }),
	.PORT_IN_DATA({ 24'd0, ser_tx_data }), .PORT_IN_WR({ 3'b000, ser_tx_wr }),
	.PORT_CFG(128'd0), .PORT_IN_FULL(port_in_full), .PORT_STAT(), .PORT_STAT_WR(),
	.RTC_A(rtc_a), .RTC_DI(rtc_di), .RTC_CS(rtc_cs), .RTC_WR_N(rtc_wr_n), .MATRIX_CMD(24'd0), .MATRIX_CMD_WR(1'b0),
	.DEBUG_ADDR(dbg_addr), .DEBUG_DATA(dbg_data), .RTC_DO(rtc_do),

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

wire sw_reset = sw[3*8];

// ------------------------------------------------------------------ the SoC

wire [15:0] sdr_dq_o, sdr_dq_oe_n, md_o, md_oe_n;
genvar gi;
generate
	for (gi = 0; gi < 16; gi = gi + 1) begin : g_io
		assign SDR_DQ[gi] = sdr_dq_oe_n[gi] ? 1'bz : sdr_dq_o[gi];
		assign MD[gi] = md_oe_n[gi] ? 1'bz : md_o[gi];
	end
endgenerate

wire scr_we, font_we, col_we, beep, tone, cpu_run;
wire [10:0] scr_wa, col_wa;
wire [2:0] col_wd;
wire [14:0] font_wa;
wire [7:0] scr_wd, font_wd, vstat, dbg_uart, dbg_state, sd_dbg;
wire [15:0] cursor, dbg_z80_pc;
wire sd_we;
wire [13:0] sd_off;
wire [31:0] sd_wd, sd_rd;

criss_soc soc (
	.esp_rxd(UART_RX), .esp_txd(esp_txd), .esp_cts_n(esp_cts_n),
	.clk(clk_sys),
	.reset(areset | sw_reset),
	.ld_kind(ld_kind), .ld_active(ld_active), .ld_addr(ld_addr), .ld_data(ld_data), .ld_wr(ld_wr), .ld_ready(ld_ready),
	.ps2_code(ps2_code), .ps2_wr(ps2_wr & ~ps2_kind),
	.sd_we(sd_we), .sd_off(sd_off), .sd_wd(sd_wd), .sd_rd(sd_rd),
	.scr_we(scr_we), .scr_wa(scr_wa), .scr_wd(scr_wd), .font_we(font_we), .font_wa(font_wa), .font_wd(font_wd),
	.col_we(col_we), .col_wa(col_wa), .col_wd(col_wd),
	.vstat(vstat), .cursor(cursor), .beep(beep), .tone(tone),
	.ser_rx_data(port_out_data[7:0]), .ser_rx_wr(port_out_wr[0]), .ser_rx_ready(ser_rx_ready),
	.ser_tx_data(ser_tx_data), .ser_tx_wr(ser_tx_wr), .ser_tx_full(port_in_full[0]),
	.rtc_a(rtc_a), .rtc_do(rtc_do), .rtc_di(rtc_di), .rtc_cs(rtc_cs), .rtc_wr_n(rtc_wr_n),
	.ms_x(ms_x), .ms_y(ms_y), .ms_b(ms_b), .ms_wr(ms_wr),
	.sw({ 16'd0, sw }),
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
	.sdc_byte_out_data(sdc_byte_out_data), .img_flush(img_flush),
	.bk_mount(bk_mount), .bk_flushed(bk_flushed), .bk_size(bk_size), .dbg(sd_dbg)
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

reg [1:0] beep_s = 2'd0, mel_s = 2'd0;
reg [12:0] tone_cnt = 13'd0;
reg tone_2k = 1'b0;
always @(posedge clk_pix) begin
	beep_s <= { beep_s[0], beep };
	mel_s <= { mel_s[0], tone };
	if (tone_cnt == 13'd6249) begin tone_cnt <= 13'd0; tone_2k <= ~tone_2k; end
	else tone_cnt <= tone_cnt + 13'd1;
end
// the bell (2 kHz while BEL) and the melody tone of the peripheral MCU
wire signed [15:0] snd_bell = beep_s[1] ? (tone_2k ? 16'sd6000 : -16'sd6000) : 16'sd0;
wire signed [15:0] snd_mel = mel_s[1] ? 16'sd6000 : -16'sd6000;
reg mel_on = 1'b0;
reg [15:0] mel_idle = 16'd0;
always @(posedge clk_pix) begin
	// the melody square wave: silent when it stops toggling (no DC offset)
	if (mel_s[1] != mel_s[0]) begin mel_idle <= 16'd0; mel_on <= 1'b1; end
	else if (mel_idle == 16'hFFFF) mel_on <= 1'b0;
	else mel_idle <= mel_idle + 16'd1;
end
wire signed [15:0] snd_l = snd_bell + (mel_on ? snd_mel : 16'sd0);
wire signed [15:0] snd_r = snd_l;

kg_pcm5102 #(.DIV_BITS(2)) dac (  // bck 3.1 MHz
	.clk(clk_pix),
	.reset(areset_pix),
	.left(snd_l),
	.right(snd_r),
	.din(DAC_DAT),
	.bck(DAC_BCK),
	.lrck(DAC_LRCK)
);
assign MIDI_TX = 1'b1;

// ------------------------------------------------------------------ VGA

// 640 x 480 at 60 Hz: negative syncs as on HDMI, black outside de (the DAC has no blank input); the
// OSD output is registered with them
reg vga_hs = 1'b1, vga_vs = 1'b1;
reg [23:0] vga_rgb = 24'd0;
always @(posedge clk_pix) begin
	vga_hs <= ~osd_hs;
	vga_vs <= ~osd_vs;
	vga_rgb <= osd_de ? osd_rgb : 24'd0;
end
assign VGA_R = vga_rgb[23:16];
assign VGA_G = vga_rgb[15:8];
assign VGA_B = vga_rgb[7:0];
assign VGA_HS = vga_hs;
assign VGA_VS = vga_vs;

// ------------------------------------------------------------------ debug

// DEBUG_ADDR: the Z80 PC (REG); DEBUG_DATA: { the SoC state, the state of criss_sd }
assign dbg_addr = dbg_z80_pc;
always @(posedge clk_sys)
	dbg_data <= { dbg_state, sd_dbg };

endmodule
