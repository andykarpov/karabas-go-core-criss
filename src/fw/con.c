// Console output of CRISS (platform 0 "CRISS"): the screen is Z80 RAM F800 + (y-1) * CHARS + (x-1),
// the control codes and ESC sequences of firmware 4.67 (docs/re/cpu-io.md 12). Ф1: the text part;
// windows, status line, graphics, colour, melodies, other platforms come in Ф2 / Ф3.
#include "con.h"
#include "hal.h"
#include "kbd.h"

#define SCR 0xF800u

static uint8_t lines, chars, cp, x, y;
static uint8_t cur_off;          // cursor off (ESC 0, 1E, ESC C 4)
static uint8_t noscroll;         // ^N: no scroll at the bottom (CRT_SETTINGS bit 7)
static uint8_t esc[9], esc_n;    // ESC bytes collected (esc[0] = first parameter), esc_n = count
static uint8_t in_esc;
static uint8_t save_x = 1, save_y = 1;
uint8_t con_fifo[32];
uint8_t con_fifo_n;

static uint16_t vend(void) { return (uint16_t)(lines * chars); }
static uint8_t fill(void) { return cp == 1 ? 0x00 : 0x20; }

static void update(void)
{
	uint8_t vstat = (uint8_t)((lines << 3) | cp);
	uint16_t off = (uint16_t)((y - 1) * chars + (x - 1));
	hal_video(vstat, (cur_off || cp == 1) ? 0xFFFF : off);
}

static void clear_range(uint16_t from, uint16_t to)
{
	uint8_t f = fill();
	for (uint16_t i = from; i < to; i++) hal_wr(SCR + i, f);
}

static void scroll_up(void)
{
	uint16_t n = vend() - chars;
	for (uint16_t i = 0; i < n; i++) hal_wr(SCR + i, hal_rd(SCR + i + chars));
	clear_range(n, vend());
}

static void cls(void)
{
	clear_range(0, vend());
	x = y = 1;
}

static void lf(void)
{
	if (y < lines) y++;
	else if (noscroll) x = 1;
	else scroll_up();
}

static void crlf(void) { x = 1; lf(); }

static void put(uint8_t c)
{
	hal_wr(SCR + (y - 1) * chars + (x - 1), c);
	if (x >= chars) crlf();
	else x++;
}

static void set_mode(uint8_t vstat, uint8_t nchars)
{
	uint16_t old = vend();
	lines = vstat >> 3;
	if (lines < 24 || lines > 31) lines = 24;
	cp = vstat & 7;
	chars = nchars ? nchars : 80;
	x = y = 1;
	// cleared by the old end (firmware 4.67: 24 -> 25 lines leaves line 25 as it was)
	clear_range(0, old);
}

void con_init(void)
{
	lines = 24; chars = 80; cp = 0;
	x = y = 1;
	cur_off = 0; noscroll = 0; in_esc = 0; esc_n = 0;
	con_fifo_n = 0;
	update();
}

static void ctrl(uint8_t c)
{
	switch (c) {
	case 0x01: x = y = 1; break;
	case 0x07: hal_beep(); break;
	case 0x08: if (x >= 2) x--; break;
	case 0x09: do put(' '); while ((x - 1) & 7); break;
	case 0x0A: lf(); break;
	case 0x0C: cls(); break;
	case 0x0D: x = 1; break;
	case 0x0E: noscroll = 1; break;
	case 0x0F: noscroll = 0; break;
	case 0x14: clear_range((y - 1) * chars + (x - 1), vend()); break;
	case 0x15: if (x < chars) x++; break;
	case 0x16: clear_range((y - 1) * chars + (x - 1), y * chars); break;
	case 0x18: x = 1; clear_range((y - 1) * chars, y * chars); break;
	case 0x1A: if (y >= 2) y--; break;
	case 0x1E: cur_off = 1; break;
	case 0x1F: cur_off = 0; break;
	default: break;
	}
}

static void goto_yx(int r, int c)
{
	if (r >= 1 && r <= lines) y = (uint8_t)r;
	if (c >= 1 && c <= chars) x = (uint8_t)c;
}

