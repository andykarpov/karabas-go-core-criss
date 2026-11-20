// The colour module CLR of CRISS 6.x (ATtiny84, firmware 0.5; docs/re/periph.md 5) as a model: its
// command bytes, a colour buffer of 24 x 80 cells, written to the colour RAM of the video. Static mode
// = one colour for the whole screen (all 2048 cells of the video RAM), dynamic = per cell (24 x 80, the
// geometry of the 24-line mode).
#include "clr.h"
#include "hal.h"

static uint8_t buf[24][80];
static uint8_t def_colour = 2;       // EEPROM 0x1FF of the module: 2 = green
static uint8_t colour;               // r5: the colour of the static mode and of clears
static uint8_t col, row;
static uint8_t dynamic;

static void show_all(void)
{
	if (!dynamic) {
		for (uint16_t i = 0; i < 2048; i++) hal_col(i, colour);
		return;
	}
	for (int r = 0; r < 24; r++)
		for (int c = 0; c < 80; c++) hal_col((uint16_t)(r * 80 + c), buf[r][c]);
	for (uint16_t i = 1920; i < 2048; i++) hal_col(i, colour);
}

static void fill(uint8_t c)
{
	for (int r = 0; r < 24; r++)
		for (int k = 0; k < 80; k++) buf[r][k] = c;
}

static void show_row(int r)
{
	if (dynamic)
		for (int c = 0; c < 80; c++) hal_col((uint16_t)(r * 80 + c), buf[r][c]);
}

void clr_init(void)
{
	colour = def_colour;
	dynamic = 0;
	col = row = 0;
	fill(colour);
	show_all();
}

void clr_cmd(uint8_t b)
{
	if (b == 0x00) { dynamic = 0; colour = def_colour; fill(colour); show_all(); }
	else if (b <= 0x08) { dynamic = 0; colour = b - 1; fill(colour); show_all(); }
	else if (b <= 0x10) def_colour = b - 9;
	else if (b <= 0x60) col = b - 0x11;
	else if (b <= 0x78) row = b - 0x61;
	else if (b == 0x79) { for (int c = 0; c < 80; c++) buf[row][c] = colour; show_row(row); }
	else if (b == 0x7A) { fill(colour); if (dynamic) show_all(); }
	else if (b == 0x7B) {
		for (int r = 0; r < 23; r++)
			for (int c = 0; c < 80; c++) buf[r][c] = buf[r + 1][c];
		for (int c = 0; c < 80; c++) buf[23][c] = colour;
		if (dynamic) show_all();
	}
	else if (b == 0x7C) { dynamic = 0; show_all(); }
	else if (b == 0x7E || b == 0x7F) { dynamic = 1; fill(colour); show_all(); }   // 7E: EEPROM picture (none)
	else if (b >= 0x80) {
		int n = ((b >> 3) & 15) + 1;
		for (int c = col; c < col + n && c < 80; c++) buf[row][c] = b & 7;
		show_row(row);
	}
}
