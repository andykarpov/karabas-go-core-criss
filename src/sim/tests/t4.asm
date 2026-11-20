; Ф2 console test 4
	device NOSLOT64K
	org 0x100
start:	ld a, 0
	ld (0xFF80), a
	ld a, 2
	ld (0xFF81), a
	ld a, 3
	ld (0xFF82), a
	ld a, 1
	ld (0xFF83), a
	ld hl, text
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
text:	db E, 'G', 'S'				; 24 x 80, code page per row from FF80 + row
	db "row 1: CP0 text", 13, 10
	db "row 2: CP2 Robotron RU: abcdef ABCDEF", 13, 10
	db "row 3: CP3 Robotron EN: abcdef ABCDEF", 13, 10
	db "row 4: CP1 graphics: ", 0x0F, 0xF0, 0xFF, 0x33, 0xCC, 13, 10
	db 0xFE
	savebin "t4.com", 0x100, $ - 0x100