static void del_line(void)
{
	uint16_t from = (y - 1) * chars, n = vend() - chars;
	for (uint16_t i = from; i < n; i++) hal_wr(SCR + i, hal_rd(SCR + i + chars));
	clear_range(n, vend());
}

static void ins_line(void)
{
	uint16_t from = (y - 1) * chars;
	for (int i = vend() - 1; i >= from + chars; i--) hal_wr(SCR + i, hal_rd(SCR + i - chars));
	clear_range(from, from + chars);
}

static void mode_g(uint8_t m)
{
	static const struct { uint8_t m, chars, vstat; } g[] = {
		{ '0', 80, 0xC0 }, { '1', 80, 0xC2 }, { '2', 80, 0xC3 }, { '3', 80, 0xC4 }, { '4', 80, 0xC1 },
		{ '5', 80, 0xC8 }, { 'S', 80, 0xC7 }, { '6', 64, 0xF0 }, { '7', 64, 0xF4 }, { '8', 64, 0xF8 },
		{ '9', 64, 0xFC },
	};
	if (m == 'N') { set_mode(0xC0, 80); cls(); cur_off = 0; noscroll = 0; return; }
	for (unsigned i = 0; i < sizeof g / sizeof g[0]; i++)
		if (g[i].m == m) { set_mode(g[i].vstat, g[i].chars); return; }
}

// a collected ESC sequence: returns 1 when complete (handled), 0 when more bytes are needed
static int esc_do(uint16_t z80_a)
{
	uint8_t c = esc[0];
	switch (c) {
	case '0': cur_off = 1; return 1;
	case '1': cur_off = 0; return 1;
	case 'j': cls(); return 1;
	case 'H': x = y = 1; return 1;
	case 'J': clear_range((y - 1) * chars + (x - 1), vend()); return 1;
	case 'K': clear_range((y - 1) * chars + (x - 1), y * chars); return 1;
	case 'R': del_line(); return 1;
	case 'E': ins_line(); return 1;
	case '^': case '_': if (esc_n < 2) return 0; return 1;
	case 0x09: if (esc_n < 2) return 0; if (esc[1] <= chars) x = esc[1] ? esc[1] : 1; return 1;
	case '=': if (esc_n < 3) return 0; goto_yx(esc[1] - 31, esc[2] - 31); return 1;
	case 'Y': if (esc_n < 3) return 0; goto_yx(esc[1] + 1, esc[2] + 1); return 1;
	case 'B':
		if (esc_n < 2) return 0;
		if (esc[1] == '4') cur_off = 0;
		else if (esc[1] == '6') { save_x = x; save_y = y; }
		return 1;
	case 'C':
		if (esc_n < 2) return 0;
		if (esc[1] == '4') cur_off = 1;
		else if (esc[1] == '6') { x = save_x; y = save_y; }
		return 1;
	case 'Z': if (esc_n < 2) return 0; set_mode(esc[1], (uint8_t)z80_a); return 1;
	case 'G': if (esc_n < 2) return 0; mode_g(esc[1]); return 1;
	case '\\': if (esc_n < 2) return 0; put(esc[1]); return 1;
	case 'k': if (esc_n < 2) return 0; if (con_fifo_n < sizeof con_fifo) con_fifo[con_fifo_n++] = esc[1]; return 1;
	default:
		if (c >= 0x80) { if (esc_n < 2) return 0; goto_yx(esc[0] - 0x7F, esc[1] - 0x7F); return 1; }
		// unknown: firmware 4.67 swallows 5 more bytes
		return esc_n >= 6;
	}
}

void con_out(uint8_t c, uint16_t z80_a)
{
	if (in_esc) {
		esc[esc_n++] = c;
		if (esc_do(z80_a) || esc_n >= 8) in_esc = 0;
	} else if (c == 0x1B) {
		in_esc = 1;
		esc_n = 0;
	} else if (c < 0x20) {
		ctrl(c);
	} else {
		put(c == 0xFF ? 0x7F : c);
	}
	update();
}
