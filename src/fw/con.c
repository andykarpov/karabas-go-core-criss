// Console output of CRISS (platform 0 "CRISS") as firmware 4.67 does it (docs/re/cpu-io.md 12):
// the screen is Z80 RAM F800 + (y-1) * CHARS + (x-1), control codes, ESC sequences, windows, status
// line, pseudographics 160 x 96, the colour module CLR. The visible behaviour follows 4.67, also where
// it is odd (user decision 2026-11-20); only writes outside the screen are not done.
#include "con.h"
#include "hal.h"
#include "clr.h"
#include "io.h"
#include "nvram.h"
#include "rom.h"
#include <string.h>

#define SCR 0xF800u

// system variables of the firmware (docs/re/cpu-io.md 4.1, 12.8)
static uint8_t lines, chars, crt;      // CRT_SETTINGS: bits 2..0 code page, 4 colour schemes, 7 no scroll
static uint8_t x, y;                   // cursor, from 1, physical row
static uint8_t cur_hide;               // CURSOR_STATUS 0x80
static uint16_t vend, top;             // end / top of the scroll area (offsets from F800)
static uint8_t stat;                   // status line: 0 none, 1 top, else its row (bottom)
static uint8_t wx1, wy1, wx2, wy2;     // window (wx1 = 0: none)
static uint8_t sv_x = 1, sv_y = 1, sv_hide;   // ESC B 6 / ESC A

static uint8_t sleeping;
static uint8_t platform;               // HW_FLAG: 0 CRISS .. 6 VT100
static uint8_t k_inv;                  // Kaypro: inversion (ESC B 0 / ESC C 0)
static uint8_t save_buf[2000];         // ESC B S / C S
static uint8_t esc[24];                // the bytes after ESC
static uint8_t esc_n, in_esc;
uint8_t con_fifo[32];
uint8_t con_fifo_n;

static uint8_t cp(void) { return crt & 7; }
static uint8_t fill(void) { return cp() == 1 ? 0x00 : 0x20; }
static uint16_t off_xy(void) { return (uint16_t)((y - 1) * chars + (x - 1)); }

static int raw_vstat = -1;             // port D0 written by a program (until the next mode change)
static int raw_cursor = -1;            // ports D1 / D2 (until the next output)

static void update(void)
{
	uint8_t vstat = sleeping ? 0 : (uint8_t)((lines + (stat ? 1 : 0)) << 3 | cp());
	if (raw_vstat >= 0) vstat = (uint8_t)raw_vstat;
	uint16_t cur = cur_hide ? 0xFFFF : off_xy();
	if (raw_cursor >= 0) cur = (uint16_t)raw_cursor;
	hal_video(vstat, cur);
}

void con_raw_vstat(uint8_t v) { raw_vstat = v; update(); }

void con_raw_cursor(uint8_t hi, uint8_t lo)
{
	uint16_t a = (uint16_t)((hi & 0x07) << 8 | lo);
	raw_cursor = ((hi & 0x80) || a < 0x100 || a >= 0x8D0) ? 0xFFFF : a - 0x100;
	update();
}

// system variables kept by the console (docs/re/cpu-io.md 4.1, 12.8): -1 = not ours
int con_sysvar(uint8_t o)
{
	uint16_t vc = (uint16_t)(off_xy() + 0x100), ve = (uint16_t)(vend + 0x100), tp = (uint16_t)(top + 0x100);
	switch (o) {
	case 0: return platform;
	case 5: return crt;
	case 6: return x;
	case 7: return y;
	case 8: return cur_hide;
	case 9: return lines;
	case 10: return chars;
	case 11: return ve & 0xFF;
	case 12: return ve >> 8;
	case 13: return vc & 0xFF;
	case 14: return vc >> 8;
	case 0x66: return wx1;
	case 0x67: return wy1;
	case 0x68: return wx2;
	case 0x69: return wy2;
	case 0x6C: return tp & 0xFF;
	case 0x6D: return tp >> 8;
	case 0x6E: return stat;
	default: return -1;
	}
}

int con_sysvar_set(uint8_t o, uint8_t v)
{
	switch (o) {
	case 5: crt = v; break;
	case 6: if (v >= 1 && v <= chars) x = v; break;
	case 7: if (v >= 1 && v <= 31) y = v; break;
	case 8: cur_hide = v & 0x80; break;
	default: return 0;
	}
	update();
	return 1;
}

