// Layout of the data ROM of the .kg (src/tools/mkroms.py builds it from upstream/).
#ifndef ROM_H
#define ROM_H

#define ROM_MAGIC      0x53535243u   // "CRSS"
#define ROM_HDR        0x00000       // u32 magic, u32 version, u32 cpm image length
#define ROM_CPM        0x00100       // CP/M RAM image (CPM22.bin), loaded at Z80 0x0000, max 0xF800
#define ROM_FONTS      0x10000       // CP0..CP4: 5 x (20 rows x 256 chars), byte = row * 256 + char
#define ROM_FONT_SIZE  (20 * 256)
#define ROM_FONT_N     5
#define ROM_KBD_MAIN   0x18000       // 2048: page (Shift + 2 RUS + 4 Alt) * 256 + Ctrl * 128 + scan
#define ROM_KBD_CAPS   0x18800       // 256: CapsLock remap of the result
#define ROM_KBD_E0     0x18900       // 21: E0 + scan 0x69..0x7D -> scan
#define ROM_SIZE       0x18A00

#endif
