// The CRISS helper: boot of the CP/M image and the BIOS traps of the CRISS firmware 4.67
// (docs/re/cpu-core.md 3, 4, 5; docs/PLAN.md Ф1). The Z80 stops on ED 00..27, 77, 7F, A4..A7.
#include "hal.h"
#include "rom.h"
#include "con.h"
#include "kbd.h"
#include <string.h>

// BIOS state (system variables of the firmware, docs/re/cpu-core.md 8)
static uint8_t bios_disk, bios_sector, bios_last, bdos_last;
static uint16_t bios_track, bios_dma;

// one 512-byte sector cache, write-through (firmware 4.67 4.2)
static uint8_t sec[512];
static int sec_slot = -1;
static uint32_t sec_lba;

#define SPT 80   // 128-byte CP/M sectors per track of a 819200-byte image

static void boot(void)
{
	const uint8_t *rom = hal_rom();
	uint32_t len = rom[8] | rom[9] << 8 | rom[10] << 16 | (uint32_t)rom[11] << 24;
	hal_z80_reset(1);
	for (uint32_t a = 0; a < 0x10000; a++) hal_wr((uint16_t)a, 0x20);
	for (uint32_t a = 0; a < len && a < 0xF800; a++) hal_wr((uint16_t)a, rom[ROM_CPM + a]);
	hal_fonts(rom + ROM_FONTS, ROM_FONT_N * ROM_FONT_SIZE);
	// the key of the reset (F12 of the OSD, Ctrl+Alt+Shift+Esc) may still be queued: not for CP/M
	while (hal_ps2() >= 0) ;
	kbd_init();
	con_init();
	bios_disk = bios_sector = 0; bios_track = 0; bios_dma = 0x80;
	sec_slot = -1;
	hal_freeze(1);
	hal_z80_reset(0);
	int code;
	while ((code = hal_wait_trap()) != TRAP_FREEZE && code != HAL_STOP) hal_resume();   // (a trap of the old program: let it go)
	hal_freeze(0);
	z80regs_t r;
	hal_get_regs(&r);
	r.pc = 0x0100; r.sp = 0x0100;
	r.a = 0; r.f = 0; r.bc = 0; r.de = 0;
	hal_set_regs(&r);
	hal_resume();
}

// a key for CONIN / CONST: the ESC k queue first, then the keyboard (D5)
static int key_ready(void)
{
	kbd_poll();
	return con_fifo_n || kbd_d5;
}

static int key_get(z80regs_t *r)
{
	kbd_poll();
	if (con_fifo_n) {
		r->a = con_fifo[0];
		memmove(con_fifo, con_fifo + 1, --con_fifo_n);
		return 1;
	}
	if (!kbd_d5) return 0;
	uint8_t c = kbd_d5;
	kbd_d5 = 0;
	// CRISS platform: A = character, B = 0, C = D = code (TAB 80, BS 7F, Enter 9E by scan), E = E0
	uint8_t code = c;
	if (kbd_d4 == 0x0D) code = 0x80;
	else if (kbd_d4 == 0x66) code = 0x7F;
	else if (kbd_d4 == 0x5A) code = 0x9E;
	r->a = c;
	r->bc = code;
	r->de = (uint16_t)(code << 8 | 0xE0);
	return 1;
}

static int disk_io(int write)
{
	int slot = bios_disk;
	uint32_t size = hal_disk_size(slot);
	if (!size) return 1;
	uint32_t off = ((uint32_t)(bios_track & 0xFF) * SPT + bios_sector) * 128;
	if (off + 128 > size) return 1;
	uint32_t lba = off >> 9;
	if (sec_slot != slot || sec_lba != lba) {
		if (hal_disk_read(slot, lba, sec)) { sec_slot = -1; return 1; }
		sec_slot = slot; sec_lba = lba;
	}
	uint8_t *p = sec + (off & 511);
	if (write) {
		for (int i = 0; i < 128; i++) p[i] = hal_rd((uint16_t)(bios_dma + i));
		if (hal_disk_write(slot, lba, sec)) { sec_slot = -1; return 1; }
	} else {
		for (int i = 0; i < 128; i++) hal_wr((uint16_t)(bios_dma + i), p[i]);
	}
	return 0;
}

