#!/usr/bin/env python3
"""The other end of t11 (Ф4) on 127.0.0.1: UDP 47000 echoes "E:" + datagram; 47002 is the console
server: it collects the characters of CRISS and answers "Z" once "LAN>" came (then a second more).
Prints what it got.
  netpeer.py SECONDS [ADDRESS]   (ADDRESS to bind: 127.0.0.1, or 0.0.0.0 for the board)"""
import socket, sys, time

A = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
e = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); e.bind((A, 47000)); e.settimeout(0.05)
c = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); c.bind((A, 47002)); c.settimeout(0.05)
con, sent = b"", False
end = time.time() + float(sys.argv[1])
while time.time() < end:
    try:
        d, a = e.recvfrom(2048); e.sendto(b"E:" + d, a); print("echo", d, a, flush=True)
    except socket.timeout:
        pass
    try:
        d, a = c.recvfrom(2048); con += d
        if b"LAN>" in con and not sent:
            c.sendto(b"Z", a); sent = True; print("console from", a, flush=True)
            end = min(end, time.time() + 1)
    except socket.timeout:
        pass
print("console", con, flush=True)