static void clr(uint8_t b) { clr_cmd(b); }
static int clr24(void) { return lines == 24; }

static void clear_range(uint16_t from, uint16_t to)
{
	uint8_t f = fill();
	for (uint16_t i = from; i < to && i < 2048; i++) hal_wr(SCR + i, f);
}

static void scroll_up(void)
{
	for (uint16_t i = top; i + chars < vend; i++) hal_wr(SCR + i, hal_rd(SCR + i + chars));
	clear_range(vend - chars, vend);
	if (clr24() && (crt & 0x10)) clr(0x7B);
}

static void home(void) { x = y = 1; }

static void cls(void)
{
	clear_range(top, vend);
	if (clr24() && (crt & 0x10)) clr(0x7A);
	home();
}

static void lf(void)
{
	if (y < lines) y++;
	else if (crt & 0x80) x = 1;
	else scroll_up();
}

static void put(uint8_t c)
{
	if (y >= 1) hal_wr(SCR + off_xy(), c);
	if (x >= chars) { x = 1; lf(); }
	else x++;
}

// the mode change of the firmware (0xEC2C): VSTAT, cursor, VEND, TOP, status line, window, CLR
static void apply_mode(void)
{
	raw_vstat = -1;
	cur_hide = cp() == 1 ? 0x80 : 0;
	vend = (uint16_t)(lines * chars);
	top = 0;
	stat = 0;
	wx1 = wy1 = wx2 = wy2 = 0;
	clr(0x00);
	crt &= ~0x10;
}

static void set_mode(uint8_t v, uint8_t nchars)
{
	uint16_t old_top = top, old_vend = vend;
	lines = v >> 3;
	chars = nchars;
	crt = v & 7;
	home();
	clear_range(old_top, old_vend);        // by the old area (4.67: 24 -> 25 lines keeps line 25)
	apply_mode();
}

void con_init(void)
{
	lines = 24; chars = 80; crt = 0;
	sleeping = 0;
	in_esc = 0; esc_n = 0;
	con_fifo_n = 0;
	clr_init();
	home();
	apply_mode();
	update();
}

// ------------------------------------------------------------------ cursor positioning (0xF338)

static void pos(int r, int c)
{
	if (r > lines) return;
	if (stat == 1) r++;
	if (c > chars) return;
	if (r < 1) r = 1;                      // 4.67 takes 0 (a cell left / a row up): not done here
	if (c < 1) c = 1;
	y = (uint8_t)r;
	x = (uint8_t)c;
}

// ------------------------------------------------------------------ control codes

static void ctrl(uint8_t c)
{
	switch (c) {
	case 0x01: home(); break;
	case 0x07: hal_beep(); break;
	case 0x08: if (x >= 2) x--; break;
	case 0x09: do put(' '); while ((x - 1) & 7); break;
	case 0x0A: lf(); break;
	case 0x0C: cls(); break;
	case 0x0D: x = 1; break;
	case 0x0E: crt |= 0x80; break;
	case 0x0F: crt &= ~0x80; break;
	case 0x14: clear_range(off_xy(), vend); break;
	case 0x15: if (x < chars) x++; break;
	case 0x16: clear_range(off_xy(), (uint16_t)(y * chars)); break;
	case 0x18: x = 1; clear_range(off_xy(), (uint16_t)(y * chars)); break;
	case 0x1A: if (y >= 2) y--; break;
	case 0x1E: cur_hide = 0x80; break;
	case 0x1F: cur_hide = 0; break;
	default: break;
	}
}

// ------------------------------------------------------------------ lines, window

static void del_line(void)
{
	uint16_t from = (uint16_t)((y - 1) * chars);
	for (uint16_t i = from; i + chars < vend; i++) hal_wr(SCR + i, hal_rd(SCR + i + chars));
	clear_range(vend - chars, vend);
}

static void ins_line(void)
{
	uint16_t from = (uint16_t)((y - 1) * chars);
	for (int i = vend - 1; i >= from + chars; i--) hal_wr(SCR + i, hal_rd(SCR + i - chars));
	clear_range(from, from + chars);
}

static uint16_t wcell(int r, int c) { return (uint16_t)((wy1 - 1 + r) * chars + (wx1 - 1 + c)); }

