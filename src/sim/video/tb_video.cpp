// criss_video alone: the screen (F800.. of a Z80 RAM dump of fw/host --dump-mem) and the fonts of the
// data ROM written as the SoC does, two frames run, the second saved as a PPM (de area, 640 x 480).
//   tb_video <mem dump> <criss_data.bin> <out.ppm> [vstat]
#include "Vcriss_video.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char **argv)
{
	Verilated::commandArgs(argc, argv);
	FILE *f = fopen(argv[1], "rb");
	std::vector<uint8_t> mem(65536 + 3 + 2048);
	fread(mem.data(), 1, mem.size(), f); fclose(f);
	f = fopen(argv[2], "rb");
	std::vector<uint8_t> rom(0x18A00);
	fread(rom.data(), 1, rom.size(), f); fclose(f);
	Vcriss_video *t = new Vcriss_video;
	auto sys = [&]() { t->clk_sys = 0; t->eval(); t->clk_sys = 1; t->eval(); };
	t->scr_we = 0; t->font_we = 0; t->col_we = 0;
	t->vstat_i = argc > 4 ? strtoul(argv[4], 0, 0) : mem[65536];
	t->cursor_i = mem[65537] | mem[65538] << 8;
	for (int i = 0; i < 2048; i++) { t->scr_we = 1; t->scr_wa = i; t->scr_wd = mem[0xF800 + i]; sys(); }
	for (int i = 0; i < 2048; i++) { t->col_we = 1; t->col_wa = i; t->col_wd = mem[65539 + i]; sys(); }
	t->col_we = 0;
	for (int i = 0; i < 5 * 5120; i++) { t->font_we = 1; t->font_wa = i; t->font_wd = rom[0x10000 + i]; sys(); }
	t->scr_we = 0; t->font_we = 0; sys();
	std::vector<uint8_t> img(640 * 480 * 3);
	int frame = 0, x = 0, y = 0, prev_vs = 0, prev_de = 0, prev_hs = 0, hs_n = 0, vs_n = 0;
	for (long c = 0; c < 3L * 800 * 525 && frame < 3; c++) {
		t->clk_pix = 0; t->eval(); t->clk_pix = 1; t->eval();
		if (t->clk_sys) sys();
		if (t->vs && !prev_vs) { frame++; y = 0; vs_n++; }
		if (t->hs && !prev_hs) hs_n++;
		if (frame == 2 && t->de) {
			if (x < 640 && y < 480) { uint32_t p = t->rgb; img[(y * 640 + x) * 3] = p >> 16; img[(y * 640 + x) * 3 + 1] = p >> 8; img[(y * 640 + x) * 3 + 2] = p; }
			x++;
		}
		if (!t->de && prev_de) { x = 0; y++; }
		prev_vs = t->vs; prev_de = t->de; prev_hs = t->hs;
	}
	f = fopen(argv[3], "wb");
	fprintf(f, "P6 640 480 255\n");
	fwrite(img.data(), 1, img.size(), f);
	fclose(f);
	printf("vstat %02X cursor %04X, hs %d vs %d\n", (unsigned)t->vstat_i, (unsigned)t->cursor_i, hs_n, vs_n);
	return 0;
}