static float fl_rd(uint16_t a)
{
	uint32_t v = 0;
	for (int i = 3; i >= 0; i--) v = v << 8 | hal_rd((uint16_t)(a + i));
	float f;
	memcpy(&f, &v, 4);
	return f;
}

static void fl_wr(uint16_t a, float f)
{
	uint32_t v;
	memcpy(&v, &f, 4);
	for (int i = 0; i < 4; i++) hal_wr((uint16_t)(a + i), (uint8_t)(v >> (8 * i)));
}

static void trap(int code)
{
	z80regs_t r;
	hal_get_regs(&r);
	uint8_t c = r.bc & 0xFF;
	if (code <= 0x26) bios_last = (uint8_t)code;
	switch (code) {
	case 0x00: case 0x01: case 0x08: break;               // BOOT, WBOOT, HOME: nothing
	case 0x02: r.a = key_ready() ? 0xFF : 0x00; break;    // CONST
	case 0x03:                                            // CONIN: no key -> run the trap again
		if (!key_get(&r)) r.pc -= 2;
		break;
	case 0x04: con_out(c, r.a); break;                    // CONOUT
	case 0x05: case 0x06: break;                          // LIST, PUNCH: Ф3
	case 0x07: r.a = 0; break;                            // READER: no data
	case 0x09: bios_disk = c; r.a = c; r.hl = (uint16_t)(0xDEA0 + 16 * c); break;   // SELDSK
	case 0x0A: bios_track = r.bc; break;                  // SETTRK
	case 0x0B: bios_sector = c; r.a = c; break;           // SETSEC
	case 0x0C: bios_dma = r.bc; break;                    // SETDMA
	case 0x0D: r.a = disk_io(0) ? 1 : 0; break;           // READ
	case 0x0E: r.a = disk_io(1) ? 1 : 0; break;           // WRITE
	case 0x0F: r.a = 0xFF; break;                         // LISTST
	case 0x10: r.hl = r.bc; break;                        // SECTRAN
	case 0x27:                                            // BDOS shortcut
		bdos_last = c;
		if (c == 11) r.a = key_ready() ? 1 : 0;
		else if (c == 6) {
			if ((r.de & 0xFF) == 0xFF) { if (!key_get(&r)) r.a = 0; }
			else con_out(r.de & 0xFF, r.a);
		} else r.pc = 0xCABF;
		break;
	case 0x77: {                                          // MUL: A:HL = HL * A
		uint32_t p = (uint32_t)r.hl * r.a;
		r.hl = (uint16_t)p; r.a = (uint8_t)(p >> 16);
		break;
	}
	case 0x7F:                                            // DIV: HL = HL / A, A = remainder
		if (r.a == 0) r.hl = 0xFFFF;
		else { uint16_t q = r.hl / r.a; r.a = (uint8_t)(r.hl % r.a); r.hl = q; }
		break;
	case 0xA4: fl_wr(r.hl, fl_rd(r.hl) + fl_rd(r.bc)); break;   // FL32: (HL) op (BC) -> (HL)
	case 0xA5: fl_wr(r.hl, fl_rd(r.hl) - fl_rd(r.bc)); break;
	case 0xA6: fl_wr(r.hl, fl_rd(r.hl) * fl_rd(r.bc)); break;
	case 0xA7: fl_wr(r.hl, fl_rd(r.hl) / fl_rd(r.bc)); break;
	default: break;                                       // BIOS 17..38: nothing (as 4.67)
	}
	hal_set_regs(&r);
}

int criss_idle(void)
{
	kbd_poll();
	return kbd_reset_req;
}

void criss_main(void)
{
	boot();
	for (;;) {
		int code = hal_wait_trap();
		if (code == HAL_STOP) return;
		if (code < 0 || kbd_reset_req) { boot(); continue; }
		if (code != TRAP_FREEZE) trap(code);
		hal_resume();
	}
}
