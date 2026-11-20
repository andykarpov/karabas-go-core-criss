#ifndef IO_H
#define IO_H
#include <stdint.h>

extern uint8_t io_settings[96];   // DS1305 RAM: settings (vectors at 0x3F..0x4E)
extern int io_reset_req;          // SYSRQ 0x0F: reset of the machine

void io_init(void);
void io_poll(void);               // timers, melody, interrupts (often)
uint8_t io_in(uint16_t port);
void io_out(uint16_t port, uint8_t v);
void io_key_event(void);          // a key (event F0 bit 0)
void io_rs_rx_event(void);        // a byte came in on RS-232 (event F0 bit 7)
uint8_t io_sysvar(uint8_t o);     // system variable o (as port F7 reads it)
void io_sysvar_set(uint8_t o, uint8_t v);

// the parts of the steps of Ф3 (io_ext.c until then)
uint8_t io_compat_in(uint8_t p);
void io_compat_out(uint8_t p, uint8_t v);
uint8_t io_rs_status(void);
uint8_t io_rs_get(void);
void io_rs_put(uint8_t c);
void io_rs_speed(uint8_t ubrr);
uint8_t io_mouse_reg(uint8_t i);
void io_mouse_out(uint8_t i, uint8_t v);
uint8_t io_uart1_count(void);
uint8_t io_uart1_get(void);
void io_uart1_put(uint8_t c);
uint8_t io_rtc_cmd(uint8_t a);

// state of the other parts of the firmware as system variables (-1 / 0: not theirs)
int con_sysvar(uint8_t o);
int con_sysvar_set(uint8_t o, uint8_t v);
int bios_sysvar(uint8_t o);
void bios_sysvar_set(uint8_t o, uint8_t v);
void con_raw_vstat(uint8_t v);
void con_raw_cursor(uint8_t hi, uint8_t lo);

#endif
