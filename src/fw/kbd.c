// PS/2 keyboard as the peripheral MCU of CRISS does it (docs/re/periph.md 4.4, 4.5): set 2 codes
// to CP866 / function codes through the tables of its firmware, the registers D3 / D4 / D5.
#include "kbd.h"
#include "hal.h"
#include "rom.h"
#include "io.h"

// modifiers, the bits of D3: 0 Ctrl, 1 Shift, 2 RUS (ScrollLock), 3 CapsLock, 4 Alt
static uint8_t mods, e0, rel;
uint8_t kbd_d3, kbd_d4, kbd_d5;
int kbd_reset_req;

void kbd_init(void)
{
	mods = e0 = rel = 0;
	kbd_d3 = kbd_d4 = kbd_d5 = 0;
	kbd_reset_req = 0;
}

static void key(uint8_t b)
{
	const uint8_t *rom = hal_rom();
	if (b == 0xFA || b == 0xFE || b == 0xE1) return;
	if (b == 0xE0) { e0 = 1; return; }
	if (b == 0xF0) { rel = 1; return; }
	uint8_t was_e0 = e0, was_rel = rel;
	e0 = rel = 0;
	if (was_rel) {
		if (b == 0x12 || b == 0x59) mods &= ~0x02;
		else if (b == 0x14) mods &= ~0x01;
		else if (b == 0x11) mods &= ~0x10;
		return;
	}
	if (was_e0 && b >= 0x69 && b <= 0x7D) b = rom[ROM_KBD_E0 + b - 0x69];
	if (b == 0x83) b = 0x02;                  // F7
	switch (b) {
	case 0x12: case 0x59: mods |= 0x02; return;
	case 0x14: mods |= 0x01; return;
	case 0x11: mods |= 0x10; return;
	case 0x58: mods ^= 0x08; kbd_d3 = mods; return;
	case 0x7E: mods ^= 0x04; kbd_d3 = mods; return;
	default: break;
	}
	// Ctrl + Shift + Alt + Esc: reset of the machine (the peripheral MCU pulses the CPU reset)
	if (b == 0x76 && (mods & 0x13) == 0x13) { kbd_reset_req = 1; return; }
	unsigned page = ((mods >> 1) & 1) | (((mods >> 2) & 1) << 1) | (((mods >> 4) & 1) << 2);
	uint8_t c = rom[ROM_KBD_MAIN + page * 256 + (mods & 1) * 128 + (b & 0x7F)];
	if (mods & 0x08) c = rom[ROM_KBD_CAPS + c];
	kbd_d3 = mods;
	kbd_d4 = b;
	kbd_d5 = c;
	io_key_event();
}

void kbd_poll(void)
{
	int b;
	while ((b = hal_ps2()) >= 0) key((uint8_t)b);
}
