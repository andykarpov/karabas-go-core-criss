// The other devices of the peripheral MCU of CRISS (docs/re/periph.md 4.7..4.9): RS-232 (the USB
// serial port of the board), the PS/2 mouse (the mouse of the board), I2C (no PCF8574 here: errors);
// UART1 of the CPU (FF 37..39): no line on the boards (backlog).
#include "io.h"
#include "hal.h"

// RS-232: F1 status (clear on read), F3 received byte (clear on read)
static uint8_t rs_flags = 0x20, rs_byte;
static int rs_have;

static void rs_fetch(void)
{
	if (rs_have) return;
	int c = hal_ser_get();
	if (c >= 0) { rs_byte = (uint8_t)c; rs_have = 1; rs_flags |= 0x80; io_rs_rx_event(); }
}

uint8_t io_rs_status(void)
{
	rs_fetch();
	uint8_t v = rs_flags | 0x20;
	rs_flags = rs_have ? 0x80 : 0;
	return v;
}

uint8_t io_rs_get(void)
{
	rs_fetch();
	uint8_t v = rs_have ? rs_byte : 0;
	rs_have = 0;
	rs_flags &= ~0x80;
	return v;
}

void io_rs_put(uint8_t c)
{
	// no reader on the port: the byte is dropped after 50 ms (the Z80 waits in its OUT meanwhile)
	uint32_t t0 = hal_ms();
	while (hal_ser_put(c) && hal_ms() - t0 < 50) ;
	rs_flags |= 0x40;               // TXC
}

void io_rs_speed(uint8_t ubrr) { (void)ubrr; }   // the USB port has no speed

int io_rs_ready(void) { rs_fetch(); return rs_have; }

// mouse / I2C: D7 command (0 done, 1 error), D8 data / buttons, D9 / DA movement sums
static uint8_t d7, d8 = 0xFF, d9, da, ms_on;

uint8_t io_mouse_reg(uint8_t i)
{
	switch (i) {
	case 0x07: return d7;
	case 0x08: return d8;
	case 0x09: return d9;
	default: return da;
	}
}

void io_mouse_out(uint8_t i, uint8_t v)
{
	if (i == 0x08) { d8 = v; return; }
	if (i == 0x09) { d9 = v; return; }
	if (i == 0x0A) { da = v; return; }
	int dx, dy;
	uint8_t b;
	switch (v) {
	case 0xE0: hal_mouse(&dx, &dy, &b); d9 = da = 0; ms_on = 1; d7 = 0; break;
	case 0xE1:
		hal_mouse(&dx, &dy, &b);
		d8 = (uint8_t)((d8 << 4) & 0xF0) | (b & 7);
		d9 = (uint8_t)(d9 + dx);
		da = (uint8_t)(da - dy);          // PS/2: Y up is +
		d7 = 0;
		break;
	case 0xE2: d8 = 0xFF; d7 = 0; break;
	case 0xE4: ms_on = 0; d7 = 0; break;
	default:
		if (v >= 0x10 && v <= 0xDF) { d7 = 1; d8 = 0xFF; }   // PCF8574: no answer (time-out)
		else if (v == 0xFD) { d7 = 1; d8 = 0xFF; }
		else if (v >= 0x10) d7 = 0;
		break;
	}
}

// UART1 (TTY): no line
uint8_t io_uart1_count(void) { return 0; }
uint8_t io_uart1_get(void) { return 0; }
void io_uart1_put(uint8_t c) { (void)c; }
