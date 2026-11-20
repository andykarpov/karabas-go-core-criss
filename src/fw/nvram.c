// The clock (DS1305 of CRISS: FE 80..87, docs/re/cpu-io.md 5.2) on the clock of the board, its 96-byte
// RAM (the settings) and the two 64 KB RAM snapshots of the 25LC1024 (docs/re/cpu-core.md 5.4) in the
// NVRAM image of the OSD item NVRAM (slot NV_SLOT, /criss/criss.nvr, NV_SECTORS * 512 bytes; make nvr).
// (The backup RAM file of the board firmware is made only after a file loader load: not for CRISS.)
//   sector 0             magic, the 96 settings bytes
//   1 + n * 130 .. +127  snapshot n (0, 1): the Z80 RAM 0000..FFFF
//   +128, +129           its state: registers, console, BIOS (snap_state_t)
#include "nvram.h"
#include "io.h"
#include "hal.h"
#include "con.h"
#include "net.h"
#include <string.h>

#define NV_SLOT 4
#define NV_SECTORS (1 + 2 * 130)
#define NV_MAGIC 0x56534E43u      // "CNSV"

static uint8_t sec[512];
static int asked, have_file, loaded;

void nv_init(void)
{
	(void)asked;
	have_file = 0;
	loaded = 0;
}

static int nv_ready(void)
{
	if (have_file) return 1;
	if (hal_disk_size(NV_SLOT) < NV_SECTORS * 512u) return 0;
	have_file = 1;
	return 1;
}

static int nv_load_settings(void)
{
	if (!nv_ready() || hal_disk_read(NV_SLOT, 0, sec)) return 0;
	uint32_t m = sec[0] | sec[1] << 8 | sec[2] << 16 | (uint32_t)sec[3] << 24;
	if (m != NV_MAGIC) return 0;
	memcpy(io_settings, sec + 16, sizeof io_settings);
	net_settings();
	return 1;
}

static int nv_save_settings(void)
{
	if (!nv_ready()) return 0;
	memset(sec, 0, sizeof sec);
	sec[0] = NV_MAGIC & 0xFF; sec[1] = (NV_MAGIC >> 8) & 0xFF; sec[2] = (NV_MAGIC >> 16) & 0xFF; sec[3] = NV_MAGIC >> 24;
	memcpy(sec + 16, io_settings, sizeof io_settings);
	return hal_disk_write(NV_SLOT, 0, sec) == 0;
}

// FE 80..87
uint8_t io_rtc_cmd(uint8_t a)
{
	uint8_t r = 0;
	switch (a) {
	case 80: for (int i = 0; i < 7; i++) io_sysvar_set((uint8_t)(65 + i), r = hal_rtc_rd((uint8_t)i)); break;
	case 81: for (int i = 0; i < 3; i++) hal_rtc_wr((uint8_t)i, r = io_sysvar((uint8_t)(65 + i))); break;
	case 82: for (int i = 3; i < 7; i++) hal_rtc_wr((uint8_t)i, r = io_sysvar((uint8_t)(65 + i))); break;
	case 83: nv_save_settings(); r = io_settings[95]; break;
	case 84: case 85: for (int i = 0; i < 4; i++) io_sysvar_set((uint8_t)(65 + i), 0); r = 0; break;
	case 86: r = 0; break;
	case 87: nv_load_settings(); r = io_settings[95]; break;
	default: r = (uint8_t)(a - 80); break;
	}
	return r;
}

// ------------------------------------------------------------------ snapshots

typedef struct {
	uint32_t magic;
	z80regs_t regs;
	uint8_t con[CON_STATE_SIZE];
	uint8_t bios[8];
} snap_state_t;

int nv_snap_save(int n, const z80regs_t *r)
{
	if (!nv_ready()) return 0;
	uint32_t base = 1 + (uint32_t)n * 130;
	for (uint32_t s = 0; s < 128; s++) {
		for (int i = 0; i < 512; i++) sec[i] = hal_rd((uint16_t)(s * 512 + i));
		if (hal_disk_write(NV_SLOT, base + s, sec)) return 0;
	}
	static snap_state_t st;
	memset(&st, 0, sizeof st);
	st.magic = NV_MAGIC;
	st.regs = *r;
	con_state_save(st.con);
	bios_state_save(st.bios);
	memset(sec, 0, sizeof sec);
	memcpy(sec, &st, sizeof st < 512 ? sizeof st : 512);
	return hal_disk_write(NV_SLOT, base + 128, sec) == 0;
}

int nv_snap_load(int n, z80regs_t *r)
{
	if (!nv_ready()) return 0;
	uint32_t base = 1 + (uint32_t)n * 130;
	static snap_state_t st;
	if (hal_disk_read(NV_SLOT, base + 128, sec)) return 0;
	memcpy(&st, sec, sizeof st < 512 ? sizeof st : 512);
	if (st.magic != NV_MAGIC) return 0;
	for (uint32_t s = 0; s < 128; s++) {
		if (hal_disk_read(NV_SLOT, base + s, sec)) return 0;
		for (int i = 0; i < 512; i++) hal_wr((uint16_t)(s * 512 + i), sec[i]);
	}
	*r = st.regs;
	con_state_load(st.con);
	bios_state_load(st.bios);
	return 1;
}

void nv_poll(void)
{
	// the settings once the file is there
	if (!loaded && nv_ready()) { loaded = 1; nv_load_settings(); }
}
