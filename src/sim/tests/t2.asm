; Ф2 console test 2: pseudographics 160 x 96 (ESC G 4), circle, lines, dots. Ends waiting for a key.
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
	ld c, 0x1B		; back to the text mode: ESC G 0
	call conout
	ld c, 'G'
	call conout
	ld c, '0'
	call conout
	rst 0
conout:	ld hl, (1)
	ld de, 9
	add hl, de
	jp (hl)
E	equ 0x1B
text:	db E, 'G', '4'				; graphics, CP1
	db E, 'c', 0xFF, 48, 80, 40		; full circle centre (48, 80), R 40
	db E, 'c', 0x0F, 48, 80, 20		; half (octants 0..3)
	db E, 'L', 0, 0, 95, 159		; diagonal
	db E, 'L', 95, 0, 0, 159
	db E, 'L', 2, 2, 2, 157			; top line
	db E, 'x', 2, 10, 2, 30			; invert part of it (gap)
	db E, '*', 90, 5, E, '*', 90, 7, E, '*', 90, 9
	db 0xFE
	savebin "t2.com", 0x100, $ - 0x100
