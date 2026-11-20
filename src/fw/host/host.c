// The CRISS helper firmware on a PC (docs/PLAN.md Ф1 step 2): the hal on a Z80 interpreter
// (superzazu/z80, MIT) with the same traps as criss_z80.v, floppy images as files, keys typed as
// PS/2 codes when the Z80 waits in CONIN, the text screen printed at the end.
//   criss_host --rom criss_data.bin [--disk A:img.cpm] [--keys "DIR\r"] [--steps N] [--save] [--dump-mem f]
// (--dump-mem: 64 KB of Z80 RAM + VSTAT + cursor offset, for sim/video)
#include "../hal.h"
#include "../rom.h"
#include "z80/z80.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static z80 cpu;
static uint8_t mem[65536];
static uint8_t *rom;
static int reset_hold = 1, freeze_on;
static unsigned long steps, max_steps = 50000000;
static int conin_polls;
static uint8_t video_vstat = 0xC0;
static uint16_t video_cursor;
static int save_disks;
static const char *dump_mem;      // write the 64 KB of the Z80 at the end (sim/video test)

static struct { char *name; uint8_t *data; uint32_t size; } disk[4];

// keys: PS/2 codes queued one key (make + break) at a time
static uint8_t keyq[8192];
static int keyq_n, keyq_rd;
static int key_avail;           // bytes of the current key that may be read

static uint8_t rd(void *u, uint16_t a) { (void)u; return mem[a]; }
static void wr(void *u, uint16_t a, uint8_t v) { (void)u; mem[a] = v; }
static uint8_t pin(z80 *z, uint8_t p) { (void)z; (void)p; return 0xFF; }
static void pout(z80 *z, uint8_t p, uint8_t v) { (void)z; (void)p; (void)v; }

static int is_trap(uint8_t b) { return b <= 0x27 || b == 0x77 || b == 0x7F || (b >= 0xA4 && b <= 0xA7); }

void hal_z80_reset(int hold)
{
	if (hold && !reset_hold) { }
	if (!hold && reset_hold) {
		z80_init(&cpu);
		cpu.read_byte = rd; cpu.write_byte = wr; cpu.port_in = pin; cpu.port_out = pout;
		cpu.pc = 0;
	}
	reset_hold = hold;
}
void hal_freeze(int on) { freeze_on = on; }

int hal_wait_trap(void)
{
	for (;;) {
		if (freeze_on) return TRAP_FREEZE;
		if (steps >= max_steps) return HAL_STOP;
		uint16_t pc = cpu.pc;
		if (mem[pc] == 0xED && is_trap(mem[(uint16_t)(pc + 1)])) {
			uint8_t code = mem[(uint16_t)(pc + 1)];
			cpu.pc = pc + 2;
			steps++;
			if (code == 0x03) conin_polls++; else conin_polls = 0;
			return code;
		}
		z80_step(&cpu);
		steps++;
		if ((steps & 1023) == 0 && criss_idle()) return -1;
	}
}

void hal_get_regs(z80regs_t *r)
{
	r->a = cpu.a; r->f = get_f(&cpu); r->a2 = cpu.a_; r->f2 = cpu.f_;
	r->i = cpu.i; r->r = cpu.r; r->sp = cpu.sp; r->pc = cpu.pc;
	r->bc = get_bc(&cpu); r->de = get_de(&cpu); r->hl = get_hl(&cpu);
	r->ix = cpu.ix; r->iy = cpu.iy;
	r->bc2 = cpu.b_ << 8 | cpu.c_; r->de2 = cpu.d_ << 8 | cpu.e_; r->hl2 = cpu.h_ << 8 | cpu.l_;
	r->im = cpu.interrupt_mode; r->iff1 = cpu.iff1; r->iff2 = cpu.iff2;
}

void hal_set_regs(const z80regs_t *r)
{
	cpu.a = r->a; set_f(&cpu, r->f); cpu.a_ = r->a2; cpu.f_ = r->f2;
	cpu.i = r->i; cpu.r = r->r; cpu.sp = r->sp; cpu.pc = r->pc;
	set_bc(&cpu, r->bc); set_de(&cpu, r->de); set_hl(&cpu, r->hl);
	cpu.ix = r->ix; cpu.iy = r->iy;
	cpu.b_ = r->bc2 >> 8; cpu.c_ = r->bc2; cpu.d_ = r->de2 >> 8; cpu.e_ = r->de2;
	cpu.h_ = r->hl2 >> 8; cpu.l_ = r->hl2;
	cpu.interrupt_mode = r->im; cpu.iff1 = r->iff1; cpu.iff2 = r->iff2;
}

void hal_resume(void) { }
uint8_t hal_rd(uint16_t a) { return mem[a]; }
void hal_wr(uint16_t a, uint8_t v) { mem[a] = v; }
const uint8_t *hal_rom(void) { return rom; }
void hal_video(uint8_t vstat, uint16_t cursor) { video_vstat = vstat; video_cursor = cursor; }
void hal_fonts(const uint8_t *f, uint32_t len) { (void)f; (void)len; }
static uint8_t vscr[2048], vcol[2048];      // the video copies (vscr: ESC | writes only)
void hal_scr(uint16_t off, uint8_t c) { vscr[off & 2047] = c; }
void hal_col(uint16_t off, uint8_t colour) { vcol[off & 2047] = colour & 7; }
uint32_t hal_ms(void) { return (uint32_t)(steps / 1000); }
void hal_dbg(const char *s) { fputs(s, stderr); }
void hal_beep(void) { }