static void window_cmd(uint8_t k)
{
	if (!wx1) return;
	int w = wx2 - wx1 + 1, h = wy2 - wy1 + 1;
	if (k == 'C') {
		for (int r = 0; r < h; r++) for (int c = 0; c < w; c++) hal_wr(SCR + wcell(r, c), 0x20);
	} else if (k == 'U') {
		for (int r = 0; r + 1 < h; r++) for (int c = 0; c < w; c++) hal_wr(SCR + wcell(r, c), hal_rd(SCR + wcell(r + 1, c)));
		for (int c = 0; c < w; c++) hal_wr(SCR + wcell(h - 1, c), 0x20);
	} else if (k == 'F' || k == 'f') {
		// corners top, horizontal, vertical, corners bottom (0x11F4A / 0x11F54)
		const uint8_t *t = (k == 'F') ? (const uint8_t *)"\xC9\xCD\xBB\xBA\x20\xBA\xC8\xCD\xBC"
		                              : (const uint8_t *)"\xDA\xC4\xBF\xB3\x20\xB3\xC0\xC4\xD9";
		for (int r = 0; r < h; r++) {
			const uint8_t *s = t + (r == 0 ? 0 : r == h - 1 ? 6 : 3);
			for (int c = 0; c < w; c++)
				hal_wr(SCR + wcell(r, c), c == 0 ? s[0] : c == w - 1 ? s[2] : s[1]);
		}
	}
}

static void window_set(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	if (a >= 1 && a <= chars && b >= 1 && b <= lines && a <= c && c <= chars && b <= d && d <= lines) {
		wx1 = a; wy1 = b; wx2 = c; wy2 = d;
	} else
		wx1 = wy1 = wx2 = wy2 = 0;
}

// ------------------------------------------------------------------ pseudographics 160 x 96 (0xF612)

static void dot(int py, int px, int op)   // op 0 set, 1 clear, 2 invert
{
	if (py < 0 || px < 0 || py > 0x60 || px > 0xA0) return;
	uint16_t a = (uint16_t)(SCR + (py >> 2) * chars + (px >> 1));
	uint8_t bit = (uint8_t)(1u << (2 * (py & 3) + (px & 1)));
	uint8_t v = hal_rd(a);
	v = op == 0 ? (v | bit) : op == 1 ? (v & ~bit) : (v ^ bit);
	hal_wr(a, v);
}

static void line(int y1, int x1, int y2, int x2, int op)
{
	int dy = y2 - y1, dx = x2 - x1;
	int sy = dy < 0 ? -1 : 1, sx = dx < 0 ? -1 : 1;
	if (dy < 0) dy = -dy;
	if (dx < 0) dx = -dx;
	int n = dy > dx ? dy : dx, ay = 0, ax = 0;
	for (int i = 0; i <= n; i++) {
		dot(y1, x1, op);
		ay += dy; ax += dx;
		if (n && ay >= n) { ay -= n; y1 += sy; }
		if (n && ax >= n) { ax -= n; x1 += sx; }
	}
}

static void circle(uint8_t s, int cy, int cx, int r)
{
	int a = 0, b = r, d = 3 - 2 * r;
	while (a < b) {
		if (s & 0x01) dot((cy + b) & 0xFF, (cx + a) & 0xFF, 0);
		if (s & 0x02) dot((cy - b) & 0xFF, (cx + a) & 0xFF, 0);
		if (s & 0x04) dot((cy + b) & 0xFF, (cx - a) & 0xFF, 0);
		if (s & 0x08) dot((cy - b) & 0xFF, (cx - a) & 0xFF, 0);
		if (s & 0x10) dot((cy + a) & 0xFF, (cx + b) & 0xFF, 0);
		if (s & 0x20) dot((cy - a) & 0xFF, (cx + b) & 0xFF, 0);
		if (s & 0x40) dot((cy + a) & 0xFF, (cx - b) & 0xFF, 0);
		if (s & 0x80) dot((cy - a) & 0xFF, (cx - b) & 0xFF, 0);
		if (d < 0) d += 4 * a + 6;
		else { d += 4 * (a - b) + 6; b--; }
		a++;
	}
	if (b == 0) dot(cy & 0xFF, (cx + a) & 0xFF, 0);
}

// ------------------------------------------------------------------ ESC sequences (0x16BFE)

