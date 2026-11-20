// The AT engine of the ESP8266 (stock firmware 1.7.x, docs/PLAN.md Ф4 step 2). Never blocks: one
// stream parser for every byte from the module (lines, the "> " prompt, +IPD with its data, which can
// come in the middle of any wait), one command in flight with a time-out, a queue of jobs (open a UDP
// link, send a datagram, ping). Only volatile commands: the flash of the module is shared by all cores.
#include "esp.h"
#include "hal.h"
#include <string.h>

#define NLINK 5                  // CIPMUX=1: links 0..4, one per our UDP port, the oldest reused
#define NJOB 6
#define NRX 8

// ------------------------------------------------------------------ state

static int up;
static uint8_t my_ip[4], my_mac[6];

static uint16_t link_port[NLINK];
static uint8_t link_open[NLINK];
static uint32_t link_use[NLINK];

enum { J_LISTEN, J_SEND, J_PING };
typedef struct {
	uint8_t type;
	uint16_t lport, rport;
	uint8_t ip[4];
	uint16_t n;
	uint8_t d[ESP_DATA];
} job_t;
static job_t jobs[NJOB];
static int job_h, job_n;

static esp_dgram_t rxq[NRX];
static int rx_h, rx_n;

// the module's transmit ring (the UART takes a byte about every 87 us)
static uint8_t txb[2048];
static uint16_t tx_w, tx_r;

static void out(const void *p, int n)
{
	const uint8_t *s = p;
	while (n--) {
		uint16_t w = (tx_w + 1) & 2047;
		while (w == tx_r) { if (hal_esp_put(txb[tx_r]) == 0) tx_r = (tx_r + 1) & 2047; }
		txb[tx_w] = *s++;
		tx_w = w;
	}
}

static void tx_pump(void)
{
	while (tx_r != tx_w && hal_esp_put(txb[tx_r]) == 0) tx_r = (tx_r + 1) & 2047;
}

// ------------------------------------------------------------------ commands and their answers

enum { W_NONE, W_OK, W_PROMPT, W_SEND, W_PING };
static int wait, result, timeouts;      // result: 0 waiting, 1 ok, -1 error / time-out
static uint32_t wait_t, wait_to;
static int need_cifsr;

static char cbuf[64];
static int cn;
static void c_s(const char *s) { while (*s && cn < (int)sizeof cbuf - 3) cbuf[cn++] = *s++; }
static void c_u(unsigned v)
{
	char t[6]; int n = 0;
	do t[n++] = (char)('0' + v % 10); while ((v /= 10) && n < 5);
	while (n && cn < (int)sizeof cbuf - 3) cbuf[cn++] = t[--n];
}
static void c_ip(const uint8_t *ip) { c_s("\""); for (int i = 0; i < 4; i++) { if (i) c_s("."); c_u(ip[i]); } c_s("\""); }
static void c_go(int w, uint32_t to)
{
	cbuf[cn++] = '\r'; cbuf[cn++] = '\n';
	out(cbuf, cn);
	cn = 0;
	wait = w; result = 0; wait_t = hal_ms(); wait_to = to;
}
static void cmd(const char *s, int w, uint32_t to) { cn = 0; c_s(s); c_go(w, to); }

static int ping_ok;

static int parse_dec(const char **p)
{
	int v = 0, any = 0;
	while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; any = 1; }
	return any ? v : -1;
}

static void parse_ip(const char *s, uint8_t *ip)
{
	for (int i = 0; i < 4; i++) {
		int v = parse_dec(&s);
		ip[i] = (uint8_t)(v < 0 ? 0 : v);
		if (*s == '.') s++;
	}
}

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; }

static void links_reset(void)
{
	memset(link_open, 0, sizeof link_open);
	memset(link_port, 0, sizeof link_port);
}

static int st;                    // ST_*
enum { ST_BOOT, ST_INIT, ST_IDLE, ST_JOB };
static int sub, init_i, cur_link;
static uint32_t boot_t;

