// The hal of the helper firmware on the VexRiscv of criss_soc.v (MMIO map: its header).
#include "hal.h"
#include "rom.h"

#define IO(o)    (*(volatile uint32_t *)(0x80000000u + (o)))
#define Z80M(a)  (*(volatile uint32_t *)(0x90000000u + 4u * (a)))
#define FONT(i)  (*(volatile uint32_t *)(0xA0000000u + 4u * (i)))
#define SCR(i)   (*(volatile uint32_t *)(0xB0000000u + 4u * (i)))
#define COL(i)   (*(volatile uint32_t *)(0xC0000000u + 4u * (i)))
#define SDBUF(i) (*(volatile uint32_t *)(0x80001000u + 4u * (i)))

#define R_DBG    0x00
#define R_Z80    0x04
#define R_ZST    0x08
#define R_RESUME 0x0c
#define R_DIRSET 0x10
#define R_MS     0x14
#define R_PS2    0x18
#define R_VIDEO  0x1c
#define R_BEEP   0x20
#define R_IO     0x28
#define R_IODIN  0x2c
#define R_INT    0x30
#define R_TONE   0x34
#define R_SER    0x38
#define R_SERF   0x3c
#define R_RTCA   0x60
#define R_RTCD   0x64
#define R_RTCW   0x68
#define R_MOUSE  0x6c
#define R_BKREQ  0x90
#define R_REG    0x40
#define R_SECTOR 0x80
#define R_CMD    0x84
#define R_STAT   0x88
#define R_SIZE   0xa0

static uint32_t z80ctl = 1;      // bit 0 reset, bit 1 freeze
static uint32_t regw[7];         // REG as read (DIR written from it)

void hal_z80_reset(int hold) { z80ctl = (z80ctl & ~1u) | (hold ? 1u : 0u); IO(R_Z80) = z80ctl; }
void hal_freeze(int on) { z80ctl = (z80ctl & ~2u) | (on ? 2u : 0u); IO(R_Z80) = z80ctl; }

int hal_wait_trap(void)
{
	for (;;) {
		uint32_t st = IO(R_ZST);
		if (st & 1) return (st >> 8) & 0xFF;
		if (criss_idle()) return -1;
	}
}

static unsigned bits(int lsb, int n)
{
	uint64_t v = regw[lsb >> 5] >> (lsb & 31);
	if ((lsb & 31) + n > 32) v |= (uint64_t)regw[(lsb >> 5) + 1] << (32 - (lsb & 31));
	return (unsigned)(v & ((1u << n) - 1));
}

static void setbits(int lsb, int n, unsigned val)
{
	for (int i = 0; i < n; i++) {
		int b = lsb + i;
		regw[b >> 5] = (regw[b >> 5] & ~(1u << (b & 31))) | (((val >> i) & 1u) << (b & 31));
	}
}

// T80 REG: A F A' F' I R SP PC BC DE HL IX BC' DE' HL' IY IM(2) IFF1 IFF2
void hal_get_regs(z80regs_t *r)
{
	for (int i = 0; i < 7; i++) regw[i] = IO(R_REG + 4 * i);
	r->a = bits(0, 8); r->f = bits(8, 8); r->a2 = bits(16, 8); r->f2 = bits(24, 8);
	r->i = bits(32, 8); r->r = bits(40, 8); r->sp = bits(48, 16); r->pc = bits(64, 16);
	r->bc = bits(80, 16); r->de = bits(96, 16); r->hl = bits(112, 16); r->ix = bits(128, 16);
	r->bc2 = bits(144, 16); r->de2 = bits(160, 16); r->hl2 = bits(176, 16); r->iy = bits(192, 16);
	r->im = bits(208, 2); r->iff1 = bits(210, 1); r->iff2 = bits(211, 1);
}

void hal_set_regs(const z80regs_t *r)
{
	setbits(0, 8, r->a); setbits(8, 8, r->f); setbits(16, 8, r->a2); setbits(24, 8, r->f2);
	setbits(32, 8, r->i); setbits(40, 8, r->r); setbits(48, 16, r->sp); setbits(64, 16, r->pc);
	setbits(80, 16, r->bc); setbits(96, 16, r->de); setbits(112, 16, r->hl); setbits(128, 16, r->ix);
	setbits(144, 16, r->bc2); setbits(160, 16, r->de2); setbits(176, 16, r->hl2); setbits(192, 16, r->iy);
	setbits(208, 2, r->im); setbits(210, 1, r->iff1); setbits(211, 1, r->iff2);
	for (int i = 0; i < 7; i++) IO(R_REG + 4 * i) = regw[i];
	IO(R_DIRSET) = 1;
}

