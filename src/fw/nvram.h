#ifndef NVRAM_H
#define NVRAM_H
#include <stdint.h>
#include "hal.h"

#define CON_STATE_SIZE 32

void nv_init(void);                       // ask for the backup RAM file
void nv_poll(void);
int nv_snap_save(int n, const z80regs_t *r);
int nv_snap_load(int n, z80regs_t *r);

void con_state_save(uint8_t *b);          // CON_STATE_SIZE bytes
void con_state_load(const uint8_t *b);
void bios_state_save(uint8_t *b);         // 8 bytes
void bios_state_load(const uint8_t *b);

#endif
