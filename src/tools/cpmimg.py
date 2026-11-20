#!/usr/bin/env python3
"""CRISS CP/M floppy images (.cpm, 819 200 bytes): list, add, extract files, make an empty image.
Disk parameters of CRISS CP/M 2.2 (CPM22.Z80 DPB0): 128-byte sectors, 80 per track, 80 tracks,
2 system tracks, 2 KB blocks (BSH 4), DSM 389 (16-bit block numbers, 8 per extent), EXM 0, 128
directory entries (blocks 0..1), user 0 only for add. cpmtools: diskdef ris1715.
  cpmimg.py new <img>
  cpmimg.py ls <img>
  cpmimg.py add <img> <files...>       (names upper-cased, 8.3)
  cpmimg.py get <img> <name> [<out>]
"""
import os
import sys

SIZE = 819200
OFF = 2 * 80 * 128           # directory / block 0
BLOCK = 2048
DSM = 389
DIRN = 128
DIRBLOCKS = 2


def load(fn):
    d = bytearray(open(fn, 'rb').read())
    if len(d) != SIZE:
        sys.exit('%s: %d bytes, not a CRISS image (%d)' % (fn, len(d), SIZE))
    return d


def entries(d):
    for i in range(DIRN):
        yield i, d[OFF + 32 * i:OFF + 32 * i + 32]


def name_of(e):
    n = bytes(x & 0x7F for x in e[1:9]).decode('ascii', 'replace').rstrip()
    x = bytes(x & 0x7F for x in e[9:12]).decode('ascii', 'replace').rstrip()
    return n + ('.' + x if x else '')


def blocks_of(e):
    return [e[16 + 2 * k] | e[17 + 2 * k] << 8 for k in range(8) if e[16 + 2 * k] | e[17 + 2 * k] << 8]


def files(d):
    f = {}
    for i, e in entries(d):
        if e[0] == 0xE5 or e[0] > 15:
            continue
        key = (e[0], name_of(e))
        f.setdefault(key, []).append((e[12] + 32 * e[14], e[15], blocks_of(e)))
    return f


def cmd_ls(d):
    for (u, n), ext in sorted(files(d).items()):
        ext.sort()
        recs = sum(x[1] if j == len(ext) - 1 else 128 for j, x in enumerate(ext))
        print('%2d: %-12s %7d' % (u, n, recs * 128))


def cmd_get(d, name, out):
    name = name.upper()
    for (u, n), ext in files(d).items():
        if n == name:
            ext.sort()
            data = bytearray()
            for j, (_, rc, bl) in enumerate(ext):
                ext_data = bytearray()
                for b in bl:
                    ext_data += d[OFF + b * BLOCK:OFF + (b + 1) * BLOCK]
                data += ext_data[:rc * 128]
            open(out or name, 'wb').write(data)
            return
    sys.exit('%s: not found' % name)


def cmd_add(d, fns):
    used = set(range(DIRBLOCKS))
    for _, e in entries(d):
        if e[0] != 0xE5:
            used.update(blocks_of(e))
    free_dir = [i for i, e in entries(d) if e[0] == 0xE5]
    for fn in fns:
        base = os.path.basename(fn).upper()
        n, _, x = base.partition('.')
        if not n or len(n) > 8 or len(x) > 3:
            sys.exit('%s: not an 8.3 name' % base)
        if (0, base) in files(d):
            sys.exit('%s: already on the image' % base)
        data = open(fn, 'rb').read()
        recs = (len(data) + 127) // 128
        data += b'\x1A' * (recs * 128 - len(data))
        nblocks = (len(data) + BLOCK - 1) // BLOCK
        free = [b for b in range(DIRBLOCKS, DSM + 1) if b not in used]
        if len(free) < nblocks:
            sys.exit('%s: no room' % base)
        bl = free[:nblocks]
        used.update(bl)
        for k in range(nblocks):
            o = OFF + bl[k] * BLOCK
            chunk = data[k * BLOCK:(k + 1) * BLOCK]
            d[o:o + len(chunk)] = chunk
        ext_n = max(1, (nblocks + 7) // 8)
        for j in range(ext_n):
            if not free_dir:
                sys.exit('directory full')
            i = free_dir.pop(0)
            e = bytearray(32)
            e[0] = 0
            e[1:9] = n.ljust(8).encode()
            e[9:12] = x.ljust(3).encode()
            e[12] = j & 31
            e[14] = j >> 5
            r = recs - j * 128
            e[15] = 128 if r >= 128 else r
            for k, b in enumerate(bl[j * 8:(j + 1) * 8]):
                e[16 + 2 * k] = b & 0xFF
                e[17 + 2 * k] = b >> 8
            d[OFF + 32 * i:OFF + 32 * i + 32] = e
        print('%s: %d bytes, %d blocks' % (base, len(data), nblocks))


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        sys.exit(__doc__)
    cmd, img = a[0], a[1]
    if cmd == 'new':
        d = bytearray(b'\xE5' * SIZE)
        open(img, 'wb').write(d)
        return
    d = load(img)
    if cmd == 'ls':
        cmd_ls(d)
    elif cmd == 'get':
        cmd_get(d, a[2], a[3] if len(a) > 3 else None)
    elif cmd == 'add':
        cmd_add(d, a[2:])
        open(img, 'wb').write(d)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