static void mode_g(uint8_t m)
{
	static const struct { uint8_t m, chars, vstat; } g[] = {
		{ '0', 80, 0xC0 }, { '1', 80, 0xC2 }, { '2', 80, 0xC3 }, { '3', 80, 0xC4 }, { '4', 80, 0xC1 },
		{ '5', 80, 0xC8 }, { 'S', 80, 0xC7 }, { '6', 64, 0xF0 }, { '7', 64, 0xF4 }, { '8', 64, 0xF8 },
		{ '9', 64, 0xFC },
	};
	if (m == 'N') {
		clear_range(top, vend);
		cur_hide = 0;
		home();
		lines = 24; chars = 80; crt = 0;
		apply_mode();
		return;
	}
	if (m == 's') { sleeping = 1; return; }
	if (m == 'w') { sleeping = 0; return; }
	for (unsigned i = 0; i < sizeof g / sizeof g[0]; i++)
		if (g[i].m == m) { set_mode(g[i].vstat, g[i].chars); return; }
}

static void clr_r(uint8_t k)
{
	static const char cols[] = "HRGYBMCW";
	if (k == 'L') { cls(); return; }
	if (!clr24()) return;
	const char *p = strchr(cols, k);
	if (p && k) { clr((uint8_t)(0x01 + (p - cols))); crt &= ~0x10; }
	else if (k == 'P') { clr(0x7F); crt |= 0x10; }
	else if (k == 'E') { if (crt & 0x10) clr(0x7A); }
	else if (k == 'S') { if (crt & 0x10) clr(0x7B); }
	else { clr(0x00); crt &= ~0x10; }
}

static void clr_s(int n, uint8_t c)
{
	if (!clr24() || !(crt & 0x10)) return;
	int k = 0;
	for (;;) {
		int cx = x - 1 + k;
		if (cx >= 80) return;
		clr((uint8_t)(0x11 + cx));
		if (y - 1 < 24) clr((uint8_t)(0x61 + y - 1));
		if (n == 0) return;
		if (n <= 16) { clr((uint8_t)(0x80 | (n - 1) << 3 | (c & 7))); return; }
		clr((uint8_t)(0xF8 | (c & 7)));
		n -= 16;
		k += 16;
	}
}

// length of a sequence with ESC by its first byte (0x16BFE); 0: variable (ESC m)
static int esc_len(uint8_t c)
{
	if (c >= 0x80) return 3;
	switch (c) {
	case '0': case '1': case 'j': case 'H': case 'J': case 'K': case 'R': case 'E': return 2;
	case 'A': case 'a': case 'B': case 'C': case 0x09: case '^': case '_': case 'Z': case 'G': case '\\':
	case '|': case 'k': case 'w': case 'q': case 'r': return 3;
	case 'm': return 0;
	case '=': case 'Y': case 'y': case '*': case ' ': case '#': case 'S': case 'g': case 's': return 4;
	default: return 6;                     // c L D x W, and unknown (dropped at 6)
	}
}

