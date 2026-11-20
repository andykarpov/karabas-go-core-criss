// The parts of Ф3 not done yet: answers as "no device" (replaced step by step).
#include "io.h"

uint8_t io_compat_in(uint8_t p) { return p; }            // 4.67: an unknown port reads its number
void io_compat_out(uint8_t p, uint8_t v) { (void)p; (void)v; }
