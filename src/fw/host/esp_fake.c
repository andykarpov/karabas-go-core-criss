// A made-up ESP8266 for the PC build (--esp): the AT commands the firmware uses (esp.c), on real UDP
// sockets of 127.0.0.2 (its IP: the peers of a test bind 127.0.0.1 with the same ports); a ping answers for 127.x.x.x only. Starts with the
// garbage of the boot loader and "ready", as the module does. Included by host.c.
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

static int esp_on;
static int esp_sock[5] = { -1, -1, -1, -1, -1 };
static uint8_t eo[65536];              // to the firmware
static unsigned eo_w, eo_r;
static char el[256];
static int el_n;
static int ed_left, ed_link, ed_port;   // CIPSEND data
static uint8_t ed[2048];
static int ed_n;
static struct in_addr ed_ip;

static void eo_s(const char *s) { while (*s) { eo[eo_w++ & 0xFFFF] = (uint8_t)*s++; } }
static void eo_b(const uint8_t *d, int n) { while (n--) eo[eo_w++ & 0xFFFF] = *d++; }

static void esp_fake_start(void)
{
	esp_on = 1;
	eo_s("\x18\x02r\x8c\xfe garbage of the boot loader\r\n\r\nready\r\nWIFI CONNECTED\r\nWIFI GOT IP\r\n");
}

static void esp_cmd(char *l)
{
	char r[300];
	int k, n, port, lport;
	char ip[64];
	if (!strcmp(l, "AT") || !strcmp(l, "ATE0") || !strncmp(l, "AT+CIPMUX=", 10) || !strncmp(l, "AT+CIPDINFO=", 12)) eo_s("\r\nOK\r\n");
	else if (!strcmp(l, "AT+CIFSR")) eo_s("+CIFSR:STAIP,\"127.0.0.2\"\r\n+CIFSR:STAMAC,\"02:11:22:33:44:55\"\r\n\r\nOK\r\n");
	else if (sscanf(l, "AT+CIPCLOSE=%d", &k) == 1) {
		for (int i = 0; i < 5; i++)
			if ((k == 5 || k == i) && esp_sock[i] >= 0) { close(esp_sock[i]); esp_sock[i] = -1; snprintf(r, sizeof r, "%d,CLOSED\r\n", i); eo_s(r); }
		eo_s(k == 5 || (k >= 0 && k < 5) ? "\r\nOK\r\n" : "\r\nERROR\r\n");
	} else if (sscanf(l, "AT+CIPSTART=%d,\"UDP\",\"%63[^\"]\",%d,%d,2", &k, ip, &port, &lport) == 4 && k >= 0 && k < 5 && esp_sock[k] < 0) {
		int s = socket(AF_INET, SOCK_DGRAM, 0);
		struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)lport) };
		a.sin_addr.s_addr = htonl(0x7F000002);
		int one = 1;
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) { close(s); eo_s("\r\nERROR\r\n"); return; }
		fcntl(s, F_SETFL, O_NONBLOCK);
		esp_sock[k] = s;
		snprintf(r, sizeof r, "%d,CONNECT\r\n\r\nOK\r\n", k); eo_s(r);
	} else if (sscanf(l, "AT+CIPSEND=%d,%d,\"%63[^\"]\",%d", &k, &n, ip, &port) == 4 && k >= 0 && k < 5 && esp_sock[k] >= 0 && n > 0 && n <= 2048) {
		ed_left = n; ed_n = 0; ed_link = k; ed_port = port; inet_aton(ip, &ed_ip);
		eo_s("\r\nOK\r\n> ");
	} else if (sscanf(l, "AT+PING=\"%63[^\"]\"", ip) == 1) {
		eo_s(!strncmp(ip, "127.", 4) ? "+1\r\n\r\nOK\r\n" : "+timeout\r\n\r\nERROR\r\n");
	} else eo_s("\r\nERROR\r\n");
}

int hal_esp_put(uint8_t c)
{
	steps += 87;                      // the time of a byte at 115200 (hal_ms counts steps)
	if (!esp_on) return 0;
	if (ed_left) {
		ed[ed_n++] = c;
		if (--ed_left == 0) {
			struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)ed_port), .sin_addr = ed_ip };
			sendto(esp_sock[ed_link], ed, (size_t)ed_n, 0, (struct sockaddr *)&a, sizeof a);
			char r[64]; snprintf(r, sizeof r, "\r\nRecv %d bytes\r\n\r\nSEND OK\r\n", ed_n); eo_s(r);
		}
		return 0;
	}
	if (c == '\n') {
		if (el_n && el[el_n - 1] == '\r') el_n--;
		el[el_n] = 0; el_n = 0;
		esp_cmd(el);
	} else if (el_n < (int)sizeof el - 1) el[el_n++] = (char)c;
	return 0;
}

int hal_esp_get(void)
{
	steps += 1;
	if (!esp_on) return -1;
	if (eo_r == eo_w) {
		for (int k = 0; k < 5; k++) {
			if (esp_sock[k] < 0) continue;
			uint8_t d[2048];
			struct sockaddr_in a; socklen_t al = sizeof a;
			ssize_t n = recvfrom(esp_sock[k], d, sizeof d, 0, (struct sockaddr *)&a, &al);
			if (n <= 0) continue;
			char h[96];
			snprintf(h, sizeof h, "\r\n+IPD,%d,%d,%s,%d:", k, (int)n, inet_ntoa(a.sin_addr), ntohs(a.sin_port));
			eo_s(h); eo_b(d, (int)n);
			break;
		}
		if (eo_r == eo_w) return -1;
	}
	return eo[eo_r++ & 0xFFFF];
}