static void esc_do(uint8_t z80_a)
{
	uint8_t c = esc[0], p1 = esc[1], p2 = esc[2], p3 = esc[3], p4 = esc[4];
	if (c >= 0x80) { pos(c - 0x7F, p1 - 0x7F); return; }
	switch (c) {
	case '0': cur_hide = 0x80; break;
	case '1': cur_hide = 0; break;
	case 'j': cls(); break;
	case 'H': home(); break;
	case 'J': clear_range(off_xy(), vend); break;
	case 'K': clear_range(off_xy(), (uint16_t)(y * chars)); break;
	case 'R': del_line(); break;
	case 'E': ins_line(); break;
	case 'A':
		sv_x = x; sv_y = y; sv_hide = cur_hide;
		cur_hide = 0x80;
		if (stat && p1 <= chars) { y = stat; x = p1 ? p1 : 1; }
		break;
	case 'a':
		if (stat) break;
		if (p1 == 'T') { lines--; top += chars; stat = 1; }
		else if (p1 == 'B') { stat = lines; lines--; vend -= chars; }
		break;
	case 'B':
		if ((platform == 3 || platform == 5) && p1 >= '0' && p1 <= '3') { if (p1 == '0') k_inv = 1; break; }
		if (p1 == '4') cur_hide = 0;
		else if (p1 == '6') { sv_x = x; sv_y = y; sv_hide = cur_hide; }
		else if (p1 == 'S') for (int i = 0; i < 2000; i++) save_buf[i] = hal_rd((uint16_t)(SCR + i));
		break;
	case 'C':
		if ((platform == 3 || platform == 5) && p1 >= '0' && p1 <= '3') { if (p1 == '0') k_inv = 0; break; }
		if (p1 == '4') cur_hide = 0x80;
		else if (p1 == '6') { x = sv_x; y = sv_y; cur_hide = sv_hide; }
		else if (p1 == 'S') for (int i = 0; i < 2000; i++) hal_wr((uint16_t)(SCR + i), save_buf[i]);
		break;
	case 0x09: if (p1 <= chars) x = p1 ? p1 : 1; break;
	case '^': case '_': case 'S': break;
	case 'Z': set_mode(p1, z80_a ? z80_a : 80); break;
	case 'G': mode_g(p1); break;
	case '\\': put(p1); break;
	case '|': hal_scr(off_xy(), p1); break;
	case 'k': if (con_fifo_n < sizeof con_fifo) con_fifo[con_fifo_n++] = p1; break;
	case 'w': window_cmd(p1); break;
	case 'q': if (clr24()) clr(p1); break;
	case 'r': clr_r(p1); break;
	case '=': pos(p1 - 31, p2 - 31); break;
	case 'Y': pos(p1 + 1, p2 + 1); break;
	case 'y':
		if (wx1 && p1 >= 1 && p1 <= wy2 - wy1 + 1 && p2 >= 1 && p2 <= wx2 - wx1 + 1)
			pos(wy1 + p1 - 1, wx1 + p2 - 1);
		break;
	case '*': dot(p1, p2, 0); break;
	case ' ': dot(p1, p2, 1); break;
	case '#': dot(p1, p2, 2); break;
	case 'g': break;                       // GPIO: Ф3
	case 's': clr_s(p1, p2); break;
	case 'c': circle(p1, p2, p3, p4); break;
	case 'L': line(p1, p2, p3, p4, 0); break;
	case 'D': line(p1, p2, p3, p4, 1); break;
	case 'x': line(p1, p2, p3, p4, 2); break;
	case 'W': window_set(p1, p2, p3, p4); break;
	default: break;
	}
}

// ------------------------------------------------------------------ platforms (docs/re/cpu-io.md 3, 12.3, 12.4)
// 0 CRISS, 1 Robotron 1715 RU, 2 Robotron 1715 EN, 3 Kaypro, 4 TRS-80, 5 Kaypro 8 bit, 6 VT100 / ANSI / VT52

static uint8_t r_attr, r_s1;           // Robotron: attribute of the current field, s1 (0x0928 bit 2)
static uint8_t vt_save_x = 1, vt_save_y = 1;
static uint8_t i8275, i8275_col;       // OUT 18 / 19 state

uint8_t con_platform(void) { return platform; }

void con_set_platform(uint8_t p)
{
	const uint8_t *rec = hal_rom() + ROM_PLATFORMS + 24 * (p > 6 ? 0 : p);
	platform = p > 6 ? 0 : p;
	r_attr = r_s1 = 0;
	k_inv = 0;
	clear_range(top, vend);
	cur_hide = 0;
	home();
	lines = rec[16]; chars = rec[17]; crt = rec[18] & 0xC7;
	apply_mode();
	update();
}

// a printable character (>= 0x20) of the platform: the byte to print, or -1 (nothing)
static int plat_char(uint8_t c)
{
	switch (platform) {
	case 1: case 2: {                  // Robotron (0x16F42)
		if (c < 0x80) {
			uint8_t a = r_attr ^ r_s1;
			if (a & 0x04) { if (c == 0x40) c = 0x1F; else if (c >= 0x41 && c <= 0x5F) c -= 0x40; }
			if (a & 0x10) c |= 0x80;
			return c;
		}
		switch (c) {
		case 0x82: cur_hide = 0; return -1;
		case 0x83: cur_hide = 0x80; return -1;
		case 0x84: r_attr = 0x40; return 0x3A;
		case 0x85: r_attr = 0x50; return 0x3A;
		case 0x86: r_attr = 0x41; return 0x3A;
		case 0x87: r_attr = 0x51; return 0x3A;
		case 0x90: r_s1 = 0; return -1;
		case 0x91: r_s1 = 4; return -1;
		case 0xFF: return 0x7F;
		default: return 0x20;
		}
	}
	case 3: c &= 0x7F; return k_inv ? (c | 0x80) : c;   // Kaypro (0x1719A)
	case 4: return c & 0x7F;                            // TRS-80
	case 6: return c;                                   // VT100
	default: return c == 0xFF ? 0x7F : c;               // CRISS, Kaypro 8 bit
	}
}

