; Ф3 port test: version (FF 35/36), system variables (F6/F7), FPU (FF 0), timer DE, an interrupt of
; the timer event (KVU mask 0x4A through SYSRQ, INT_MASK through F6/F7, IM 1). Prints results as hex.
	device NOSLOT64K
	org 0x100
start:	ld sp, 0x8000
	ld hl, s_ver : call print
	ld a, 35 : out (0xFF), a : in a, (0xFE) : call hex	; 43
	ld a, 36 : out (0xFF), a : in a, (0xFE) : call hex	; 04
	ld hl, s_cur : call print
	ld a, 6 : out (0xF6), a : in a, (0xF7) : call hex	; cursor X (after the text)
	ld a, 9 : out (0xF6), a : in a, (0xF7) : call hex	; LINES 18
	ld a, 2 : out (0xF6), a : in a, (0xF7) : call hex	; HW_FOUND 8B
	; FPU: 1.5 + 2.25 = 3.75 (0x40700000)
	ld hl, s_fpu : call print
	ld a, 52 : out (0xF6), a
	ld b, 4 : ld hl, f_a
1:	ld a, (hl) : out (0xF7), a : inc hl : djnz 1b		; 52..55 A, then 56..59 B (auto increment)
	ld b, 4 : ld hl, f_b
1:	ld a, (hl) : out (0xF7), a : inc hl : djnz 1b
	xor a : out (0xFF), a					; FPU add
	ld a, 63 : out (0xF6), a : in a, (0xF7) : call hex
	ld a, 62 : out (0xF6), a : in a, (0xF7) : call hex
	ld a, 61 : out (0xF6), a : in a, (0xF7) : call hex
	ld a, 60 : out (0xF6), a : in a, (0xF7) : call hex
	; timer: DE = 5 (50 ms), wait until 0
	ld hl, s_tmr : call print
	in a, (0xF0)					; clear the events (the key of T5 is there)
	ld a, 5 : out (0xDE), a
1:	in a, (0xDE) : or a : jr nz, 1b
	in a, (0xF0) : call hex					; events: bit 2 timer (04)
	; interrupt: IM 1, the KVU mask 0x4A := 04 (SYSRQ 2), INT_MASK low := 04
	ld hl, s_int : call print
	di
	im 1
	ld a, 0x4A : out (0xDC), a : ld a, 0x04 : out (0xDD), a : ld a, 0x82 : out (0xDB), a
	ld a, 3 : out (0xF6), a : ld a, 0x04 : out (0xF7), a
	ld a, 0xC3 : ld (0x38), a : ld hl, isr : ld (0x39), hl	; JP isr at 0038 (CP/M keeps 0038 free)
	xor a : ld (count), a
	ei
	ld a, 3 : out (0xDE), a					; 30 ms
1:	ld a, (count) : or a : jr z, 1b
	di
	call hex						; 01
	ld hl, s_end : call print
	ld c, 1 : call 5
	rst 0
isr:	push af
	ld a, (count) : inc a : ld (count), a
	pop af
	ei
	reti
count:	db 0
f_a:	db 0x00, 0x00, 0xC0, 0x3F		; 1.5
f_b:	db 0x00, 0x00, 0x10, 0x40		; 2.25
s_ver:	db 13, 10, "VER ", 0
s_cur:	db 13, 10, "SYS ", 0
s_fpu:	db 13, 10, "FPU ", 0
s_tmr:	db 13, 10, "TMR ", 0
s_int:	db 13, 10, "INT ", 0
s_end:	db 13, 10, "END", 13, 10, 0
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
	savebin "t5.com", 0x100, $ - 0x100