int hal_ps2(void)
{
	if (!key_avail && keyq_rd < keyq_n && conin_polls > 20) {
		// one key: up to and including its break code (F0 xx) and a following Shift release
		int i = keyq_rd;
		while (i < keyq_n && keyq[i] != 0xF0) i++;
		i += 2;
		if (i < keyq_n && keyq[i] == 0xF0 && keyq[i + 1] == 0x12) i += 2;
		key_avail = i - keyq_rd;
		conin_polls = 0;
	}
	if (!key_avail) return -1;
	key_avail--;
	return keyq[keyq_rd++];
}

uint32_t hal_disk_size(int slot) { return slot < 4 && disk[slot].data ? disk[slot].size : 0; }
int hal_disk_read(int slot, uint32_t lba, uint8_t *buf)
{
	if (!hal_disk_size(slot) || (lba + 1) * 512 > disk[slot].size) return 1;
	memcpy(buf, disk[slot].data + lba * 512, 512);
	return 0;
}
int hal_disk_write(int slot, uint32_t lba, const uint8_t *buf)
{
	if (!hal_disk_size(slot) || (lba + 1) * 512 > disk[slot].size) return 1;
	memcpy(disk[slot].data + lba * 512, buf, 512);
	return 0;
}

static uint8_t *load(const char *fn, uint32_t *size)
{
	FILE *f = fopen(fn, "rb");
	if (!f) { perror(fn); exit(2); }
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *p = malloc(n);
	if (fread(p, 1, n, f) != (size_t)n) { perror(fn); exit(2); }
	fclose(f);
	*size = (uint32_t)n;
	return p;
}

// ASCII -> PS/2 set 2 through the keyboard table of the ROM (page 0 plain, page 1 Shift)
static void type(const char *s)
{
	for (; *s; s++) {
		uint8_t ch = (uint8_t)*s;
		int found = 0;
		for (int sh = 0; sh < 2 && !found; sh++)
			for (int sc = 1; sc < 0x80 && !found; sc++)
				if (rom[ROM_KBD_MAIN + sh * 256 + sc] == ch) {
					if (sh) keyq[keyq_n++] = 0x12;
					keyq[keyq_n++] = (uint8_t)sc;
					keyq[keyq_n++] = 0xF0; keyq[keyq_n++] = (uint8_t)sc;
					if (sh) { keyq[keyq_n++] = 0xF0; keyq[keyq_n++] = 0x12; }
					found = 1;
				}
		if (!found) fprintf(stderr, "no key for %02X\n", ch);
	}
}

static void dump(void)
{
	int lines = video_vstat >> 3, cp = video_vstat & 7;
	int chars = lines >= 30 ? 64 : 80;
	if (lines < 24) lines = 24;
	printf("---- screen %dx%d cp %d, cursor %04X, %lu Z80 instructions\n", chars, lines, cp, video_cursor, steps);
	for (int y = 0; y < lines; y++) {
		char line[128];
		int n = 0;
		for (int x = 0; x < chars; x++) {
			uint8_t c = mem[0xF800 + y * chars + x];
			line[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : (c == 0 ? ' ' : '.');
		}
		while (n && line[n - 1] == ' ') n--;
		line[n] = 0;
		printf("|%s\n", line);
	}
}

int main(int argc, char **argv)
{
	const char *keys = "";
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--rom") && i + 1 < argc) { uint32_t n; rom = load(argv[++i], &n); }
		else if (!strcmp(argv[i], "--disk") && i + 1 < argc) {
			char *a = argv[++i];
			int d = a[0] - 'A';
			if (d < 0 || d > 3 || a[1] != ':') { fprintf(stderr, "--disk A:file\n"); return 2; }
			disk[d].name = a + 2;
			disk[d].data = load(a + 2, &disk[d].size);
		}
		else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys = argv[++i];
		else if (!strcmp(argv[i], "--steps") && i + 1 < argc) max_steps = strtoul(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--save")) save_disks = 1;
		else if (!strcmp(argv[i], "--dump-mem") && i + 1 < argc) dump_mem = argv[++i];
		else { fprintf(stderr, "usage: %s --rom f [--disk A:f] [--keys s] [--steps n] [--save]\n", argv[0]); return 2; }
	}
	if (!rom) { fprintf(stderr, "--rom needed\n"); return 2; }
	// \r in --keys: Enter
	char buf[4096];
	int n = 0;
	for (const char *k = keys; *k && n < (int)sizeof buf - 1; k++) {
		if (k[0] == '\\' && k[1] == 'r') { buf[n++] = '\r'; k++; }
		else buf[n++] = *k;
	}
	buf[n] = 0;
	type(buf);
	criss_main();      // returns at the step limit (hal_wait_trap: HAL_STOP)
	dump();
	if (dump_mem) {
		FILE *f = fopen(dump_mem, "wb");
		fwrite(mem, 1, sizeof mem, f);
		fprintf(f, "%c%c%c", video_vstat, video_cursor & 0xFF, video_cursor >> 8);
		fwrite(vcol, 1, sizeof vcol, f);
		fclose(f);
	}
	if (save_disks)
		for (int d = 0; d < 4; d++)
			if (disk[d].data) {
				FILE *f = fopen(disk[d].name, "wb");
				fwrite(disk[d].data, 1, disk[d].size, f);
				fclose(f);
			}
	return 0;
}
