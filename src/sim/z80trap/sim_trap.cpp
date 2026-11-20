// Bench of criss_z80: 64K memory, the helper side of the traps, checks (src/sim/z80trap/run.sh).
#include "Vcriss_z80.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

static uint8_t mem[65536];
static int errors = 0;

// REG / DIR field access (T80 order: A F A' F' I R SP PC BC DE HL IX BC' DE' HL' IY IM IFF1 IFF2)
struct Regs { uint32_t w[7]; };
static unsigned get(const Regs &r, int lsb, int n) {
	unsigned v = 0;
	for (int i = 0; i < n; i++) { int b = lsb + i; v |= ((r.w[b >> 5] >> (b & 31)) & 1u) << i; }
	return v;
}
static void set(Regs &r, int lsb, int n, unsigned v) {
	for (int i = 0; i < n; i++) { int b = lsb + i; r.w[b >> 5] = (r.w[b >> 5] & ~(1u << (b & 31))) | (((v >> i) & 1u) << (b & 31)); }
}
enum { R_A = 0, R_F = 8, R_A2 = 16, R_F2 = 24, R_I = 32, R_R = 40, R_SP = 48, R_PC = 64, R_BC = 80, R_DE = 96,
	R_HL = 112, R_IX = 128, R_BC2 = 144, R_DE2 = 160, R_HL2 = 176, R_IY = 192 };

static void check(const char *what, unsigned got, unsigned exp) {
	if (got != exp) { printf("FAIL %s: %04X, expected %04X\n", what, got, exp); errors++; }
}

int main(int argc, char **argv) {
	Verilated::commandArgs(argc, argv);
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 2; }
	fread(mem, 1, sizeof mem, f);
	fclose(f);
	Vcriss_z80 *t = new Vcriss_z80;
	t->reset_n = 0; t->int_n = 1; t->nmi_n = 1; t->freeze_req = 1; t->resume = 0; t->dir_set = 0;
	unsigned div = 0, cyc = 0;
	std::vector<unsigned> codes;
	bool int_hold = false;
	int int_acks = 0;
	auto tick = [&]() {
		t->cen = (div == 0);
		div = (div + 1) % 5;            // the Z80 clock: clk / 5
		t->di = mem[t->a];
		t->clk = 0; t->eval();
		t->di = mem[t->a];
		t->clk = 1; t->eval();
		t->di = mem[t->a];
		t->eval();
		if (!t->mreq_n && !t->wr_n) mem[t->a] = t->dout;
		if (!t->m1_n && !t->iorq_n && int_hold) { int_hold = false; t->int_n = 1; int_acks++; }
		cyc++;
	};
	for (int i = 0; i < 20; i++) tick();
	t->reset_n = 1;
	while (cyc < 2000000 && mem[0x0825] == 0) {
		tick();
		if (!t->frozen) continue;
		Regs r; memcpy(r.w, t->reg_q, sizeof r.w);
		unsigned code = t->trap_code, pc = get(r, R_PC, 16);
		codes.push_back(code);
		Regs d = r;   // identity unless changed below
		switch (codes.size()) {
		case 1:       // boot: the freeze at PC 0
			check("boot code", code, 0xFF);
			check("boot pc", pc, 0x0000);
			t->freeze_req = 0;
			set(d, R_PC, 16, 0x0100); set(d, R_SP, 16, 0x0100);
			break;
		case 2:       // ED 02: all registers
			check("t1 code", code, 0x02);
			check("A", get(r, R_A, 8), 0x88); check("A'", get(r, R_A2, 8), 0x11);
			check("BC", get(r, R_BC, 16), 0x99AA); check("DE", get(r, R_DE, 16), 0xBBCC);
			check("HL", get(r, R_HL, 16), 0xDDEE); check("IX", get(r, R_IX, 16), 0x1357);
			check("IY", get(r, R_IY, 16), 0x2468); check("BC'", get(r, R_BC2, 16), 0x2233);
			check("DE'", get(r, R_DE2, 16), 0x4455); check("HL'", get(r, R_HL2, 16), 0x6677);
			check("SP", get(r, R_SP, 16), 0xF000);
			break;
		case 3:       // ED 0D: results
			check("t2 code", code, 0x0D);
			set(d, R_A, 8, 0x5A); set(d, R_HL, 16, 0x1234);
			set(d, R_BC, 16, (get(r, R_BC, 16) & 0xFF00) | 0x77);
			break;
		case 10:      // ED 10: move PC
			check("t9 code", code, 0x10);
			set(d, R_PC, 16, 0x0186);  // jumped (trap_test.lst)
			break;
		case 11:      // ED 03: A, then INT held low (must come after the release)
			check("t10 code", code, 0x03);
			set(d, R_A, 8, 0x42);
			int_hold = true; t->int_n = 0;
			break;
		default: break;
		}
		memcpy(t->dir_d, d.w, sizeof d.w);
		t->dir_set = 1; tick(); t->dir_set = 0;
		// write back must read back the same
		Regs q; memcpy(q.w, t->reg_q, sizeof q.w);
		for (int i = 0; i < 7; i++) if ((q.w[i] ^ d.w[i]) & (i == 6 ? 0xFFFFF : ~0u) & ~(i == 1 ? 0xFF00u : 0u)) {
			printf("FAIL trap %zu: REG after DIR word %d %08X != %08X\n", codes.size(), i, q.w[i], d.w[i]); errors++;
		}
		t->resume = 1; tick(); t->resume = 0;
		for (int i = 0; i < 4; i++) tick();
	}
	static const unsigned exp_codes[] = { 0xFF, 0x02, 0x0D, 0x05, 0x77, 0x7F, 0xA4, 0xA7, 0x27, 0x10, 0x03 };
	printf("traps:");
	for (unsigned c : codes) printf(" %02X", c);
	printf("\n");
	if (codes.size() != sizeof exp_codes / sizeof exp_codes[0]) { printf("FAIL trap count %zu\n", codes.size()); errors++; }
	else for (size_t i = 0; i < codes.size(); i++) check("trap order", codes[i], exp_codes[i]);
	auto w = [&](int o) { return mem[0x800 + o] | (mem[0x801 + o] << 8); };
	check("done", mem[0x0825], 0xAA);
	check("res A", mem[0x800], 0x88); check("res BC", w(1), 0x99AA); check("res DE", w(3), 0xBBCC);
	check("res HL", w(5), 0xDDEE); check("res IX", w(7), 0x1357); check("res IY", w(9), 0x2468);
	check("res A'", mem[0x800 + 11], 0x11); check("res BC'", w(12), 0x2233); check("res DE'", w(14), 0x4455);
	check("res HL'", w(16), 0x6677);
	check("res t2 A", mem[0x800 + 18], 0x5A); check("res t2 HL", w(19), 0x1234); check("res t2 BC", w(21), 0x2277);
	check("scratch (ED ED 02)", mem[0x820], 0x3C);
	check("NEG", mem[0x800 + 23], 0xFF); check("jumped", mem[0x800 + 24], 0xC3);
	check("A at INT", mem[0x800 + 25], 0x42);
	if (mem[0x800 + 26] < 1 || int_acks < 1) { printf("FAIL interrupt not taken (%d, acks %d)\n", mem[0x826], int_acks); errors++; }
	printf("%u clocks, %s (%d errors)\n", cyc, errors ? "FAIL" : "PASS", errors);
	delete t;
	return errors ? 1 : 0;
}