static void on_line(const char *l)
{
	if (!strcmp(l, "OK")) {
		timeouts = 0;
		if (wait == W_OK || wait == W_PING) result = 1;
	} else if (!strcmp(l, "ERROR") || !strcmp(l, "FAIL") || !strcmp(l, "SEND FAIL")) {
		timeouts = 0;
		if (wait != W_NONE) result = -1;
	} else if (!strcmp(l, "SEND OK")) {
		if (wait == W_SEND) result = 1;
	} else if (!strcmp(l, "ready")) {                 // the module restarted: all links gone
		up = 0; links_reset();
		st = ST_BOOT; wait = W_NONE; boot_t = hal_ms() - 1000;
	} else if (!strcmp(l, "WIFI GOT IP")) {
		need_cifsr = 1;
	} else if (!strcmp(l, "WIFI DISCONNECT")) {
		memset(my_ip, 0, 4);
	} else if (!strncmp(l, "+CIFSR:STAIP,\"", 14)) {
		parse_ip(l + 14, my_ip);
	} else if (!strncmp(l, "+CIFSR:STAMAC,\"", 15)) {
		const char *s = l + 15;
		for (int i = 0; i < 6 && s[0] && s[1]; i++, s += 3) my_mac[i] = (uint8_t)(hexv(s[0]) << 4 | hexv(s[1]));
	} else if (wait == W_PING && l[0] == '+') {
		ping_ok = l[1] >= '0' && l[1] <= '9';
	} else if (l[0] >= '0' && l[0] <= '4' && !strcmp(l + 1, ",CLOSED")) {
		link_open[l[0] - '0'] = 0;
	}
}

// the parser: one byte from the module
static char line[160];
static int line_n;
static int ipd_left;
static esp_dgram_t *ipd;

static void feed(uint8_t c)
{
	if (ipd_left) {
		if (ipd && ipd->len < ESP_DATA) ipd->d[ipd->len++] = c;
		if (--ipd_left == 0 && ipd) rx_n++;
		return;
	}
	if (c == '\n') {
		line[line_n] = 0;
		on_line(line);
		line_n = 0;
		return;
	}
	if (c == '\r') return;
	if (line_n < (int)sizeof line - 1) line[line_n++] = (char)c;
	if (line_n == 1 && c == '>' && wait == W_PROMPT) { result = 1; return; }
	if (c == ':' && line_n > 5 && !strncmp(line, "+IPD,", 5)) {
		// +IPD,<link>,<len>,<ip>,<port>:
		line[line_n - 1] = 0;
		const char *s = line + 5;
		int lk = parse_dec(&s); if (*s == ',') s++;
		int len = parse_dec(&s); if (*s == ',') s++;
		uint8_t ip[4]; parse_ip(s, ip);
		while (*s && *s != ',') s++;
		if (*s == ',') s++;
		int port = parse_dec(&s);
		line_n = 0;
		if (len <= 0) return;
		ipd_left = len;
		ipd = 0;
		if (rx_n < NRX && lk >= 0 && lk < NLINK) {
			ipd = &rxq[(rx_h + rx_n) % NRX];
			ipd->lport = link_port[lk];
			ipd->rport = (uint16_t)(port < 0 ? 0 : port);
			memcpy(ipd->ip, ip, 4);
			ipd->len = 0;
		}
	}
}

// ------------------------------------------------------------------ jobs

static int link_of(uint16_t port)
{
	for (int i = 0; i < NLINK; i++) if (link_open[i] && link_port[i] == port) return i;
	return -1;
}

static int pick_link(void)
{
	int best = 0;
	for (int i = 0; i < NLINK; i++) {
		if (!link_open[i]) return i;
		if ((int32_t)(link_use[i] - link_use[best]) < 0) best = i;
	}
	return best;
}

static void job_done(void) { job_h = (job_h + 1) % NJOB; job_n--; st = ST_IDLE; }

static void step_job(void)
{
	job_t *j = &jobs[job_h];
	int ok = result == 1;
	switch (sub) {
	case 0:
		if (j->type == J_PING) {
			cn = 0; c_s("AT+PING="); c_ip(j->ip); ping_ok = 0; c_go(W_PING, 6000);
			sub = 50;
			return;
		}
		cur_link = link_of(j->lport);
		if (cur_link >= 0) {
			link_use[cur_link] = hal_ms();
			if (j->type == J_LISTEN) { job_done(); return; }
			sub = 3; step_job();
			return;
		}
		cur_link = pick_link();
		if (link_open[cur_link]) {
			cn = 0; c_s("AT+CIPCLOSE="); c_u((unsigned)cur_link); c_go(W_OK, 2000);
			sub = 1;
			return;
		}
		/* fall through */
	case 1:
		link_open[cur_link] = 0;
		cn = 0; c_s("AT+CIPSTART="); c_u((unsigned)cur_link); c_s(",\"UDP\",\"0.0.0.0\",0,"); c_u(j->lport); c_s(",2");
		c_go(W_OK, 3000);
		sub = 2;
		return;
	case 2:
		if (!ok) { job_done(); return; }
		link_open[cur_link] = 1; link_port[cur_link] = j->lport; link_use[cur_link] = hal_ms();
		if (j->type == J_LISTEN) { job_done(); return; }
		/* fall through */
	case 3:
		cn = 0; c_s("AT+CIPSEND="); c_u((unsigned)cur_link); c_s(","); c_u(j->n); c_s(","); c_ip(j->ip); c_s(","); c_u(j->rport);
		c_go(W_PROMPT, 2000);
		sub = 4;
		return;
	case 4:
		if (!ok) { job_done(); return; }
		out(j->d, j->n);
		wait = W_SEND; result = 0; wait_t = hal_ms(); wait_to = 3000;
		sub = 5;
		return;
	case 5:
		job_done();
		return;
	case 50:
		net_ping_done(j->ip, ok && ping_ok);
		job_done();
		return;
	}
}

