#!/usr/bin/env python3
"""The data ROM of the CRISS core (layout: fw/rom.h) from the upstream files of the CRISS project
(tools/fetch_upstream.sh): CP/M 2.2 image CPM22.bin, the character generators of the VGA MCU
firmware 1.24 (flash 0x800 + n * 0x1400, row-major: row * 256 + char), the keyboard tables of the
peripheral MCU firmware 0.24 (flash 0x2400 main, 0x2000 CapsLock, 0x2100 E0; docs/re/periph.md 4.5).
  mkroms.py <upstream dir> <out file>
"""
import struct
import sys

up, out = sys.argv[1], sys.argv[2]
x = up + '/fw/x/'
cpm = open(x + 'criss_cpm_22_01/CPM22.bin', 'rb').read()
vga = open(x + 'criss.fw.2023.11.CLR/vga_328.hex.bin', 'rb').read()
cpd = open(x + 'criss.cpd.0.24/cpd.hex.bin', 'rb').read()
assert len(cpm) <= 0xF800

ROM_SIZE = 0x18A00
rom = bytearray(ROM_SIZE)
rom[0:12] = struct.pack('<III', 0x53535243, 1, len(cpm))
rom[0x100:0x100 + len(cpm)] = cpm
for n in range(5):
    rom[0x10000 + n * 0x1400:0x10000 + (n + 1) * 0x1400] = vga[0x800 + n * 0x1400:0x800 + (n + 1) * 0x1400]
rom[0x18000:0x18800] = cpd[0x2400:0x2C00]
rom[0x18800:0x18900] = cpd[0x2000:0x2100]
rom[0x18900:0x18915] = cpd[0x2100:0x2115]
# the tables must match the extracted ones (tools/re) when those exist
for name, off, n in (('kbd_main', 0x18000, 2048), ('kbd_caps', 0x18800, 256), ('kbd_e0', 0x18900, 21)):
    try:
        ref = open(up + '/extract/' + name + '.bin', 'rb').read()
        assert ref == bytes(rom[off:off + n]), name
    except FileNotFoundError:
        pass
open(out, 'wb').write(rom)
print(out, len(rom))
