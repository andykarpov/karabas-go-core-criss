#ifndef CLR_H
#define CLR_H
#include <stdint.h>

void clr_init(void);
void clr_cmd(uint8_t b);     // a command byte of the CPU to the colour module

#endif
