// The ESP8266 of the board with its stock AT firmware (1.7.x): UDP sockets and ping (docs/PLAN.md Ф4).
#ifndef ESP_H
#define ESP_H
#include <stdint.h>

#define ESP_DATA 726             // the most data of a datagram kept (a 768-byte Ethernet frame - 42)

typedef struct {
	uint16_t lport, rport;       // our port, the port of the sender
	uint8_t ip[4];               // the sender
	uint16_t len;
	uint8_t d[ESP_DATA];
} esp_dgram_t;

void esp_init(void);             // (re)start the module: probe, ATE0, close links, CIPMUX / CIPDINFO, CIFSR
void esp_poll(void);             // the UART and the command queue (often)
int esp_up(void);                // the module answers
const uint8_t *esp_ip(void);     // its station IP (0.0.0.0 without Wi-Fi), MAC
const uint8_t *esp_mac(void);

void esp_listen(uint16_t lport);                                  // a UDP link on our port (receive)
int esp_send(uint16_t lport, const uint8_t ip[4], uint16_t rport, const uint8_t *d, uint16_t n);
                                                                  // queued: 0; -1 module down / queue full
int esp_ping(const uint8_t ip[4]);                                // queued: 0; result: net_ping_done
void net_ping_done(const uint8_t ip[4], int ok);                  // (net.c)

int esp_rx_n(void);                                               // received datagrams waiting
esp_dgram_t *esp_rx_head(void);
void esp_rx_pop(void);
void esp_rx_clear(void);

#endif
