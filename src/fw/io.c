// The Z80 ports of CRISS (docs/re/cpu-io.md 1, 3..9; docs/re/periph.md 4): the registers of the
// peripheral MCU (D0..F3) as its firmware 0.24 keeps them, the banks F4..FD (system variables F6 / F7),
// the command ports FE / FF, the compatibility range 00..CF, the interrupts of the peripheral MCU.
#include "io.h"
#include "hal.h"
#include "con.h"
#include "kbd.h"
#include "sound.h"
#include <string.h>

// peripheral MCU registers: index = port - 0xD0
static uint8_t kvu[0x24];
static uint8_t kvu_mask;          // GPIOR1 of the MCU: events that raise its interrupt line
static uint8_t ubrr = 129;        // RS-232 speed (9600)
static uint32_t de_ms;            // the 10 ms timer of DE

// banks F4..FD: address registers, the memories
static uint8_t bank_addr[5];      // F4, F6, F8, FA, FC
static uint8_t bank_f4[19], bank_f8[36], bank_fa[57], bank_fc[134];
static uint8_t sysvar[256];       // F6 / F7: the plain variables (the others from the firmware state)

// FE / FF
static uint8_t fe_res = 0xFF, ff_res;
static uint8_t ff_state, ff_n, ff_p[2];

uint8_t io_settings[96];          // the 96 bytes of the DS1305 RAM (settings of the system console)
int io_reset_req;

// interrupts of the Z80: pending sources 0..15 (INT_MASK = sysvar 3 / 4)
static uint16_t int_pend;
static uint8_t int_cur = 0xFF;    // the source whose request is out

void io_init(void)
{
	memset(kvu, 0, sizeof kvu);
	kvu[0x00] = 0xC0;
	kvu[0x01] = 0x01;
	kvu_mask = 0;
	memset(sysvar, 0, sizeof sysvar);
	sysvar[0] = 0;                 // platform CRISS
	sysvar[1] = 0x11;              // CON_IOBYTE
	sysvar[2] = 0x8B;              // HW_FOUND: RAM, CLR, RTC, peripheral MCU
	sysvar[21] = 1;                // SD_TYPE
	for (int i = 0; i < 16; i++) sysvar[26 + i] = (uint8_t)(i + 1);
	memset(bank_addr, 0, sizeof bank_addr);
	fe_res = 0xFF; ff_state = 0; ff_n = 0;
	int_pend = 0; int_cur = 0xFF;
	de_ms = hal_ms();
	hal_int(0, 0xFF);
}

// ------------------------------------------------------------------ events and interrupts

static void kvu_event(uint8_t bits)
{
	kvu[0x20] |= bits;
	// the MCU pulses its interrupt line for events in its mask; the CPU then reads F0 (clearing it)
	// and keeps the sources of its INT_MASK (low byte)
	if (kvu[0x20] & kvu_mask) {
		int_pend |= kvu[0x20] & sysvar[3];
		kvu[0x20] = 0;
	}
}

void io_key_event(void) { kvu_event(0x01); }

static void int_update(void)
{
	if (int_cur != 0xFF) {
		if (hal_int_pending()) return;
		int_pend &= (uint16_t)~(1u << int_cur);    // acknowledged
		int_cur = 0xFF;
	}
	uint16_t p = int_pend & (uint16_t)(sysvar[3] | sysvar[4] << 8);
	if (!p) return;
	int i = 0;
	while (!(p & (1u << i))) i++;
	int_cur = (uint8_t)i;
	hal_int(1, io_settings[0x3F + i]);   // the vector bytes: settings 0x3F.. (DS1305 RAM 0x36CF - 0x3690)
}

void io_poll(void)
{
	uint32_t now = hal_ms();
	while (now - de_ms >= 10) {
		de_ms += 10;
		sysvar[86]++;              // TMR_10MS
		if (kvu[0x0E]) { if (--kvu[0x0E] == 0) kvu_event(0x04); }
	}
	if (snd_poll()) kvu_event(0x08);
	int_update();
}

// ------------------------------------------------------------------ system variables

static uint8_t sv_rd(uint8_t o)
{
	int v = con_sysvar(o);
	if (v >= 0) return (uint8_t)v;
	v = bios_sysvar(o);
	if (v >= 0) return (uint8_t)v;
	return sysvar[o];
}

static void sv_wr(uint8_t o, uint8_t v)
{
	sysvar[o] = v;
	if (con_sysvar_set(o, v)) return;
	bios_sysvar_set(o, v);
}

uint8_t io_sysvar(uint8_t o) { return sv_rd(o); }
void io_sysvar_set(uint8_t o, uint8_t v) { sv_wr(o, v); }

// ------------------------------------------------------------------ FE / FF

static void fe_cmd(uint8_t a)
{
	if (a == 1) { fe_res = sysvar[21]; for (int i = 0; i < 16; i++) sysvar[26 + i] = (uint8_t)(i + 1); }
	else if (a >= 80 && a <= 95) fe_res = io_rtc_cmd(a);
	else if (a >= 96) ;                       // nothing, the result stays
	else fe_res = 0xFF;                       // SPI, CAN, Ethernet (Ф4): no device
}

static float flt(int o) { float f; uint8_t b[4]; for (int i = 0; i < 4; i++) b[i] = sysvar[o + i]; memcpy(&f, b, 4); return f; }

