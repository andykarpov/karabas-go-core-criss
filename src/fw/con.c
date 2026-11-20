// Console output of CRISS (platform 0 "CRISS") as firmware 4.67 does it (docs/re/cpu-io.md 12):
// the screen is Z80 RAM F800 + (y-1) * CHARS + (x-1), control codes, ESC sequences, windows, status
// line, pseudographics 160 x 96, the colour module CLR. The visible behaviour follows 4.67, also where
// it is odd (user decision 2026-11-20); only writes outside the screen are not done.
#include "con.h"
#include "hal.h"
#include "clr.h"
#include "io.h"
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
		if (p1 == '4') cur_hide = 0;
		else if (p1 == '6') { sv_x = x; sv_y = y; sv_hide = cur_hide; }
		else if (p1 == 'S') for (int i = 0; i < 2000; i++) save_buf[i] = hal_rd((uint16_t)(SCR + i));
		break;
	case 'C':
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

// ESC m n1 .. nk 04 cmd: up to 16 notes to the peripheral MCU (E0..EF), then the command (D6)
void con_melody(const uint8_t *notes, int n, uint8_t cmd) __attribute__((weak));
void con_melody(const uint8_t *notes, int n, uint8_t cmd) { (void)notes; (void)n; (void)cmd; }

static uint8_t m_notes[16], m_n, m_end;  // ESC m being collected

void con_out(uint8_t c, uint8_t z80_a)
{
	raw_cursor = -1;
	if (in_esc) {
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
	} else if (c < 0x20) {
		ctrl(c);
	} else {
		put(c == 0xFF ? 0x7F : c);
	}
	update();
}