// control codes of Kaypro (0x171A8); 1 = done
static int kaypro_ctrl(uint8_t c)
{
	switch (c) {
	case 0x0C: if (x < chars) x++; return 1;
	case 0x0B: if (y >= 2) y--; return 1;
	case 0x17: clear_range(off_xy(), vend); return 1;
	case 0x18: clear_range(off_xy(), (uint16_t)(y * chars)); return 1;
	case 0x1A: cls(); return 1;
	case 0x1E: home(); return 1;
	default: return 0;
	}
}

// Robotron: NumLock (code 0x1E) switches s0 / s1 (CONIN, 0x16D80)
void con_toggle_s1(void) { r_s1 ^= 4; }

// Robotron ESC: always 3 bytes (0x16FE4 RU, 0x1716A EN)
static void rob_esc(void)
{
	uint8_t a = esc[0], b = esc[1];
	if (platform == 2 && a == '_' && b >= 0x40 && b <= 0x6B) { r_attr = (uint8_t)(((b - 0x40) >> 2) + 0x10); put(0x3A); return; }
	if (a == '^') { r_attr = b; put(0x3A); return; }
	if (a == '_') { put('+'); return; }
	if (a == 'n') return;
	pos(a - 0x7F, b - 0x7F);
}

// VT100 / ANSI / VT52 (0x1729A, 0x172FE, 0x1742A)
static uint8_t vt_par[4], vt_np, vt_csi, vt_y;
static void vt_ctrl(uint8_t c)
{
	switch (c) {
	case 0x0A: case 0x0B: case 0x0C: lf(); break;
	case 0x0D: x = 1; break;
	case 0x07: hal_beep(); break;
	case 0x09: case 0x0E: case 0x0F: break;
	default: put(hal_rom()[ROM_COMPAT_VT + c]); break;
	}
}

// a byte of a VT sequence: 1 = the sequence is complete
static int vt_esc(uint8_t c)
{
	if (c == 0x18) return 1;                       // CAN cancels it
	if (vt_csi) {
		if (c >= '0' && c <= '9') { vt_par[vt_np] = (uint8_t)(vt_par[vt_np] * 10 + (c - '0')); return 0; }
		if (c == ';') { if (vt_np < 3) vt_np++; return 0; }
		int n = vt_par[0] ? vt_par[0] : 1;
		switch (c) {
		case 'A': y = (y > n) ? (uint8_t)(y - n) : 1; break;
		case 'B': y = (y + n < 24) ? (uint8_t)(y + n) : 24; break;
		case 'C': x = (x + n < 80) ? (uint8_t)(x + n) : 80; break;
		case 'D': x = (x > n) ? (uint8_t)(x - n) : 1; break;
		case 'H': case 'f':
			if (vt_par[0] < 24 && vt_par[1] < 80) { y = vt_par[0] ? vt_par[0] : 1; x = vt_par[1] ? vt_par[1] : 1; }
			break;
		case 'K':
			if (vt_par[0] == 0) clear_range(off_xy(), (uint16_t)(y * chars));
			else if (vt_par[0] == 1) clear_range((uint16_t)((y - 1) * chars), off_xy() + 1);
			else clear_range((uint16_t)((y - 1) * chars), (uint16_t)(y * chars));
			break;
		case 'J':
			if (vt_par[0] == 0) clear_range(off_xy(), vend);
			else if (vt_par[0] == 1) clear_range(top, off_xy() + 1);
			else clear_range(top, vend);
			break;
		default: break;                            // m, q, h, l: ignored
		}
		return 1;
	}
	if (esc_n == 1) {
		switch (c) {
		case '[': vt_csi = 1; vt_np = 0; memset(vt_par, 0, sizeof vt_par); return 0;
		case 'Y': return 0;
		case 'A': if (y >= 2) y--; return 1;
		case 'B': x = 1; return 1;                 // 4.67: CR instead of down (kept)
		case 'C': if (x < chars) x++; return 1;
		case 'D': if (x >= 2) x--; return 1;
		case 'H': home(); return 1;
		case 'J': clear_range(off_xy(), vend); return 1;
		case 'K': clear_range(off_xy(), (uint16_t)(y * chars)); return 1;
		case '7': case 's': vt_save_x = x; vt_save_y = y; return 1;
		case '8': case 'u': x = vt_save_x; y = vt_save_y; return 1;
		default: return 1;
		}
	}
	// ESC Y r c
	if (esc_n == 2) { vt_y = c; return 0; }
	pos(vt_y - 31, c - 31);
	return 1;
}

