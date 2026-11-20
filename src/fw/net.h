// The network of CRISS on the ESP8266 (net.c, docs/PLAN.md Ф4).
#ifndef NET_H
#define NET_H
#include <stdint.h>

extern uint8_t net_bank[57];      // the bank FA / FB (AVR 0x0949)

void net_init(void);              // boot of CRISS
void net_settings(void);          // the settings of the NVRAM image came (PR_SRV, LST IOBYTE, MAC / IP)
void net_poll(void);              // often
int net_on(void);                 // the module answers and LAN is not disabled (settings 0x30 bit 5)
uint8_t net_cmd(uint8_t a);       // FE 48..79: the result for IN FE

// the CP/M channels by IOBYTE (docs/re/cpu-io.md 11.7)
void net_con_out(uint8_t c);
void net_list_out(uint8_t c);
void net_punch_out(uint8_t c);
void net_in_poll(void);           // datagrams of the console / READER servers into their queues
int net_reader_get(void);         // -1: empty
const uint8_t *net_pr_ip(void);   // PR_SRV (system variables 80..85)
uint16_t net_pr_port(void);

#endif
