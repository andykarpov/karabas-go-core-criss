// The parts of Ф3 not done yet: answers as "no device" (replaced step by step).
#include "io.h"

uint8_t io_compat_in(uint8_t p) { return p; }            // 4.67: an unknown port reads its number
void io_compat_out(uint8_t p, uint8_t v) { (void)p; (void)v; }
uint8_t io_rs_status(void) { return 0x20; }              // UDRE
uint8_t io_rs_get(void) { return 0; }
void io_rs_put(uint8_t c) { (void)c; }
void io_rs_speed(uint8_t ubrr) { (void)ubrr; }
uint8_t io_mouse_reg(uint8_t i) { return i == 0x07 ? 1 : 0; }   // TWCMD: 1 = error
void io_mouse_out(uint8_t i, uint8_t v) { (void)i; (void)v; }
uint8_t io_uart1_count(void) { return 0; }
uint8_t io_uart1_get(void) { return 0; }
void io_uart1_put(uint8_t c) { (void)c; }
uint8_t io_rtc_cmd(uint8_t a) { return (uint8_t)(a - 80); }
