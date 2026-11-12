#!/usr/bin/env python3
"""Write a USERSTAR.BWD for running the original game directly into a mission (reference captures under Wine).

    mkuserstar.py <m01_scn1.BWD> <out USERSTAR.BWD> [gear]

The shell normally writes this file (HGSHELL 0x100ff1e6). The player's record is a GPS gear placement (92 bytes, the
missions' layout) with group 0, leader 1, role 0 (the player), flags 0x4006 / 0x400; this copies mission 1's first
GPS record for the gear (default gdhuntea, the Hunter) and sets those fields.
The BWD header's third word is the largest chunk size: the sim's chunk walker (HGSIM 0x1001d8a1) rejects any chunk
bigger than it, so a 0 there makes the whole file read as empty.
"""
import struct, sys

def chunks(b):
    p = 12
    while p + 8 <= len(b):
        size = struct.unpack_from('<I', b, p + 4)[0]
        if size < 8:
            break
        yield b[p:p + 4], b[p + 8:p + size]
        p += size

def main():
    src, out = sys.argv[1], sys.argv[2]
    gear = (sys.argv[3] if len(sys.argv) > 3 else 'gdhuntea').encode()
    b = open(src, 'rb').read()
    rec = next(d for t, d in chunks(b) if t == b'GPS\0' and d[0x1c:0x1c + len(gear)].lower() == gear.lower())
    g = bytearray(rec)
    g[4], g[5], g[6] = 0, 1, 0                     # group 0, leader, role 0 = the player
    struct.pack_into('<HHH', g, 0x16, 0, 0x4006, 0x400)
    def ch(tag, data):
        return tag + struct.pack('<I', len(data) + 8) + bytes(data)
    cs = [ch(b'REV\0', b'1.22'), ch(b'DTBL', bytes(20)), ch(b'GPS\0', g)]
    body = b''.join(cs)
    open(out, 'wb').write(b'BWD\0' + struct.pack('<II', 12 + len(body), max(len(c) for c in cs)) + body)

if __name__ == '__main__':
    main()