static const char *const init_cmd[] = { "ATE0", "AT+CIPCLOSE=5", "AT+CIPMUX=1", "AT+CIPDINFO=1", "AT+CIFSR" };

static void step(void)
{
	switch (st) {
	case ST_BOOT:
		if (wait == W_OK && result == 1) { st = ST_INIT; init_i = 0; wait = W_NONE; step(); return; }
		wait = W_NONE;
		if (hal_ms() - boot_t < 1000) return;      // probe once a second
		boot_t = hal_ms();
		cmd("AT", W_OK, 500);
		return;
	case ST_INIT:
		if (init_i == (int)(sizeof init_cmd / sizeof init_cmd[0])) {
			wait = W_NONE; up = 1; links_reset(); need_cifsr = 0; st = ST_IDLE;
			return;
		}
		cmd(init_cmd[init_i++], W_OK, 2000);
		return;
	case ST_IDLE:
		wait = W_NONE;
		if (need_cifsr) { need_cifsr = 0; cmd("AT+CIFSR", W_OK, 2000); return; }
		if (!job_n) return;
		st = ST_JOB; sub = 0;
		/* fall through */
	case ST_JOB:
		step_job();
		return;
	}
}

// ------------------------------------------------------------------ interface

void esp_init(void)
{
	up = 0; links_reset();
	job_n = 0; rx_n = 0; ipd_left = 0; line_n = 0;
	wait = W_NONE; timeouts = 0;
	st = ST_BOOT; boot_t = hal_ms() - 1000;
	memset(my_ip, 0, 4);
}

void esp_poll(void)
{
	int c, k = 0;
	while (k++ < 4096 && (c = hal_esp_get()) >= 0) feed((uint8_t)c);
	tx_pump();
	if (wait != W_NONE && result == 0) {
		if (hal_ms() - wait_t < wait_to) return;
		result = -1;                                // time-out
		if (st != ST_BOOT && ++timeouts >= 3) {     // the module is gone: probe again
			up = 0; links_reset(); st = ST_BOOT; wait = W_NONE; timeouts = 0;
			return;
		}
	}
	step();
}

int esp_up(void) { return up; }
const uint8_t *esp_ip(void) { return my_ip; }
const uint8_t *esp_mac(void) { return my_mac; }

static job_t *job_new(int type)
{
	if (!up || job_n == NJOB) return 0;
	job_t *j = &jobs[(job_h + job_n) % NJOB];
	j->type = (uint8_t)type;
	return j;
}

void esp_listen(uint16_t lport)
{
	if (!up || !lport || link_of(lport) >= 0) return;
	for (int i = 0; i < job_n; i++) {
		job_t *j = &jobs[(job_h + i) % NJOB];
		if (j->type != J_PING && j->lport == lport) return;
	}
	job_t *j = job_new(J_LISTEN);
	if (!j) return;
	j->lport = lport;
	job_n++;
}

int esp_send(uint16_t lport, const uint8_t ip[4], uint16_t rport, const uint8_t *d, uint16_t n)
{
	job_t *j = job_new(J_SEND);
	if (!j) return -1;
	if (n > ESP_DATA) n = ESP_DATA;
	j->lport = lport; j->rport = rport; memcpy(j->ip, ip, 4);
	j->n = n; memcpy(j->d, d, n);
	job_n++;
	return 0;
}

int esp_ping(const uint8_t ip[4])
{
	job_t *j = job_new(J_PING);
	if (!j) return -1;
	memcpy(j->ip, ip, 4);
	job_n++;
	return 0;
}

int esp_rx_n(void) { return rx_n; }
esp_dgram_t *esp_rx_head(void) { return rx_n ? &rxq[rx_h] : 0; }
void esp_rx_pop(void) { if (rx_n) { rx_h = (rx_h + 1) % NRX; rx_n--; } }
void esp_rx_clear(void) { rx_n = 0; ipd = 0; }   // (a datagram coming in is dropped)
