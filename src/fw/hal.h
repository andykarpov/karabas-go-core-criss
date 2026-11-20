// The CRISS helper firmware: the interface to the machine (hw_rv.c on the core's VexRiscv,
// host/hal_host.c on a PC with a Z80 interpreter). docs/PLAN.md Ф1.
#ifndef HAL_H
#define HAL_H
#include <stdint.h>

typedef struct {
	uint8_t a, f, a2, f2, i, r;
	uint16_t sp, pc, bc, de, hl, ix, bc2, de2, hl2, iy;
	uint8_t im, iff1, iff2;
} z80regs_t;

#define TRAP_FREEZE 0xFF        // the stop asked by hal_freeze(1), not an ED trap
#define HAL_STOP    (-2)        // hal_wait_trap: end of the run (host tests)
#define TRAP_IO     0xFE        // an I/O cycle (hal_io)

// the Z80
void hal_z80_reset(int hold);   // 1: held in reset (the helper owns its memory)
void hal_freeze(int on);        // stop at the next M1 (boot)
int hal_wait_trap(void);        // wait until the Z80 stops: the ED opcode or TRAP_FREEZE; calls
                                // criss_idle() meanwhile and returns -1 when it returns non-zero
void hal_get_regs(z80regs_t *r);
void hal_set_regs(const z80regs_t *r);
void hal_resume(void);
void hal_io(uint16_t *port, int *wr, uint8_t *data);   // the stopped I/O cycle (TRAP_IO)
void hal_io_in(uint8_t v);                            // the byte the IN reads
void hal_int(int req, uint8_t vec);                   // INT request (until acknowledged), its byte
int hal_int_pending(void);                            // a request not yet acknowledged
void hal_tone(uint32_t half_period_ns);               // melody tone: 0 = silence

// the Z80 memory (only while the Z80 is stopped or in reset)
uint8_t hal_rd(uint16_t a);
void hal_wr(uint16_t a, uint8_t v);

// the data of the .kg (rom.h: CP/M image, fonts, keyboard tables)
const uint8_t *hal_rom(void);

// video: VSTAT (LINES << 3 | code page, 0 = sleep), cursor offset from F800 (or 0xFFFF = off)
void hal_video(uint8_t vstat, uint16_t cursor);
void hal_fonts(const uint8_t *fonts, uint32_t len);   // character generators into the video
void hal_scr(uint16_t off, uint8_t c);      // the screen of the video only (not the Z80 RAM): ESC |
void hal_col(uint16_t off, uint8_t colour); // the colour of a cell (0..7: RGB111)

// keyboard: the next PS/2 set 2 byte, -1 if none
int hal_ps2(void);

// floppy images: slot = drive (0 = A:); size in bytes, 0 = no image
uint32_t hal_disk_size(int slot);
int hal_disk_read(int slot, uint32_t lba, uint8_t *buf);       // 512 bytes, 0 = ok
int hal_disk_write(int slot, uint32_t lba, const uint8_t *buf);

// the USB serial port of the board (RS-232 of CRISS)
int hal_ser_get(void);          // a byte, -1 if none
int hal_ser_put(uint8_t c);     // 0 = taken, 1 = full
// mouse: movement since the last call, buttons (bit 0 left, 1 right, 2 middle)
void hal_mouse(int *dx, int *dy, uint8_t *buttons);
// the clock of the board: DS1307 registers 00..06 (BCD) of kg_mcu2
uint8_t hal_rtc_rd(uint8_t a);
void hal_rtc_wr(uint8_t a, uint8_t v);
// the backup RAM file (slot 4 of hal_disk_*): ask for it, size in sectors
void hal_bk_request(uint32_t sectors);

uint32_t hal_ms(void);
void hal_dbg(const char *s);
void hal_beep(void);

// the firmware side
int criss_idle(void);           // polls the keyboard; non-zero: reset asked
void criss_main(void);

#endif
