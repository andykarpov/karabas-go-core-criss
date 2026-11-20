; Ф2 console test 3
	device NOSLOT64K
	org 0x100
start:	ld hl, text
loop:	ld a, (hl)
	cp 0xFE
	jr z, done
	push hl
	ld c, a
	call conout
	pop hl
	inc hl
	jr loop
done:	ld c, 1
	call 5
	db 0x1B, 'G', '0'
	rst 0
conout:	ld hl, (1)
	ld de, 9
	add hl, de
	jp (hl)
E	equ 0x1B
text:	db E, 'G', '6'				; 30 x 64, CP0
	db "30 x 64 mode, row 1: 0123456789012345678901234567890123456789012", 13, 10
	db E, 'Y', 14, 0, "row 15 (middle)"
	db E, 'Y', 29, 0, "row 30: last line of the 30-line mode, 64 columns: ....END"
	db E, 'Y', 29, 63, "|"
	db 0xFE
	savebin "t3.com", 0x100, $ - 0x100
