#ifndef CON_H
#define CON_H
#include <stdint.h>

extern uint8_t con_fifo[32];     // console input queue (ESC k), taken before the keyboard
extern uint8_t con_fifo_n;

void con_init(void);
void con_out(uint8_t c, uint16_t z80_a);   // z80_a: register A (ESC Z takes CHARS from it)

#endif
