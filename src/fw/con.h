#ifndef CON_H
#define CON_H
#include <stdint.h>

extern uint8_t con_fifo[32];     // console input queue (ESC k), taken before the keyboard
extern uint8_t con_fifo_n;

void con_init(void);
void con_out(uint8_t c, uint8_t z80_a);    // z80_a: register A (ESC Z takes CHARS from it)
void con_melody(const uint8_t *notes, int n, uint8_t cmd);   // ESC m (weak: the sound of Ф3)

uint8_t con_platform(void);
void con_set_platform(uint8_t p);         // 0 CRISS, 1 / 2 Robotron RU / EN, 3 Kaypro, 4 TRS-80, 5 Kaypro 8 bit, 6 VT100
void con_toggle_s1(void);                 // Robotron s0 / s1 (NumLock)
void con_8275(uint8_t port, uint8_t v);   // OUT 18 / 19 (Robotron 8275)

#endif
