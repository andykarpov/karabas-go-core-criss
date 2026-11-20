; Ф2 console test 1: colour (CLR), window with a frame, status line. Ends waiting for a key.
	device NOSLOT64K
	org 0x100
start:	ld hl, text
loop:	ld a, (hl)
	cp 0xFE			; end marker (FE never used as data here)
	jr z, done
	push hl
	ld c, a
	call conout
	pop hl
	inc hl
	jr loop
done:	ld c, 1			; BDOS 1: wait for a key
	call 5
	rst 0
conout:	ld hl, (1)		; BIOS WBOOT + 9 = CONOUT
	ld de, 9
	add hl, de
	jp (hl)
E	equ 0x1B
text:	db 0x0C
	db E, 'r', 'P'				; colour schemes (dynamic colour)
	db "CRISS console test 1: colours", 13, 10
	db "red   green yellow blue  magenta cyan  white", 13, 10
	db E, 'Y', 1, 0, E, 's', 6, 1		; row 2 col 1: 6 cells red
	db E, 'Y', 1, 6, E, 's', 6, 2
	db E, 'Y', 1, 12, E, 's', 6, 3
	db E, 'Y', 1, 18, E, 's', 6, 4
	db E, 'Y', 1, 24, E, 's', 8, 5
	db E, 'Y', 1, 32, E, 's', 6, 6
	db E, 'Y', 1, 38, E, 's', 6, 7
	db E, 'Y', 1, 0, E, 's', 0, 0		; position only
	db E, 'W', 50, 4, 76, 12		; window x 50..76, y 4..12
	db E, 'w', 'F'				; double frame
	db E, 'y', 2, 3, "inside the window"
	db E, 'y', 4, 3, "row 4 of it"
	db E, 'Y', 4, 0, "ESC = and Y: row 5, col 1"
	db E, '=', 32+6, 32+10, "row 7, col 11 (ESC =)"
	db E, 'a', 'B'				; bottom status line
	db E, 'A', 2, "STATUS: bottom line 24", E, 'C', '6'
	db E, 'Y', 9, 0, "after the status line, row 10"
	db E, 'Y', 12, 0, "graphics chars: ", 0xB0, 0xB1, 0xB2, 0xDB, 0xC9, 0xCD, 0xBB
	db E, 'Y', 14, 0, "russian: ", 0x8F, 0xE0, 0xA8, 0xA2, 0xA5, 0xE2, 0x21
	db E, 'Y', 16, 0
	db 0xFE
	savebin "t1.com", 0x100, $ - 0x100
