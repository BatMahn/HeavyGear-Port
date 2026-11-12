#!/usr/bin/env python3
"""Read the player's spawn state from the original game running under Wine (Linux): origstate.py <linux pid>

Reads the process memory (/proc/<pid>/mem; needs the same user or root). HGSIM.DLL is relocated by Wine, so its base is
taken from /proc/<pid>/maps. Addresses are HGSIM 1.2's:
  0x1014a568  the player's vehicle index;  0x107f24c0  vehicle pointers
  vehicle +0x54 x, y, z (float), +0x64 heading (degrees), +0x24 -> gear block, +0x53c the gear's standing height
  0x1011f908  the eyepoint: x, y, z, heading, pitch, roll (degrees)
"""
import struct, sys

def main():
    pid = int(sys.argv[1])
    base = next(int(l.split('-')[0], 16) for l in open(f'/proc/{pid}/maps') if l.lower().rstrip().endswith('hgsim.dll'))
    mem = open(f'/proc/{pid}/mem', 'rb')
    def rd(addr, n):
        mem.seek(base + addr - 0x10000000)
        return mem.read(n)
    def rdabs(addr, n):
        mem.seek(addr)
        return mem.read(n)
    pl = struct.unpack('<I', rd(0x1014a568, 4))[0]
    v = struct.unpack('<I', rd(0x107f24c0 + 4 * pl, 4))[0]
    x, y, z = struct.unpack('<3f', rdabs(v + 0x54, 12))
    hd = struct.unpack('<f', rdabs(v + 0x64, 4))[0]
    gear = struct.unpack('<I', rdabs(v + 0x24, 4))[0]
    stand = struct.unpack('<f', rdabs(gear + 0x53c, 4))[0]
    e = struct.unpack('<6f', rd(0x1011f908, 24))
    print(f'player {pl}: gear {x:.1f} {y:.1f} {z:.1f} heading {hd:.1f} standing height {stand:.1f}')
    print(f'eyepoint {e[0]:.1f} {e[1]:.1f} {e[2]:.1f} heading {e[3]:.2f} pitch {e[4]:.2f} roll {e[5]:.2f}')

if __name__ == '__main__':
    main()
