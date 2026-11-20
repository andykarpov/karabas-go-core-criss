; common routines of the tests: print (HL, zero-terminated), hex (A), conout (C) through the BIOS,
; const (A = FF: a key), conin (A)
print:	ld a, (hl) : or a : ret z
	push hl : ld c, a : call conout : pop hl : inc hl : jr print
hex:	push af : rrca : rrca : rrca : rrca : call nib : pop af
nib:	and 15 : add a, '0' : cp '9' + 1 : jr c, nib1 : add a, 7
nib1:	ld c, a
conout:	push bc : push de : push hl
	ld hl, (1) : ld de, 9 : add hl, de
	ld de, cret : push de
	jp (hl)
cret:	pop hl : pop de : pop bc
	ret
const:	push bc : push de : push hl
	ld hl, (1) : ld de, 3 : add hl, de
	ld de, cret : push de
	jp (hl)
conin:	push bc : push de : push hl
	ld hl, (1) : ld de, 6 : add hl, de
	ld de, cret : push de
	jp (hl)
