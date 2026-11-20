// Ф1 step 3: the SoC RTL (criss_soc + criss_sd) on pin models of the SDRAM / SRAM (from core-doom
// tb_rtl.cpp) and of the kg_mcu2 file mounter; the helper firmware and the data ROM preloaded into
// the SDRAM, a floppy image in slot 0. Runs until a text appears on the Z80 screen (F800) or the limit.
//   tb_soc <criss_fw.bin> <criss_data.bin> <a.cpm> [--until "a>"] [--keys "DIR\r"] [--then "XONIX"] [--max Mcycles]
// --keys: \r = Enter, ~ = a pause of 50 ms
#include "Vsim_top.h"
#include "verilated.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const uint32_t SRAM_SIZE = 4u << 20, SDRAM_SIZE = 32u << 20;
static std::vector<uint8_t> sram(SRAM_SIZE), sdram(SDRAM_SIZE), disk;

static std::vector<uint8_t> load(const char *fn)
{
	FILE *f = fopen(fn, "rb");
	if (!f) { perror(fn); exit(2); }
	std::vector<uint8_t> v;
	int c;
	while ((c = fgetc(f)) != EOF) v.push_back((uint8_t)c);
	fclose(f);
	return v;
}

static uint8_t z80mem(uint16_t a) { return sram[a]; }   // Z80 byte a = SRAM byte a (lane = A0)

