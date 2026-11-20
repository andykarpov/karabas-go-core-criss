// The network of CRISS (ENC28J60, FE 48..79, bank FA / FB, docs/re/cpu-io.md 11) on the ESP8266 of the
// board (docs/PLAN.md Ф4 step 3). The Z80 sees the API of the firmware 4.67: frames are built in the
// 768-byte buffer (AVR 0x3200) as there; a frame sent (54) is taken apart: UDP goes out through the
// module, an ARP request gets a made-up answer (MAC 02:00:<IP>), an ICMP echo request becomes AT+PING
// and an echo reply when it comes back; TCP and other frames are dropped. Datagrams from the module
// come in as Ethernet / IP / UDP frames to MAC_MY / IP_MY of the bank.
#include "net.h"
#include "esp.h"
#include "io.h"
#include "con.h"
#include "hal.h"
#include <string.h>

uint8_t net_bank[57];
#define B16(o) ((uint16_t)(net_bank[o] | net_bank[(o) + 1] << 8))
static void b16_set(int o, uint16_t v) { net_bank[o] = (uint8_t)v; net_bank[o + 1] = (uint8_t)(v >> 8); }
enum {
	LAN_PAR = 0, MBUF_IN = 2, MBUF_IN_L = 4, MBUF_OUT = 6, MBUF_OUT_L = 8, IP_MY = 10, IP_REMOTE = 14,
	MAC_MY = 18, MAC_REMOTE = 24, UPD_DATA = 35, PORT_MY = 37, PORT_REMOTE = 39, PROTOCOL = 41,
	MAIN_RQST = 42, IP_ID = 43, ECHO_CNTR = 45
};

#define FB 768
static uint8_t fb[FB];                  // the frame buffer (AVR 0x3200)

// received frames for 55 / 56 / 66: the datagrams of the module and the made-up answers
#define NQ 4
static uint8_t q[NQ][FB];
static uint16_t q_len[NQ];
static int q_h, q_n;

uint8_t net_reader[32];                 // the READER queue (AVR 0x3797)
uint8_t net_reader_n;

// a pending ping: the echo request frame (its answer is made from it)
static uint8_t ping_frame[128];
static uint16_t ping_len;
static int ping_busy;

static int fake_settings_done;

