; Registers across the traps: IX, IY and the alternate set survive a BIOS call (ED trap) and an
; IN / OUT (I/O stop). Prints REGS OK or REGS BAD <n>.
	device NOSLOT64K
	org 0x100
start:	ld sp, 0x8000
	exx : ex af, af'
	ld bc, 0x1234 : ld de, 0x5678 : ld hl, 0x9ABC : ld a, 0x5A
	ex af, af' : exx
	ld ix, 0xDEF0 : ld iy, 0x0FED
	call const				; ED trap
	in a, (0xF0)				; I/O stop
	ld a, 35 : out (0xFF), a
	ld b, 1 : push ix : pop hl : ld de, 0xDEF0 : or a : sbc hl, de : jr nz, bad
	ld b, 2 : push iy : pop hl : ld de, 0x0FED : or a : sbc hl, de : jr nz, bad
	exx : ex af, af'
	push hl : push de : push bc : push af
	ex af, af' : exx
	ld b, 3 : pop hl : ld a, h : cp 0x5A : jr nz, bad
	ld b, 4 : pop hl : ld de, 0x1234 : or a : sbc hl, de : jr nz, bad
	ld b, 5 : pop hl : ld de, 0x5678 : or a : sbc hl, de : jr nz, bad
	ld b, 6 : pop hl : ld de, 0x9ABC : or a : sbc hl, de : jr nz, bad
	ld hl, s_ok : call print
	jp 0
bad:	push bc : ld hl, s_bad : call print : pop af : call hex
	jp 0
s_ok:	db "REGS OK", 13, 10, 0
s_bad:	db "REGS BAD ", 0
	include "lib.inc"
	savebin "t10.com", start, $ - start