void hal_resume(void) { IO(R_RESUME) = 1; }

void hal_io(uint16_t *port, int *wr, uint8_t *data)
{
	uint32_t v = IO(R_IO);
	*port = (uint16_t)v;
	*data = (uint8_t)(v >> 16);
	*wr = (IO(R_ZST) >> 1) & 1;
}
void hal_io_in(uint8_t v) { IO(R_IODIN) = v; }
void hal_int(int req, uint8_t vec) { IO(R_INT) = (req ? 0x100u : 0) | vec; }
int hal_int_pending(void) { return (IO(R_INT) >> 8) & 1; }
void hal_tone(uint32_t half_period_ns) { IO(R_TONE) = half_period_ns * 3u / 40u; }   // clocks of 75 MHz
uint8_t hal_rd(uint16_t a) { return (uint8_t)Z80M(a); }
void hal_wr(uint16_t a, uint8_t v) { Z80M(a) = v; }
const uint8_t *hal_rom(void) { return (const uint8_t *)0x10100000u; }

void hal_video(uint8_t vstat, uint16_t cursor) { IO(R_VIDEO) = (uint32_t)cursor << 16 | vstat; }

void hal_fonts(const uint8_t *f, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) FONT(i) = f[i];
}

void hal_scr(uint16_t off, uint8_t c) { SCR(off) = c; }
void hal_col(uint16_t off, uint8_t colour) { COL(off) = colour; }

int hal_ps2(void)
{
	uint32_t v = IO(R_PS2);
	return (v & 0x100) ? (int)(v & 0xFF) : -1;
}

uint32_t hal_disk_size(int slot) { return slot <= 4 ? IO(R_SIZE + 4 * slot) : 0; }

static int sd_cmd(int slot, uint32_t lba, uint32_t cmd)
{
	IO(R_SECTOR) = lba;
	IO(R_CMD) = cmd | (uint32_t)slot << 4;
	uint32_t st;
	while ((st = IO(R_STAT)) & 1) ;
	return (st >> 1) & 1;
}

int hal_disk_read(int slot, uint32_t lba, uint8_t *buf)
{
	if (sd_cmd(slot, lba, 1)) return 1;
	for (int i = 0; i < 512; i++) buf[i] = (uint8_t)SDBUF(i);
	return 0;
}

int hal_disk_write(int slot, uint32_t lba, const uint8_t *buf)
{
	for (int i = 0; i < 512; i++) SDBUF(i) = buf[i];
	return sd_cmd(slot, lba, 2);
}

int hal_ser_get(void) { uint32_t v = IO(R_SER); return (v & 0x100) ? (int)(v & 0xFF) : -1; }
int hal_ser_put(uint8_t c) { if (IO(R_SERF) & 1) return 1; IO(R_SER) = c; return 0; }

void hal_mouse(int *dx, int *dy, uint8_t *buttons)
{
	uint32_t v = IO(R_MOUSE);
	*dx = (int8_t)(v & 0xFF);
	*dy = (int8_t)((v >> 8) & 0xFF);
	*buttons = (uint8_t)(v >> 24);
}

uint8_t hal_rtc_rd(uint8_t a) { IO(R_RTCA) = a; (void)IO(R_RTCD); return (uint8_t)IO(R_RTCD); }
void hal_rtc_wr(uint8_t a, uint8_t v) { IO(R_RTCW) = (uint32_t)a << 8 | v; }
void hal_bk_request(uint32_t sectors) { IO(R_BKREQ) = sectors; }

uint32_t hal_ms(void) { return IO(R_MS); }
void hal_dbg(const char *s) { while (*s) IO(R_DBG) = (uint8_t)*s++; }
void hal_beep(void) { IO(R_BEEP) = 197; }

int main(void)
{
	criss_main();
	for (;;) ;
}
