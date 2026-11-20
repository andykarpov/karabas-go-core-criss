#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>

extern uint8_t snd_buf[16];     // MPBUF (ports E0..EF)
void snd_cmd(uint8_t c);        // MPCMD write (port D6)
uint8_t snd_status(void);       // MPCMD read: 0 = finished
int snd_poll(void);             // every few ms; 1 = the 16-note buffer ran out (event F0 bit 3)

#endif
