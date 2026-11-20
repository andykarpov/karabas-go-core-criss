// The parts of Ф3 not done yet: answers as "no device" (replaced step by step).
#include "io.h"

#include "hal.h"
#include "rom.h"
#include "kbd.h"
#include "con.h"

// IN 00..CF (0x17022): 0C = the keyboard of Robotron (the code of the scan code D4 by a table),
// others read their number
uint8_t io_compat_in(uint8_t p)
{
	if (p != 0x0C) return p;
	const uint8_t *t = hal_rom() + ROM_COMPAT_KEY;
	for (int i = 0; i < 64 && t[i]; i += 2)
		if (t[i] == kbd_d4) return t[i + 1];
	return kbd_d5;          // 4.67: A unchanged (here: the character)
}

// OUT 00..CF (0x17096): the 8275 of Robotron on 18 / 19
void io_compat_out(uint8_t p, uint8_t v) { if (p == 0x18 || p == 0x19) con_8275(p, v); }
