; Ф3 melody test: a scale C D E F G A B C (notes 1/4, 0x22 = pause 1, unit 2 ticks), then BEL.
	device NOSLOT64K
	org 0x100
start:	ld hl, s_hello : call print
	ld hl, notes : ld c, 0xE0 : ld b, 9
1:	ld a, (hl) : out (c), a : inc hl : inc c : djnz 1b
	ld a, 0x2A : out (0xD6), a			; PD 1, ND 10 ticks: 1/4 = 200 ms
2:	in a, (0xD6) : or a : jr nz, 2b
	ld c, 7 : call conout				; BEL
	ld hl, s_end : call print
	rst 0
; tone index << 3 | DI 1 (x2): C 22 (261.6), D 20 (293.7), E 18 (329.6), F 17 (349.2), G 15 (392), A 13 (440), B 11 (493.9), C 10 (523.2)
notes:	db 22 << 3 | 1, 20 << 3 | 1, 18 << 3 | 1, 17 << 3 | 1, 15 << 3 | 1, 13 << 3 | 1, 11 << 3 | 1, 10 << 3 | 1, 0x04
s_hello: db 13, 10, "MELODY", 0
s_end:	db 13, 10, "END", 13, 10, 0
	include "lib.inc"
	savebin "t8.com", 0x100, $ - 0x100
