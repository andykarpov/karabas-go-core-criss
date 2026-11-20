#ifndef KBD_H
#define KBD_H
#include <stdint.h>

extern uint8_t kbd_d3, kbd_d4, kbd_d5;   // modifiers, scan code, character (0 = none, the CPU clears it)
extern int kbd_reset_req;                 // Ctrl + Shift + Alt + Esc seen

void kbd_init(void);
void kbd_poll(void);

#endif
