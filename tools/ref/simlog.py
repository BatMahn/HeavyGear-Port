#!/usr/bin/env python3
"""Sample the player gear's movement state from the original running under Wine: simlog.py <linux pid> [samples] [seconds apart]

HGSIM 1.2 addresses (relocated by Wine: the base comes from /proc/<pid>/maps). The vehicle (0x107f24c0[player]): +0x54
x, y, z, +0x64 heading, +0x24 -> the gear block, +0x50 -> the control block. The gear block, by 4-byte index: 0x135 top
speed, 0x140 throttle factor, 0x1e / 0x1f the throttle target / value, 10 / 0xb the speed target / value, 0xe the turn
rate, 0x152..0x154 the velocity. The control block: +8 the throttle (0..1/64), +0xc the turn (-1024..1024). The sim's
clock (0x10130768) counts 5 ms ticks.
"""
import struct,sys,time
pid=int(sys.argv[1]); n=int(sys.argv[2]) if len(sys.argv)>2 else 1; dt=float(sys.argv[3]) if len(sys.argv)>3 else 0.2
base = next(int(l.split('-')[0], 16) for l in open(f'/proc/{pid}/maps') if l.lower().rstrip().endswith('hgsim.dll'))
mem=open(f'/proc/{pid}/mem','rb')
def rd(a,n): mem.seek(base+a-0x10000000); return mem.read(n)
def ra(a,n): mem.seek(a); return mem.read(n)
pl=struct.unpack('<I',rd(0x1014a568,4))[0]; v=struct.unpack('<I',rd(0x107f24c0+4*pl,4))[0]
g=struct.unpack('<I',ra(v+0x24,4))[0]; ctl=struct.unpack('<I',ra(v+0x50,4))[0]
G=lambda i: struct.unpack('<f',ra(g+4*i,4))[0]
for k in range(n):
    t=struct.unpack('<i',rd(0x10130768,4))[0]
    x,y,z=struct.unpack('<3f',ra(v+0x54,12)); hd=struct.unpack('<f',ra(v+0x64,4))[0]
    c=ra(ctl,0x5c)
    print(f't={t} pos {x:.1f} {y:.1f} {z:.1f} hd {hd:.2f} max135 {G(0x135):.3f} f140 {G(0x140):.3f} thr1e {G(0x1e):.4f} thr1f {G(0x1f):.4f} tgt10 {G(10):.3f} spd b {G(0xb):.3f} turn e {G(0xe):.3f} v152 {G(0x152):.3f} {G(0x153):.3f} {G(0x154):.3f} ctl8 {struct.unpack("<f",c[8:12])[0]:.3f} ctlc {struct.unpack("<f",c[12:16])[0]:.1f} flag562 {struct.unpack("<H",ra(g+0x562,2))[0]:x} c59 {c[0x59]}')
    sys.stdout.flush()
    if k<n-1: time.sleep(dt)
