; Ф3 platforms: by the system variable 0 (platform), text at row 5 column 10 the way of the platform.
	device NOSLOT64K
	org 0x100
start:	ld a, 0 : out (0xF6), a : in a, (0xF7)
	ld hl, s_criss
	cp 6 : jr nz, 1f : ld hl, s_vt : jr go
1:	cp 1 : jr z, rob : cp 2 : jr nz, 2f
rob:	ld hl, s_rob : jr go
2:	cp 3 : jr z, kay : cp 5 : jr nz, go
kay:	ld hl, s_kay
go:	call print
	ld c, 1 : call 5
	rst 0
s_criss: db 0x0C, 0x1B, 'Y', 4, 9, "CRISS AT 5,10", 0
s_vt:	db 0x1B, "[2J", 0x1B, "[5;10H", "VT100 AT 5,10", 0x1B, "[1;1H", "TOP", 0
s_rob:	db 0x0C, 0x1B, 0x84, 0x89, "ROBOTRON AT 5,10", 0
s_kay:	db 0x1A, 0x1B, '=', 32+4, 32+9, "KAYPRO AT 5,10", 0x1B, 'B', '0', "INV", 0x1B, 'C', '0', 0
	include "lib.inc"
	savebin "t9.com", 0x100, $ - 0x100
