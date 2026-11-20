; Ф3 mouse test: init (D7 E0), then polls (D7 E1) and shows D8 (buttons) D9 (dx sum) DA (dy sum) on
; one line until a key.
	device NOSLOT64K
	org 0x100
start:	ld hl, s_hello : call print
	ld a, 0xE0 : out (0xD7), a
	in a, (0xD7) : call hex
	ld hl, s_nl : call print
loop:	call const : or a : jr nz, done
	ld a, 0xE1 : out (0xD7), a
	ld c, 13 : call conout
	ld hl, s_b : call print
	in a, (0xD8) : call hex
	ld hl, s_x : call print
	in a, (0xD9) : call hex
	ld hl, s_y : call print
	in a, (0xDA) : call hex
	ld bc, 0x4000
1:	dec bc : ld a, b : or c : jr nz, 1b
	jr loop
done:	call conin
	ld a, 0xE4 : out (0xD7), a
	ld hl, s_end : call print
	rst 0
s_hello: db 13, 10, "MOUSE init ", 0
s_nl:	db 13, 10, 0
s_b:	db "B ", 0
s_x:	db " X ", 0
s_y:	db " Y ", 0
s_end:	db 13, 10, "END", 13, 10, 0
	include "lib.inc"
	savebin "t7.com", 0x100, $ - 0x100
