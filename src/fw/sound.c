// The melody player of the CRISS peripheral MCU (docs/re/periph.md 4.6): MPCMD (port D6), MPBUF
// (E0..EF), 10 ms ticks, tone table of its firmware (timer 1, 20 MHz, OCR1A).
#include "sound.h"
#include "hal.h"

static const uint16_t ocr[32] = {
	0, 11363, 12039, 12755, 13513, 14317, 15168, 16070, 17026, 18038, 19111, 20247, 21451, 22727, 24078, 25510,
	27027, 28634, 30337, 32140, 34052, 36077, 38221, 40492, 42903, 45454, 48309, 51020, 54054, 57267, 60675, 64283,
};

uint8_t snd_buf[16];
static uint8_t cmd, pos, pd, nd, playing, cont;
static int ticks, tone_ticks;
static uint32_t last_ms;
static int done_event;

static void tone(int idx)
{
	hal_tone(idx ? (uint32_t)(ocr[idx & 31] + 1) * 50u : 0);   // half period: (OCR1A + 1) / 20 MHz
}

static void next_note(void)
{
	for (;;) {
		if (pos >= 16) { playing = 0; tone(0); done_event = 1; return; }   // 16 notes without 04
		uint8_t n = snd_buf[pos];
		if (n == 0x04) { playing = 0; tone(0); return; }
		if (n == 0x05) { pos = 0; continue; }
		pos++;
		int len = nd << (n & 3);
		int p = (n & 4) ? 0 : pd;
		if (p > len) p = len;
		tone(n >> 3);
		tone_ticks = len - p;
		ticks = len;
		if (tone_ticks == 0) tone(0);
		return;
	}
}

void snd_cmd(uint8_t c)
{
	cmd = c;
	if (c == 0x00 || c == 0xFF) { playing = 0; cont = 0; tone(0); cmd = 0; return; }
	if (c == 0xFE) { cont = 1; playing = 0; tone(snd_buf[0] >> 3); return; }
	if (c >= 0xE1) { playing = 0; cont = 0; tone(0); cmd = 0; return; }   // presets: EEPROM of the MCU, erased
	cont = 0;
	pd = c >> 5;
	nd = c & 0x1F;
	pos = 0;
	playing = 1;
	last_ms = hal_ms();
	next_note();
	if (!playing) cmd = 0;
}

uint8_t snd_status(void) { return (playing || cont) ? cmd : 0; }

int snd_poll(void)
{
	uint32_t now = hal_ms();
	while (playing && now - last_ms >= 10) {
		last_ms += 10;
		ticks--;
		tone_ticks--;
		if (tone_ticks == 0) tone(0);
		if (ticks <= 0) next_note();
	}
	if (!playing && !cont) cmd = 0;
	int e = done_event;
	done_event = 0;
	return e;
}
