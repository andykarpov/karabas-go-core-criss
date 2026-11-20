; Ф3 RS-232 test: every byte from the USB serial port of the board is shown and sent back (F1..F3,
; DF); a key ends it.
	device NOSLOT64K
	org 0x100
start:	ld hl, s_hello : call print
	ld hl, s_tx : ld b, s_txe - s_tx
1:	ld a, (hl) : out (0xF2), a : ld a, 1 : out (0xDF), a : inc hl : djnz 1b
loop:	call const : or a : jr nz, done
	in a, (0xF1) : bit 7, a : jr z, loop
	in a, (0xF3)
	push af : ld c, a : call conout : pop af
	out (0xF2), a : ld a, 1 : out (0xDF), a		; echo
	jr loop
done:	call conin
	ld hl, s_end : call print
	rst 0
s_hello: db 13, 10, "RS-232 echo (a key ends): ", 0
s_tx:	db "CRISS RS-232 hello", 13, 10
s_txe:
s_end:	db 13, 10, "END", 13, 10, 0
	include "lib.inc"
	savebin "t6.com", 0x100, $ - 0x100