// ------------------------------------------------------------------ frames

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void be16_set(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static uint16_t csum(const uint8_t *p, int n)
{
	uint32_t s = 0;
	for (; n > 1; n -= 2, p += 2) s += (uint32_t)(p[0] << 8 | p[1]);
	if (n) s += (uint32_t)p[0] << 8;
	while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
	return (uint16_t)~s;
}

static void fake_mac(uint8_t *mac, const uint8_t *ip) { mac[0] = 0x02; mac[1] = 0; memcpy(mac + 2, ip, 4); }

static void eth_hdr(uint8_t *f, const uint8_t *dst, const uint8_t *src, uint16_t type)
{
	memcpy(f, dst, 6); memcpy(f + 6, src, 6); be16_set(f + 12, type);
}

static void ip_hdr(uint8_t *f, uint16_t len, uint8_t proto, const uint8_t *src, const uint8_t *dst, uint16_t id)
{
	uint8_t *h = f + 14;
	h[0] = 0x45; h[1] = 0; be16_set(h + 2, len); be16_set(h + 4, id); h[6] = 0; h[7] = 0;
	h[8] = 128; h[9] = proto; h[10] = h[11] = 0;
	memcpy(h + 12, src, 4); memcpy(h + 16, dst, 4);
	be16_set(h + 10, csum(h, 20));
}

static uint16_t next_id(void) { uint16_t id = (uint16_t)(B16(IP_ID) + 1); b16_set(IP_ID, id); return id; }

// the IP and Ethernet headers in front of an IP payload of n bytes at fb + 34 (60, 61, 70)
static void out_ip(uint16_t n, uint8_t proto)
{
	ip_hdr(fb, (uint16_t)(n + 20), proto, net_bank + IP_MY, net_bank + IP_REMOTE, next_id());
	eth_hdr(fb, net_bank + MAC_REMOTE, net_bank + MAC_MY, 0x0800);
}

static uint16_t z80_in(int at, int max)      // MBUF_OUT, MBUF_OUT_L bytes of the Z80 into fb + at
{
	uint16_t n = B16(MBUF_OUT_L), a = B16(MBUF_OUT);
	if (n > max) n = (uint16_t)max;
	for (int i = 0; i < n; i++) fb[at + i] = hal_rd((uint16_t)(a + i));
	return n;
}

static void q_put(const uint8_t *f, uint16_t n)
{
	if (q_n == NQ) return;                           // full: dropped (as a full ENC28J60 buffer)
	int i = (q_h + q_n) % NQ;
	memcpy(q[i], f, n);
	q_len[i] = n;
	q_n++;
}

static void q_pop(void) { if (q_n) { q_h = (q_h + 1) % NQ; q_n--; } }

// a datagram of the module into the queue as a frame
static void take_dgrams(void)
{
	esp_dgram_t *d;
	while (q_n < NQ && (d = esp_rx_head())) {
		static uint8_t f[FB];
		uint16_t n = d->len;
		uint8_t mac[6];
		fake_mac(mac, d->ip);
		eth_hdr(f, net_bank + MAC_MY, mac, 0x0800);
		ip_hdr(f, (uint16_t)(n + 28), 17, d->ip, net_bank + IP_MY, (uint16_t)hal_ms());
		be16_set(f + 34, d->rport); be16_set(f + 36, d->lport); be16_set(f + 38, (uint16_t)(n + 8)); f[40] = f[41] = 0;
		memcpy(f + 42, d->d, n);
		q_put(f, (uint16_t)(n + 42));
		esp_rx_pop();
	}
}

void net_ping_done(const uint8_t ip[4], int ok)
{
	ping_busy = 0;
	if (!ok) return;
	// the echo reply: the request with the addresses swapped, type 0
	uint8_t *f = ping_frame;
	uint8_t t[6];
	memcpy(t, f, 6); memcpy(f, f + 6, 6); memcpy(f + 6, t, 6);
	memcpy(t, f + 26, 4); memcpy(f + 26, f + 30, 4); memcpy(f + 30, t, 4);
	be16_set(f + 16, (uint16_t)(ping_len - 14));
	f[24] = f[25] = 0; be16_set(f + 24, csum(f + 14, 20));
	int ihl = (f[14] & 15) * 4;
	uint8_t *ic = f + 14 + ihl;
	ic[0] = 0; ic[2] = ic[3] = 0;
	be16_set(ic + 2, csum(ic, ping_len - 14 - ihl));
	q_put(f, ping_len);
}

// 54: a frame of the Z80 goes out
static uint8_t send_frame(uint16_t n)
{
	if (n > FB) return 0xFF;
	if (n < 14) return 5;
	uint16_t type = be16(fb + 12);
	if (type == 0x0806 && n >= 42 && be16(fb + 20) == 1) {          // ARP request: answer it ourselves
		uint8_t f[42], mac[6];
		fake_mac(mac, fb + 38);
		eth_hdr(f, fb + 22, mac, 0x0806);
		memcpy(f + 14, fb + 14, 6); be16_set(f + 20, 2);
		memcpy(f + 22, mac, 6); memcpy(f + 28, fb + 38, 4);       // sender: the asked IP
		memcpy(f + 32, fb + 22, 10);                              // target: the asker
		q_put(f, 42);
		return 5;
	}
	if (type != 0x0800 || n < 34) return 5;
	const uint8_t *ip = fb + 14;
	int ihl = (ip[0] & 15) * 4, tot = be16(ip + 2);
	if (tot > n - 14) tot = n - 14;
	const uint8_t *pl = ip + ihl;
	int pn = tot - ihl;
	if (pn < 0) return 5;
	if (ip[9] == 17 && pn >= 8) {                                   // UDP
		int dn = be16(pl + 4) - 8;
		if (dn > pn - 8) dn = pn - 8;
		if (dn < 0) dn = 0;
		uint32_t t0 = hal_ms();
		while (esp_send(be16(pl), ip + 16, be16(pl + 2), pl + 8, (uint16_t)dn) && esp_up() && hal_ms() - t0 < 3000) esp_poll();
	} else if (ip[9] == 1 && pn >= 8 && pl[0] == 8 && !ping_busy) {  // ICMP echo request
		ping_len = (uint16_t)(14 + tot > (int)sizeof ping_frame ? (int)sizeof ping_frame : 14 + tot);
		memcpy(ping_frame, fb, ping_len);
		if (esp_ping(ip + 16) == 0) ping_busy = 1;
	}
	return 5;
}

// the classification of a frame (0x133FC)
static int ip_is_my(const uint8_t *ip)
{
	for (int i = 0; i < 4; i++) if (net_bank[IP_MY + i] != 0xFF && net_bank[IP_MY + i] != ip[i]) return 0;
	return 1;
}

static uint8_t classify(const uint8_t *f, uint16_t n)
{
	if (n < 42) return 0;
	uint16_t type = be16(f + 12);
	if (type == 0x0806) {
		uint16_t op = be16(f + 20);
		if (op == 1 && ip_is_my(f + 38)) return 0x81;
		if (op == 2 && !memcmp(f + 28, net_bank + IP_REMOTE, 4)) { memcpy(net_bank + MAC_REMOTE, f + 22, 6); return 0x82; }
		return 0;
	}
	if (type != 0x0800) return 0;
	const uint8_t *ip = f + 14, *pl = ip + (ip[0] & 15) * 4;
	if (ip[9] == 17 && ip_is_my(ip + 16)) {
		uint16_t sp = be16(pl), dp = be16(pl + 2);
		if (!memcmp(ip + 12, net_bank + IP_REMOTE, 4) && dp == B16(PORT_MY)) return 0x83;
		if (!memcmp(ip + 12, net_pr_ip(), 4) && sp == net_pr_port()) return 0x84;
		if (!memcmp(ip + 12, io_settings + 0x1C, 4) && sp == (io_settings[0x20] | io_settings[0x21] << 8)) return 0x85;
		return 0;
	}
	if (ip[9] == 1 && ip_is_my(ip + 16)) {
		if (pl[0] == 8) return 0x8A;
		if (pl[0] == 0 && !memcmp(ip + 12, net_bank + IP_REMOTE, 4)) { net_bank[ECHO_CNTR]++; return 0x8B; }
	}
	return 0;
}

// bytes of a datagram into a FIFO of 32 (zeros dropped); 1 = all taken
static int to_fifo(uint8_t *fifo, uint8_t *fn, uint8_t *f, uint16_t *n)
{
	uint8_t *ip = f + 14;
	int at = 14 + (ip[0] & 15) * 4 + 8;
	int dn = be16(ip + (ip[0] & 15) * 4 + 4) - 8;
	if (dn > *n - at) dn = *n - at;
	int i = 0;
	for (; i < dn; i++) {
		if (!f[at + i]) continue;
		if (*fn >= 32) break;
		fifo[(*fn)++] = f[at + i];
	}
	if (i >= dn) return 1;
	// the rest stays for the next call (4.67 gave the whole datagram again: not repeated here)
	memmove(f + at, f + at + i, (size_t)(dn - i));
	be16_set(ip + (ip[0] & 15) * 4 + 4, (uint16_t)(dn - i + 8));
	*n = (uint16_t)(at + dn - i);
	return 0;
}

// 66: the queue of received frames (0x13726)
static uint8_t process(void)
{
	if (net_bank[MAIN_RQST]) return 0xFE;
	take_dgrams();
	while (q_n) {
		uint8_t *f = q[q_h];
		uint8_t c = classify(f, q_len[q_h]);
		if (c == 0x83 || c == 0x86) {
			net_bank[MAIN_RQST] = c;
			memcpy(fb, f, q_len[q_h]);
			b16_set(MBUF_IN_L, q_len[q_h]);
			q_pop();
			take_dgrams();
			return c;
		}
		if (c == 0x84 && (io_sysvar(72) & 0x40)) {
			if (!to_fifo(net_reader, &net_reader_n, f, &q_len[q_h])) return 0;
		} else if (c == 0x85 && (io_sysvar(1) & 0x40)) {
			if (!to_fifo(con_fifo, &con_fifo_n, f, &q_len[q_h])) return 0;
		}
		q_pop();
		take_dgrams();
	}
	return 0;
}

// ------------------------------------------------------------------ the ports

const uint8_t *net_pr_ip(void) { static uint8_t ip[4]; for (int i = 0; i < 4; i++) ip[i] = io_sysvar((uint8_t)(80 + i)); return ip; }
uint16_t net_pr_port(void) { return (uint16_t)(io_sysvar(84) | io_sysvar(85) << 8); }

static void listen_all(void)
{
	esp_listen(B16(PORT_MY));
	if (io_sysvar(1) & 0x40) esp_listen((uint16_t)(io_settings[0x20] | io_settings[0x21] << 8));
	if (io_sysvar(72) & 0x40) esp_listen(net_pr_port());
}

int net_on(void) { return esp_up() && !(io_settings[0x30] & 0x20); }

uint8_t net_cmd(uint8_t a)
{
	if (!net_on()) return 0xFF;
	uint8_t i = (uint8_t)(a - 48);
	esp_poll();
	switch (i) {
	case 0: case 1:                                  // init (49: + MAADR)
		q_n = 0; esp_rx_clear(); con_fifo_n = 0; net_reader_n = 0;
		listen_all();
		return i;
	case 2: z80_in(0, FB); return 0;
	case 3: {
		uint16_t n = B16(MBUF_IN_L), a2 = B16(MBUF_IN);
		if (n > FB) n = FB;
		for (int k = 0; k < n; k++) hal_wr((uint16_t)(a2 + k), fb[k]);
		return 0;
	}
	case 4: case 5: {                                // RCR: a few registers look right
		uint8_t bank = net_bank[LAN_PAR] & 3, r = net_bank[LAN_PAR + 1] & 0x1F;
		take_dgrams();
		if (r == 0x1D) return 0x01;                                  // ESTAT: CLKRDY
		if (r == 0x1C) return q_n ? 0x40 : 0;                        // EIR: PKTIF
		if (bank == 1 && r == 0x19) return (uint8_t)q_n;            // EPKTCNT
		if (bank == 3 && r == 0x12) return 0x06;                     // EREVID
		return 0;
	}
	case 6: return send_frame(B16(MBUF_OUT_L));
	case 7:                                          // 55: the next frame
		listen_all();
		take_dgrams();
		if (!q_n) { b16_set(MBUF_IN_L, 0); return 0; }
		memcpy(fb, q[q_h], q_len[q_h]);
		b16_set(MBUF_IN_L, q_len[q_h]);
		q_pop();
		return (uint8_t)(B16(MBUF_IN_L) >> 8);
	case 8: listen_all(); take_dgrams(); return (uint8_t)q_n;        // EPKTCNT
	case 9: return 0;                                // WCR / BFS / BFC: nothing to do
	case 10: {                                       // PHY: link up
		uint16_t v;
		switch (net_bank[LAN_PAR + 1] & 0x1F) {
		case 0x00: v = 0x0100; break;                // PHCON1: full duplex
		case 0x01: v = 0x1804 | 0x0004; break;       // PHSTAT1: LLSTAT
		case 0x02: v = 0x0083; break;
		case 0x03: v = 0x1400; break;
		case 0x10: v = 0x0100; break;
		case 0x11: v = 0x0400; break;                // PHSTAT2: LSTAT
		default: v = 0; break;
		}
		b16_set(UPD_DATA, v);
		return (uint8_t)(v >> 8);
	}
	case 11: {                                       // internet sum of fb + LAN_PAR, UPD_DATA bytes
		uint16_t o = B16(LAN_PAR), n = B16(UPD_DATA);
		if (o > FB) o = FB;
		if (n > FB - o) n = (uint16_t)(FB - o);
		b16_set(UPD_DATA, csum(fb + o, n));
		return 0;
	}
	case 12: {                                       // 60: IP
		uint16_t n = z80_in(34, FB - 34);
		out_ip(n, net_bank[PROTOCOL]);
		b16_set(MBUF_OUT_L, (uint16_t)(n + 34));
		return 0;
	}
	case 13: case 21: {                              // 61: UDP (69: and send)
		uint16_t n = z80_in(42, FB - 42);
		be16_set(fb + 34, B16(PORT_MY)); be16_set(fb + 36, B16(PORT_REMOTE));
		be16_set(fb + 38, (uint16_t)(n + 8)); fb[40] = fb[41] = 0;
		net_bank[PROTOCOL] = 0x11;
		out_ip((uint16_t)(n + 8), 17);
		b16_set(MBUF_OUT_L, (uint16_t)(n + 42));
		if (i == 21) return send_frame((uint16_t)(n + 42));
		return 0;
	}
	case 14: {                                       // 62: the answer to an ARP request in fb
		uint8_t t[10];
		memcpy(t, fb + 22, 10);
		eth_hdr(fb, t, net_bank + MAC_MY, 0x0806);
		be16_set(fb + 20, 2);
		memcpy(fb + 22, net_bank + MAC_MY, 6); memcpy(fb + 28, net_bank + IP_MY, 4);
		memcpy(fb + 32, t, 10);
		b16_set(MBUF_OUT_L, 42);
		return 0;
	}
	case 15: {                                       // 63: Ethernet
		uint16_t n = z80_in(14, FB - 14);
		eth_hdr(fb, net_bank + MAC_REMOTE, net_bank + MAC_MY, 0x0800);
		b16_set(MBUF_OUT_L, (uint16_t)(n + 14));
		return 0;
	}
	case 16: return classify(fb, FB);                // 64
	case 17: return 0x01;                            // 65: ESTAT
	case 18: listen_all(); return process();         // 66
	case 19: {                                       // 67: the data of the UDP frame in fb
		int dn = be16(fb + 38) - 8;
		if (dn < 0) dn = 0;
		if (dn > FB - 42) dn = FB - 42;
		uint16_t a2 = B16(MBUF_IN);
		for (int k = 0; k < dn; k++) hal_wr((uint16_t)(a2 + k), fb[42 + k]);
		b16_set(MBUF_IN_L, (uint16_t)dn);
		return 0;
	}
	case 20: {                                       // 68: an ARP request for IP_REMOTE (not sent)
		static const uint8_t bc[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
		eth_hdr(fb, bc, net_bank + MAC_MY, 0x0806);
		be16_set(fb + 14, 1); be16_set(fb + 16, 0x0800); fb[18] = 6; fb[19] = 4; be16_set(fb + 20, 1);
		memcpy(fb + 22, net_bank + MAC_MY, 6); memcpy(fb + 28, net_bank + IP_MY, 4);
		memset(fb + 32, 0, 6); memcpy(fb + 38, net_bank + IP_REMOTE, 4);
		b16_set(MBUF_OUT_L, 42);
		return 0;
	}
	case 22: {                                       // 70: ICMP echo request (not sent)
		uint16_t n = z80_in(42, FB - 42);
		fb[34] = 8; fb[35] = 0; fb[36] = fb[37] = 0; fb[38] = fb[39] = fb[40] = fb[41] = 0;
		be16_set(fb + 36, csum(fb + 34, n + 8));
		net_bank[PROTOCOL] = 1;
		out_ip((uint16_t)(n + 8), 1);
		b16_set(MBUF_OUT_L, (uint16_t)(n + 42));
		return 0;
	}
	case 23: {                                       // 71: the echo reply to the request in fb
		uint16_t n = B16(MBUF_IN_L);
		b16_set(MBUF_OUT_L, n);
		if (n < 42 || n > FB) return 0;
		uint8_t t[6];
		memcpy(t, fb, 6); memcpy(fb, fb + 6, 6); memcpy(fb + 6, t, 6);
		memcpy(t, fb + 26, 4); memcpy(fb + 26, fb + 30, 4); memcpy(fb + 30, t, 4);
		fb[24] = fb[25] = 0; be16_set(fb + 24, csum(fb + 14, 20));
		int ihl = (fb[14] & 15) * 4;
		fb[14 + ihl] = 0; fb[16 + ihl] = fb[17 + ihl] = 0;
		be16_set(fb + 16 + ihl, csum(fb + 14 + ihl, be16(fb + 16) - ihl));
		return 0;
	}
	case 24: return 0;                               // 72: TCP of 4.67 (broken segments): not supported
	default: return 0xFD;
	}
}

// ------------------------------------------------------------------ the CP/M channels (11.7)

// one character as one datagram, the port of the server on both ends; no IP = nothing
static void chan_out(const uint8_t *ip, uint16_t port, uint8_t c)
{
	if (!net_on() || !ip[0]) return;
	uint32_t t0 = hal_ms();
	while (esp_send(port, ip, port, &c, 1) && esp_up() && hal_ms() - t0 < 3000) esp_poll();
}

void net_con_out(uint8_t c) { chan_out(io_settings + 0x1C, (uint16_t)(io_settings[0x20] | io_settings[0x21] << 8), c); }
void net_list_out(uint8_t c) { chan_out(io_settings + 0x1C, (uint16_t)(io_settings[0x55] | io_settings[0x56] << 8), c); }
void net_punch_out(uint8_t c) { chan_out(net_pr_ip(), net_pr_port(), c); }

void net_in_poll(void) { if (net_on()) { esp_poll(); listen_all(); process(); } }

int net_reader_get(void)
{
	if (!net_reader_n) return -1;
	uint8_t c = net_reader[0];
	memmove(net_reader, net_reader + 1, --net_reader_n);
	return c;
}

// ------------------------------------------------------------------ start, poll

void net_init(void)
{
	memset(net_bank, 0, sizeof net_bank);
	q_n = 0; net_reader_n = 0; ping_busy = 0;
	fake_settings_done = 0;
	static int esp_started;
	if (!esp_started) { esp_started = 1; esp_init(); }   // the module keeps its links over a reset of CRISS
}

// the settings of the NVRAM image are there (nvram.c): PR_SRV, the LST IOBYTE
void net_settings(void)
{
	for (int i = 0; i < 12; i++) io_sysvar_set((uint8_t)(74 + i), io_settings[0x22 + i]);
	io_sysvar_set(88, io_settings[0x54]);
	memcpy(net_bank + MAC_MY, io_settings + 0x02, 6);
	memcpy(net_bank + IP_MY, io_settings + 0x08, 4);
	fake_settings_done = 0;
}

void net_poll(void)
{
	esp_poll();
	// the addresses of the module are ours (the IP of the settings would not be reached)
	if (esp_up() && esp_ip()[0] && !fake_settings_done) {
		memcpy(net_bank + IP_MY, esp_ip(), 4);
		memcpy(net_bank + MAC_MY, esp_mac(), 6);
		fake_settings_done = 1;
	}
}