// OUT 18 / 19: the 8275 of Robotron (0x17096)
void con_8275(uint8_t port, uint8_t v)
{
	if (port == 0x19) { i8275 = v == 0x80 ? 1 : v == 0x00 ? 5 : 0; return; }
	if (port != 0x18) return;
	switch (i8275) {
	case 1: i8275_col = v; i8275 = 2; break;
	case 2: y = (uint8_t)(v + 1); x = (uint8_t)(i8275_col + 1); i8275 = 0; update(); break;
	case 5: i8275 = 6; break;
	case 6: lines = (uint8_t)(v - 0x3F); if (lines < 24 || lines > 25) lines = 24; i8275 = 0; apply_mode(); update(); break;
	default: break;
	}
}

// ESC m n1 .. nk 04 cmd: up to 16 notes to the peripheral MCU (E0..EF), then the command (D6)

static uint8_t m_notes[16], m_n, m_end;  // ESC m being collected

void con_out(uint8_t c, uint8_t z80_a)
{
	raw_cursor = -1;
	if (in_esc && platform == 6) {
		esc_n++;
		if (vt_esc(c)) in_esc = 0;
	} else if (in_esc && (platform == 1 || platform == 2)) {
		if (esc_n < sizeof esc) esc[esc_n] = c;
		if (++esc_n == 2) { in_esc = 0; rob_esc(); }
	} else if (in_esc) {
		if (esc_n == 0 && c == 'm') { m_n = 0; m_end = 0; }
		if (esc_n < sizeof esc) esc[esc_n] = c;
		esc_n++;
		if (esc[0] == 'm') {
			if (esc_n == 1) ;                         // the 'm' itself
			else if (m_end) { con_melody(m_notes, m_n, c); in_esc = 0; }
			else if (c == 0x04) m_end = 1;
			else if (m_n < sizeof m_notes) m_notes[m_n++] = c;   // more than 16: ignored up to 04
		} else if (esc_n + 1 >= esc_len(esc[0])) {
			in_esc = 0;
			if (esc_len(esc[0]) != 6 || strchr("cLDxW", esc[0])) esc_do(z80_a);
		}
	} else if (c == 0x1B) {
		in_esc = 1;
		esc_n = 0;
		vt_csi = 0;
	} else if (c < 0x20) {
		if (platform == 6) vt_ctrl(c);
		else if ((platform == 3 || platform == 5) && kaypro_ctrl(c)) ;
		else ctrl(c);
	} else {
		int v = plat_char(c);
		if (v >= 0) put((uint8_t)v);
	}
	update();
}

// the console state for a RAM snapshot (nvram.c)
void con_state_save(uint8_t *b)
{
	uint8_t v[] = { lines, chars, crt, x, y, cur_hide, (uint8_t)vend, (uint8_t)(vend >> 8), (uint8_t)top,
		(uint8_t)(top >> 8), stat, wx1, wy1, wx2, wy2, sv_x, sv_y, sv_hide };
	memset(b, 0, CON_STATE_SIZE);
	memcpy(b, v, sizeof v);
}

void con_state_load(const uint8_t *b)
{
	lines = b[0]; chars = b[1]; crt = b[2]; x = b[3]; y = b[4]; cur_hide = b[5];
	vend = (uint16_t)(b[6] | b[7] << 8); top = (uint16_t)(b[8] | b[9] << 8); stat = b[10];
	wx1 = b[11]; wy1 = b[12]; wx2 = b[13]; wy2 = b[14]; sv_x = b[15]; sv_y = b[16]; sv_hide = b[17];
	if (!chars) chars = 80;
	if (lines < 23 || lines > 31) lines = 24;
	in_esc = 0;
	raw_vstat = raw_cursor = -1;
	clr_cmd(0x00);
	update();
}