static std::string screen(int lines = 24, int chars = 80)
{
	std::string s;
	for (int y = 0; y < lines; y++) {
		std::string l;
		for (int x = 0; x < chars; x++) {
			uint8_t c = z80mem(0xF800 + y * chars + x);
			l += (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
		}
		while (!l.empty() && l.back() == ' ') l.pop_back();
		s += "|" + l + "\n";
	}
	return s;
}

int main(int argc, char **argv)
{
	Verilated::commandArgs(argc, argv);
	if (argc < 4) { fprintf(stderr, "usage: tb_soc fw.bin data.bin a.cpm [--until s] [--keys s] [--then s] [--max M]\n"); return 2; }
	auto fw = load(argv[1]), rom = load(argv[2]);
	disk = load(argv[3]);
	memcpy(&sdram[0], fw.data(), fw.size());
	memcpy(&sdram[0x100000], rom.data(), rom.size());
	std::string until = "a>", keys, then_s;
	uint64_t max_cycles = 400000000ull;
	for (int i = 4; i < argc; i++) {
		if (!strcmp(argv[i], "--until")) until = argv[++i];
		else if (!strcmp(argv[i], "--keys")) {
			for (const char *k = argv[++i]; *k; k++)
				if (k[0] == '\\' && k[1] == 'r') { keys += '\r'; k++; } else keys += *k;
		}
		else if (!strcmp(argv[i], "--then")) then_s = argv[++i];
		else if (!strcmp(argv[i], "--max")) max_cycles = strtoull(argv[++i], 0, 0) * 1000000ull;
	}
	// keys -> PS/2 through the table of the data ROM (page 0 plain, page 1 Shift)
	std::vector<uint8_t> ps2;
	for (char ch : keys) {
		if (ch == '~') { ps2.push_back(0); continue; }   // a pause of 50 ms (0 is never a PS/2 byte sent)
		bool ok = false;
		for (int sh = 0; sh < 2 && !ok; sh++)
			for (int sc = 1; sc < 0x80 && !ok; sc++)
				if (rom[0x18000 + sh * 256 + sc] == (uint8_t)ch) {
					if (sh) ps2.push_back(0x12);
					ps2.push_back(sc); ps2.push_back(0xF0); ps2.push_back(sc);
					if (sh) { ps2.push_back(0xF0); ps2.push_back(0x12); }
					ok = true;
				}
	}

	Vsim_top *t = new Vsim_top;
	t->reset = 1; t->ps2_wr = 0; t->sdc_img_mounted = 0; t->sdc_busy = 0; t->sdc_done = 0; t->sdc_error = 0;
	t->sdc_byte_in_strobe = 0;
	bool open[4] = {false, false, false, false};
	uint32_t row[4] = {0, 0, 0, 0};
	int64_t act_at[4] = {-100, -100, -100, -100}, pre_at[4] = {-100, -100, -100, -100};
	struct Rd { int64_t at; uint32_t addr; };
	std::vector<Rd> rd;
	int errors = 0;
	uint32_t prev_ma = 0;
	uint64_t cyc = 0;
	auto err = [&](const char *m, int b) { if (errors++ < 20) fprintf(stderr, "SDRAM %s bank %d at %llu\n", m, b, (unsigned long long)cyc); };

	// mounter model
	enum { M_IDLE, M_TAKE, M_RD, M_WR, M_DONE } ms = M_IDLE;
	int m_wait = 0, m_i = 0, m_slot = 0, m_write = 0;
	uint32_t m_sec = 0;
	std::vector<int> wr_addr;   // byte addresses put on sdc_byte_addr (write), data 3 clocks later
	uint64_t found_at = 0, reads = 0, writes = 0;
	size_t kpos = 0;
	uint64_t next_key = 0;
	bool phase2 = false;

	for (int i = 0; i < 16; i++) { t->clk = 0; t->eval(); t->clk = 1; t->eval(); }
	t->reset = 0;
	while (cyc < max_cycles) {
		t->sdr_dq_i = 0xdead;
		for (auto &r : rd) if (cyc == (uint64_t)r.at + 2) { uint16_t v; memcpy(&v, &sdram[r.addr * 2], 2); t->sdr_dq_i = v; }
		// mounter: its outputs for this clock (set before the edge), advanced after it
		t->sdc_img_mounted = (cyc == 50) ? 1 : 0;
		t->sdc_img_size = disk.size();
		t->sdc_done = 0; t->sdc_byte_in_strobe = 0;
		switch (ms) {
		case M_IDLE:
			if (t->sdc_rd | t->sdc_wr) {
				uint8_t m = t->sdc_rd | t->sdc_wr;
				m_slot = __builtin_ctz(m);
				m_write = (t->sdc_wr >> m_slot) & 1;
				m_sec = t->sdc_sector;
				t->sdc_slot = m_slot;
				t->sdc_busy = 1;
				ms = M_TAKE; m_wait = 3;
			}
			break;
		case M_RD:
			if (m_i < 512) {
				uint32_t o = m_sec * 512 + m_i;
				t->sdc_byte_in_strobe = 1;
				t->sdc_byte_addr = m_i;
				t->sdc_byte_in_data = o < disk.size() ? disk[o] : 0;
			}
			break;
		case M_WR:
			t->sdc_byte_addr = m_i < 512 ? m_i : 511;
			break;
		case M_DONE:
			t->sdc_done = 1; t->sdc_busy = 0;
			break;
		default: break;
		}
		t->clk = 0; t->eval();
		t->clk = 1; t->eval();
		cyc++;
		switch (ms) {
		case M_TAKE: if (--m_wait == 0) { ms = m_write ? M_WR : M_RD; m_i = 0; } break;
		case M_RD: if (++m_i > 512) { ms = M_DONE; reads++; } break;
		case M_WR:
			// criss_sd gives the byte two edges after its address (kg_l2_img adds its address register:
			// IMG_OUT_LATENCY 3): after edge j the output holds byte j - 1
			if (m_i >= 1 && m_i <= 512) { uint32_t o = m_sec * 512 + (m_i - 1); if (o < disk.size()) disk[o] = t->sdc_byte_out_data; }
			if (++m_i == 513) { ms = M_DONE; writes++; }
			break;
		case M_DONE: ms = M_IDLE; break;
		default: break;
		}

		// SDRAM model (commands after this edge)
		uint8_t c = (t->sdr_ras_n << 2) | (t->sdr_cas_n << 1) | t->sdr_we_n;
		int b = t->sdr_ba;
		uint32_t a = t->sdr_a;
		int64_t k = (int64_t)cyc;
		switch (c) {
		case 3: if (open[b]) err("ACTIVE to an open bank", b); if (k - pre_at[b] < 2) err("tRP", b); open[b] = true; row[b] = a; act_at[b] = k; break;
		case 5: case 4: {
			if (!open[b]) { err("READ/WRITE to a closed bank", b); break; }
			if (k - act_at[b] < 2) err("tRCD", b);
			uint32_t addr = ((uint32_t)b << 22) | (row[b] << 9) | (a & 0x1ff);
			if (c == 5) rd.push_back({k, addr});
			else {
				uint16_t v = t->sdr_dq_o;
				if (!(t->sdr_dqm & 1)) sdram[addr * 2] = v & 0xff;
				if (!(t->sdr_dqm & 2)) sdram[addr * 2 + 1] = v >> 8;
			}
			if (a & 0x400) { open[b] = false; pre_at[b] = k + 1; }
			break;
		}
		case 2: if (a & 0x400) { for (int i = 0; i < 4; i++) { open[i] = false; pre_at[i] = k; } } else { open[b] = false; pre_at[b] = k; } break;
		default: break;
		}
		rd.erase(std::remove_if(rd.begin(), rd.end(), [&](const Rd &r) { return k > r.at + 3; }), rd.end());
		// SRAM
		uint32_t ma = t->MA & 0x1fffff;
		if (!(t->MWR_N & 1) && !(t->MD_OE_N & 0xff)) sram[ma * 2] = t->MD_O & 0xff;
		if (!(t->MWR_N & 2) && !(t->MD_OE_N & 0xff00)) sram[ma * 2 + 1] = t->MD_O >> 8;
		{ uint16_t v; memcpy(&v, &sram[ma * 2], 2); t->MD_I = v; }
		prev_ma = ma;
		// keys: one PS/2 byte every 300000 clocks (4 ms) after the first text (one character register, as CRISS)
		t->ps2_wr = 0;
		if (phase2 && kpos < ps2.size() && cyc >= next_key) {
			if (ps2[kpos] == 0) { kpos++; next_key = cyc + 3750000; }
			else { t->ps2_code = ps2[kpos++]; t->ps2_wr = 1; next_key = cyc + 300000; }
		}
		if ((cyc & 0xFFFF) == 0) {
			std::string s = screen();
			if (!phase2 && s.find(until) != std::string::npos) {
				found_at = cyc;
				printf("found \"%s\" at %llu clocks (%.1f ms at 75 MHz), %llu sector reads\n", until.c_str(),
					(unsigned long long)cyc, cyc / 75000.0, (unsigned long long)reads);
				if (keys.empty() || then_s.empty()) break;
				phase2 = true; next_key = cyc;
			} else if (phase2 && kpos >= ps2.size() && s.find(then_s) != std::string::npos) {
				printf("found \"%s\" at %llu clocks\n", then_s.c_str(), (unsigned long long)cyc);
				found_at = cyc;
				break;
			}
		}
	}
	printf("---- Z80 screen (vstat %02X cursor %04X, pc %04X, state %02X) after %llu clocks, %d SDRAM errors, %llu reads %llu writes\n",
		t->vstat, t->cursor, t->dbg_z80_pc, t->dbg_state, (unsigned long long)cyc, errors,
		(unsigned long long)reads, (unsigned long long)writes);
	printf("%s", screen().c_str());
	bool then_ok = then_s.empty() || (phase2 && screen().find(then_s) != std::string::npos);
	bool ok = found_at && errors == 0 && then_ok;
	printf("%s\n", ok ? "PASS" : "FAIL");
	delete t;
	return ok ? 0 : 1;
}
