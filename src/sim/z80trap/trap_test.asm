; criss_z80 trap test (src/sim/z80trap/run.sh). Boot: the bench freezes the CPU at PC 0 and sets
; PC = SP = 0x0100 through DIR (the boot path of the core). Results: RES area, checked by the bench.
	device NOSLOT64K
	org 0
	jp fail			; never run: the boot sets PC to 0x0100
	org 0x0100
start:
	ld sp, 0xF000
	; registers with known values, alternate set active after EXX / EX AF,AF'
	ld a, 0x11
	ld bc, 0x2233
	ld de, 0x4455
	ld hl, 0x6677
	exx
	ex af, af'
	ld a, 0x88
	ld bc, 0x99AA
	ld de, 0xBBCC
	ld hl, 0xDDEE
	ld ix, 0x1357
	ld iy, 0x2468
	; trap 1: ED 02 (CONST), the bench writes REG back unchanged (identity)
	db 0xED, 0x02
	ld (res+0), a
	ld (res+1), bc
	ld (res+3), de
	ld (res+5), hl
	ld (res+7), ix
	ld (res+9), iy
	exx
	ex af, af'
	ld (res+11), a
	ld (res+12), bc
	ld (res+14), de
	ld (res+16), hl
	; trap 2: ED 0D (READ), main set active: the bench sets A = 0x5A, HL = 0x1234, C = 0x77
	db 0xED, 0x0D
	ld (res+18), a
	ld (res+19), hl
	ld (res+21), bc
	; not traps: ED ED 02 (ED ED = NOP, then 02 = LD (BC),A), CB 02 (RLC D), DD CB 05 02, ED 44 (NEG)
	ld bc, scratch
	ld a, 0x3C
	db 0xED, 0xED, 0x02
	ld d, 0x81
	db 0xCB, 0x02
	ld ix, scratch
	db 0xDD, 0xCB, 0x01, 0x02
	ld a, 1
	db 0xED, 0x44
	ld (res+23), a		; NEG 1 = FF
	; traps with a prefix and the extensions: DD ED 05, ED 77, ED 7F, ED A4, ED A7, ED 27
	db 0xDD, 0xED, 0x05
	db 0xED, 0x77
	db 0xED, 0x7F
	db 0xED, 0xA4
	db 0xED, 0xA7
	db 0xED, 0x27
	; not a trap: ED 28 .. (NOP), ED A3 (OUTI would do I/O: skipped), ED A8 (LDD) skipped
	db 0xED, 0x28
	; trap 3: ED 10, the bench moves PC to 'jumped'
	db 0xED, 0x10
	jp fail
jumped:
	ld a, 0xC3
	ld (res+24), a
	; interrupts: IM 1, EI, the bench holds INT low around the next trap; the trap is taken first
	im 1
	ei
	db 0xED, 0x03
	nop			; the interrupt is taken here, after the release
	nop
	di
	ld (res+25), a		; A from the bench: 0x42 (set at the trap, before the interrupt)
	ld a, (intcnt)
	ld (res+26), a		; >= 1: the interrupt came after the release
	ld a, 0xAA
	ld (done), a
	halt
fail:
	ld a, 0xEE
	ld (done), a
	halt

	org 0x0038
	push af
	ld a, (intcnt)
	inc a
	ld (intcnt), a
	pop af
	ei
	reti

	org 0x0800
res:	ds 32
scratch: ds 4
intcnt:	db 0
done:	db 0
	savebin "trap_test.bin", 0, 0x0900
