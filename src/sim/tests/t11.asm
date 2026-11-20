; Ф4 network test (fw/net.c through the ESP8266, on the PC: host --esp + netpeer.py): LAN up, UDP to
; 127.0.0.1:47000 and its echo (FE 69 / 66 / 67), ping (70 / 54 / 55 / 64), ARP (68 / 54 / 55 / 64),
; the console over UDP (CON_IOBYTE bits 2 and 6, server 127.0.0.1:47002 of the NVRAM image).
; The peer: sjasmplus -DP1=192 -DP2=168 -DP3=1 -DP4=120 (the board against a PC), 127.0.0.1 if not given.
	ifndef P1
P1 = 127
P2 = 0
P3 = 0
P4 = 1
	endif
	device NOSLOT64K
	org 0x100
start:	ld sp, 0x8000
	ld bc, 0
w_up:	ld a, 2 : out (0xF6), a : in a, (0xF7) : bit 5, a : jr nz, up
	dec bc : ld a, b : or c : jr nz, w_up
	ld hl, s_nolan : call print : jp 0
up:	ld hl, s_up : call print
	ld a, 10 : out (0xFA), a				; IP_MY
	ld b, 4
1:	push bc : in a, (0xFB) : call hex : pop bc : djnz 1b
	; IP_REMOTE = the peer, PORT_MY 47001, PORT_REMOTE 47000
	ld a, 14 : out (0xFA), a
	ld a, P1 : out (0xFB), a : ld a, P2 : out (0xFB), a : ld a, P3 : out (0xFB), a : ld a, P4 : out (0xFB), a
	ld a, 37 : out (0xFA), a
	ld hl, 47001 : call bank16 : ld hl, 47000 : call bank16
	ld a, 6 : out (0xFA), a					; MBUF_OUT, MBUF_OUT_L
	ld hl, msg : call bank16 : ld hl, 5 : call bank16
	ld a, 69 : out (0xFE), a
	ld bc, 0
w_udp:	ld a, 66 : out (0xFE), a
	ld a, 42 : out (0xFA), a : in a, (0xFB) : cp 0x83 : jr z, udp
	dec bc : ld a, b : or c : jr nz, w_udp
	ld hl, s_t_udp : call print : jp 0
udp:	ld a, 42 : out (0xFA), a : xor a : out (0xFB), a	; MAIN_RQST = 0
	ld a, 2 : out (0xFA), a : ld hl, buf : call bank16	; MBUF_IN
	ld a, 67 : out (0xFE), a
	ld a, 4 : out (0xFA), a : in a, (0xFB) : ld b, a	; MBUF_IN_L
	ld hl, buf : ld e, b : ld d, 0 : add hl, de : ld (hl), 0
	ld hl, s_udp : call print : ld hl, buf : call print
	; ping the peer
	ld a, 6 : out (0xFA), a : ld hl, msg : call bank16 : ld hl, 5 : call bank16
	ld a, 70 : out (0xFE), a : ld a, 54 : out (0xFE), a
	call w_frame : jr c, t_out
	ld hl, s_ping : call print : ld a, 64 : out (0xFE), a : in a, (0xFE) : call hex
	; ARP for the peer
	ld a, 68 : out (0xFE), a : ld a, 54 : out (0xFE), a
	call w_frame : jr c, t_out
	ld hl, s_arp : call print : ld a, 64 : out (0xFE), a : in a, (0xFE) : call hex
	ld a, 24 : out (0xFA), a				; MAC_REMOTE
	ld b, 6
1:	push bc : in a, (0xFB) : call hex : pop bc : djnz 1b
	; the console over the network: output to both, input from the server
	ld a, 1 : out (0xF6), a : ld a, 0x55 : out (0xF7), a
	ld hl, s_lan : call print
	call conin : push af
	ld a, 1 : out (0xF6), a : ld a, 0x11 : out (0xF7), a
	ld hl, s_got : call print : pop af : ld c, a : call conout
	ld hl, s_end : call print
	jp 0
t_out:	ld hl, s_t_fr : call print : jp 0

; a frame from 55 (MBUF_IN_L != 0), carry = time-out
w_frame: ld bc, 0
1:	ld a, 55 : out (0xFE), a
	ld a, 4 : out (0xFA), a : in a, (0xFB) : ld e, a : in a, (0xFB) : or e : ret nz
	dec bc : ld a, b : or c : jr nz, 1b
	scf : ret
bank16:	ld a, l : out (0xFB), a : ld a, h : out (0xFB), a : ret

msg:	db "HELLO"
s_nolan: db "NO LAN", 13, 10, 0
s_up:	db "LAN UP ", 0
s_t_udp: db 13, 10, "UDP TIMEOUT", 13, 10, 0
s_t_fr:	db 13, 10, "FRAME TIMEOUT", 13, 10, 0
s_udp:	db 13, 10, "UDP ", 0
s_ping:	db 13, 10, "PING ", 0
s_arp:	db 13, 10, "ARP ", 0
s_lan:	db 13, 10, "LAN>", 0
s_got:	db 13, 10, "GOT ", 0
s_end:	db 13, 10, "END", 13, 10, 0
	include "lib.inc"
buf:	ds 64
	savebin "t11.com", start, $ - start
