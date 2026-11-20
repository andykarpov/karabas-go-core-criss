// criss_esp loopback (txd -> rxd): 300 bytes out, all back in order, CTS while more than 1792 wait.
#include "Vcriss_esp.h"
#include <cstdio>
int main()
{
	Vcriss_esp t;
	t.reset = 1; t.rxd = 1; t.tx_wr = 0; t.rx_rd = 0;
	auto clk = [&]() { t.clk = 0; t.eval(); t.clk = 1; t.eval(); t.rxd = t.txd; };
	for (int i = 0; i < 4; i++) clk();
	t.reset = 0;
	int sent = 0, got = 0, bad = 0;
	long cyc = 0;
	while (got < 300 && cyc < 300L * 651 * 12) {
		if (sent < 300 && !t.tx_busy && !t.tx_wr) { t.tx_data = (uint8_t)(sent * 7 + 3); t.tx_wr = 1; sent++; }
		else t.tx_wr = 0;
		if (t.rx_valid && !t.rx_rd && (cyc % 40) == 0) {
			if (t.rx_data != (uint8_t)(got * 7 + 3)) bad++;
			got++; t.rx_rd = 1;
		} else t.rx_rd = 0;
		clk(); cyc++;
	}
	printf("sent %d got %d bad %d over %d cts_n %d\n", sent, got, bad, t.rx_over, t.cts_n);
	return !(got == 300 && bad == 0 && !t.rx_over);
}