static void ff_write(uint8_t a)
{
	if (ff_state == 0) {
		if (a < 32) {                         // FPU: operands in the system variables 52..59
			sysvar[51] = a;
			float r, x = flt(52), y = flt(56);
			switch (a & 0x7F) {
			case 0: r = x + y; break;
			case 1: r = x - y; break;
			case 2: r = x * y; break;
			case 3: r = x / y; break;
			default: sysvar[51] = 0x40; ff_res = a; return;
			}
			uint8_t b[4];
			memcpy(b, &r, 4);
			for (int i = 0; i < 4; i++) sysvar[60 + i] = b[i];
			sysvar[51] = a | 0x80;
			ff_res = a;
			return;
		}
		switch (a) {
		case 35: ff_res = fe_res = 0x43; return;     // version 4.67
		case 36: ff_res = fe_res = 0x04; return;
		case 37: ff_res = io_uart1_count(); return;
		case 38: ff_res = io_uart1_get(); return;
		default: ff_state = a; ff_n = 0; return;     // 32..34, 39: parameters follow
		}
	}
	switch (ff_state) {
	case 32: ff_res = fe_res = 0; ff_state = 0; break;       // AVR data space: nothing to show
	case 33: if (ff_n++ == 1) { ff_res = fe_res = a; ff_state = 0; } break;
	case 34: if (ff_n++ == 2) { ff_res = fe_res = a; ff_state = 0; } break;
	case 39: io_uart1_put(a); ff_state = 0; break;           // (4.67 sends a wrong byte: fixed)
	default: ff_state = 0; break;
	}
	(void)ff_p;
}

// ------------------------------------------------------------------ the ports

static uint8_t *bank_mem(int b, int *len)
{
	switch (b) {
	case 0: *len = sizeof bank_f4; return bank_f4;
	case 2: *len = sizeof bank_f8; return bank_f8;
	case 3: *len = sizeof bank_fa; return bank_fa;
	case 4: *len = sizeof bank_fc; return bank_fc;
	default: *len = 254; return 0;
	}
}

uint8_t io_in(uint16_t port)
{
	uint8_t p = port & 0xFF;
	if (p < 0xD0) return io_compat_in(p);
	if (p <= 0xF3) {
		uint8_t i = p - 0xD0, v = kvu[i];
		switch (i) {
		case 0x03: v = kbd_d3; break;
		case 0x04: v = kbd_d4; break;
		case 0x05: v = kbd_d5; break;
		case 0x06: v = snd_status(); break;
		case 0x20: kvu[0x20] = 0; break;                  // F0 events
		case 0x21: v = io_rs_status(); break;             // F1
		case 0x23: v = io_rs_get(); break;                // F3
		case 0x07: case 0x08: case 0x09: case 0x0A: v = io_mouse_reg(i); break;
		default: break;
		}
		return v;
	}
	if (p <= 0xFD) {
		int b = (p - 0xF4) >> 1;
		if (!(p & 1)) return bank_addr[b];
		int len;
		uint8_t *m = bank_mem(b, &len), a = bank_addr[b];
		uint8_t v = 0xFF;
		if (a < len) v = m ? m[a] : sv_rd(a);
		bank_addr[b]++;
		return v;
	}
	return p == 0xFE ? fe_res : ff_res;
}

void io_out(uint16_t port, uint8_t v)
{
	uint8_t p = port & 0xFF;
	if (p < 0xD0) { io_compat_out(p, v); return; }
	if (p <= 0xF3) {
		uint8_t i = p - 0xD0;
		switch (i) {
		case 0x00: kvu[0] = v; con_raw_vstat(v); return;
		case 0x01: case 0x02: kvu[i] = v; con_raw_cursor(kvu[1], kvu[2]); return;
		case 0x05: kvu[5] = v; kbd_d5 = v; return;         // the CPU clears the character
		case 0x06: snd_cmd(v); return;
		case 0x07: case 0x08: io_mouse_out(i, v); return;
		case 0x0B:                                         // SYSRQ
			kvu[0x0B] = v;
			if (v & 0x80) {
				uint8_t a = kvu[0x0C];
				switch (v & 0x0F) {
				case 1: kvu[0x0D] = a == 0x4A ? kvu_mask : a == 0xC4 ? ubrr : 0; break;
				case 2: case 3:
					if (a == 0x4A) kvu_mask = kvu[0x0D];
					else if (a == 0xC4) { ubrr = kvu[0x0D]; io_rs_speed(ubrr); }
					break;
				case 0x0E: kvu[0x0C] = 0; kvu[0x0D] = 0x18; break;
				case 0x0F: io_reset_req = 1; break;
				default: break;
				}
				kvu[0x0B] &= 0x70;
			}
			return;
		case 0x0E: kvu[0x0E] = v; de_ms = hal_ms(); return;
		case 0x0F: kvu[0x0F] = v; while (kvu[0x0F]) { io_rs_put(kvu[0x22]); kvu[0x0F]--; } kvu_event(0x40); return;
		default:
			if (i >= 0x10 && i <= 0x1F) snd_buf[i - 0x10] = v;
			kvu[i] = v;
			return;
		}
	}
	if (p <= 0xFD) {
		int b = (p - 0xF4) >> 1;
		if (!(p & 1)) { bank_addr[b] = v; return; }
		int len;
		uint8_t *m = bank_mem(b, &len), a = bank_addr[b];
		if (a < len) { if (m) m[a] = v; else sv_wr(a, v); }
		bank_addr[b]++;
		return;
	}
	if (p == 0xFE) fe_cmd(v);
	else ff_write(v);
}

// RS-232 received byte (step 3): event F0 bit 7
void io_rs_rx_event(void) { kvu_event(0x80); }
