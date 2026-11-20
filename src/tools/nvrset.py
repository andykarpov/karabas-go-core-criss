#!/usr/bin/env python3
"""Show or change the settings of a CRISS NVRAM image (/criss/*.nvr, sector 0: "CNSV" + the 96 bytes
of the DS1305 RAM, docs/re/cpu-io.md 11.8). The system console of CRISS edits them on the original;
the core has none.

  nvrset.py FILE [--new] [KEY=VALUE ...]

KEY: con=IP:PORT   the console server (CON over UDP: CON_IOBYTE bit 2 out, bit 6 in)
     prn=PORT      the port of the network printer (LST, on the IP of the console server)
     pr=IP:PORT    the PUNCH / READER server (PR_SRV)
     lst=N         the LST IOBYTE at start (bit 2: network printer)
     lan=on|off    the network (settings 0x30 bit 5 = off)
     mac=, ip=     MAC / IP of CRISS (the core takes the ones of the ESP8266 once it is up)
--new makes the file (261 sectors of zeros, as `make nvr`) if it is missing.
GPL-3 (LICENSE)."""
import os, struct, sys

SECTORS = 261
MAGIC = b"CNSV"
OFF = 16


def ip_b(s):
    p = [int(x) for x in s.split(".")]
    if len(p) != 4 or any(x < 0 or x > 255 for x in p):
        raise ValueError("bad IP " + s)
    return bytes(p)


def ipport(s):
    ip, port = s.rsplit(":", 1)
    return ip_b(ip), struct.pack("<H", int(port))


def mac_b(s):
    p = bytes(int(x, 16) for x in s.split(":"))
    if len(p) != 6:
        raise ValueError("bad MAC " + s)
    return p


def show(st):
    ip = lambda o: ".".join(str(x) for x in st[o:o + 4])
    port = lambda o: struct.unpack_from("<H", st, o)[0]
    mac = lambda o: ":".join("%02x" % x for x in st[o:o + 6])
    print("mac %s  ip %s" % (mac(0x02), ip(0x08)))
    print("con %s:%d  (mac %s)  prn %d" % (ip(0x1C), port(0x20), mac(0x16), port(0x55)))
    print("pr  %s:%d  (mac %s)" % (ip(0x28), port(0x2C), mac(0x22)))
    print("lst 0x%02x  lan %s" % (st[0x54], "off" if st[0x30] & 0x20 else "on"))


def main(a):
    if not a or a[0].startswith("-"):
        print(__doc__)
        return 2
    fn, a = a[0], a[1:]
    if "--new" in a:
        a.remove("--new")
        if not os.path.exists(fn):
            open(fn, "wb").write(bytes(SECTORS * 512))
    img = bytearray(open(fn, "rb").read())
    if len(img) < SECTORS * 512:
        sys.exit("%s: %d bytes, %d expected" % (fn, len(img), SECTORS * 512))
    st = bytearray(img[OFF:OFF + 96]) if img[:4] == MAGIC else bytearray(96)
    for kv in a:
        k, v = kv.split("=", 1)
        if k == "con":
            i, p = ipport(v); st[0x1C:0x20] = i; st[0x20:0x22] = p
        elif k == "prn":
            st[0x55:0x57] = struct.pack("<H", int(v))
        elif k == "pr":
            i, p = ipport(v); st[0x28:0x2C] = i; st[0x2C:0x2E] = p
        elif k == "lst":
            st[0x54] = int(v, 0)
        elif k == "lan":
            st[0x30] = (st[0x30] & ~0x20) | (0x20 if v == "off" else 0)
        elif k == "mac":
            st[0x02:0x08] = mac_b(v)
        elif k == "ip":
            st[0x08:0x0C] = ip_b(v)
        else:
            sys.exit("unknown key " + k)
    if a:
        img[0:4] = MAGIC
        img[OFF:OFF + 96] = st
        open(fn, "wb").write(img)
    show(st)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
