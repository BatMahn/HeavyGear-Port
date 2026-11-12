#!/usr/bin/env python3
"""Regenerate src/gear_tables.h from the game's HGSIM.DLL 1.2: gen_gear_tables.py <HGSIM.DLL> > src/gear_tables.h

Needs pefile (pip install pefile). The tables (RE_NOTES "Weapons"):
  equipment id -> weapon type   i32 at 0x10101d98 + id * 12   (read by the gear builder 0x1004e6a0)
  equipment id -> ammunition    i16 at 0x100eb862 + id * 0xaa
  weapon type -> HUD icon       name at 0x10119382 + type * 0xd6, the damaged icon at +0x10
  equipment id -> internal      i32 at 0x100eb852 + id * 0xaa (the zone's internal points before options)
  equipment options             0x100ff058 + n * 0x98: i32 added to the internal points, f32 multiplier
  weapon type -> stats          0x101192f0 + type * 0xd6 (the per-shot step 0x100ba550, the shot 0x100bb425)
  weapon type -> short name     0x100626b0 copies a language lookup (0x100a0384) of an English default into each
                                type's entry; the defaults' addresses, in type order, are below.
"""
import struct, sys
import pefile

def cf(x):
    t = '%.9g' % x
    return (t if ('.' in t or 'e' in t) else t + '.0') + 'f'

EQUIP_IDS = 470   # the equipment table's entries (0..469, the last the Shoulder Anti-Personnel Grenade Launcher; as the shell's 0x101afffc)
NAME_LITERALS = [0x101229c0, 0x101229c4, 0x101229c8, 0x101229cc, 0x101229d0, 0x101229d4, 0x101229d8, 0x101229dc,
                 0x101229e0, 0x101229e4, 0x101229e8, 0x101229ec, 0x101229f0, 0x101229f8, 0x101229fc, 0x10122a00,
                 0x10122a08, 0x10122a10, 0x10122a18, 0x10122a20, 0x10122a28, 0x10122a30, 0x10122a38, 0x10122a40,
                 0x10122a48, 0x10122a50, 0x10122a58, 0x10122a60, 0x10122a64, 0x10122a68, 0x10122a70, 0x10122a78,
                 0x10122a80, 0x10122a84, 0x10122a88, 0x10122a8c, 0x10122a90, 0x10122a94, 0x10122a98, 0x10122a9c,
                 0x10122aa0, 0x10122aa4, 0x10122aa8, 0x10122ab0, 0x10122ab4, 0x10122ab8]

def main():
    pe = pefile.PE(sys.argv[1])
    img = pe.get_memory_mapped_image()
    va = lambda a: a - pe.OPTIONAL_HEADER.ImageBase
    cstr = lambda a, n=16: img[va(a):va(a) + n].split(b'\0')[0].decode('latin1')
    print('/* gear_tables.h - generated from HGSIM.DLL 1.2 by tools/ref/gen_gear_tables.py (RE_NOTES "Weapons"). */')
    print('/* Equipment id -> weapon type: the i32 at 0x10101d98 + id * 12. Ammunition: the i16 at 0x100eb862 + id * 0xaa. */')
    print(f'#define GEAR_EQUIP_IDS {EQUIP_IDS}')
    print('static const signed char gear_weapon_type[GEAR_EQUIP_IDS] = {')
    v = [struct.unpack_from('<i', img, va(0x10101d98) + i * 12)[0] for i in range(EQUIP_IDS)]
    for i in range(0, EQUIP_IDS, 20):
        print('    ' + ', '.join(str(x if -128 <= x < 128 else -1) for x in v[i:i + 20]) + ',')
    print('};')
    print('static const short gear_weapon_ammo[GEAR_EQUIP_IDS] = {')
    v = [struct.unpack_from('<h', img, va(0x100eb862) + i * 0xaa)[0] for i in range(EQUIP_IDS)]
    for i in range(0, EQUIP_IDS, 16):
        print('    ' + ', '.join(str(x) for x in v[i:i + 16]) + ',')
    print('};')
    n = len(NAME_LITERALS)
    print('/* Weapon type -> HUD short name (English: the defaults 0x100626b0 looks up) and HUD icon (0x10119382 + type * 0xd6;')
    print(' * the damaged icon is the next name, +0x10). */')
    print(f'#define GEAR_WEAPON_TYPES {n}')
    print('static const char *const gear_weapon_name[GEAR_WEAPON_TYPES] = {')
    print('    ' + ', '.join('"%s"' % cstr(a) for a in NAME_LITERALS) + ',')
    print('};')
    print('static const char *const gear_weapon_icon[GEAR_WEAPON_TYPES][2] = {')
    for t in range(n):
        e = 0x10119382 + t * 0xd6
        print('    {"%s", "%s"},' % (cstr(e), cstr(e + 16)))
    print('};')
    print('/* Equipment id -> internal points: the i32 at 0x100eb852 + id * 0xaa (0x1003e6d5 and friends). */')
    print('static const short gear_equip_internal[GEAR_EQUIP_IDS] = {')
    v = [struct.unpack_from('<i', img, va(0x100eb852) + i * 0xaa)[0] for i in range(EQUIP_IDS)]
    for i in range(0, EQUIP_IDS, 16):
        print('    ' + ', '.join(str(x) for x in v[i:i + 16]) + ',')
    print('};')
    print('/* Equipment options (0x100ff058 + n * 0x98): points added, then a multiplier (0x1003d3fd, 0x1003d9d3, 0x1003dd42);')
    print(' * +0x1c a weapon\'s ammunition class (its record\'s +0xc low 16 bits pick the option; 0x1004e6a0: 1 and 2 add 5 and')
    print(' * 10 to the shots\' power, 3 adds 10; the names say AT, HEAT, APDS). */')
    print('#define GEAR_OPTIONS 51')
    print('static const struct { int add; float mul; int ammo; } gear_option[GEAR_OPTIONS] = {')
    for i in range(51):
        a, m = struct.unpack_from('<if', img, va(0x100ff058) + i * 0x98)
        q = struct.unpack_from('<i', img, va(0x100ff058) + i * 0x98 + 0x1c)[0]
        print('    {%d, %s, %d},' % (a, cf(m), q))
    print('};')
    print('/* The gear builder\'s sums (0x1004e6a0; RE_NOTES "Gear design"): per equipment id, the record at 0x100eb846 + id x')
    print(' * 0xaa: +0 weight (weapons), +4 capacity, +8 the torso\'s full armor, +0x10 handling, +0x14 stealth, +0x18 sensors,')
    print(' * +0x1c speed, +0x20 the speed cap; 0x10101d90 + id x 12: weight, +4 capacity (weapons). */')
    print('static const struct { short w, cap, armor, hand, stealth, sensor, speed, scap, weight, wcap; } gear_equip_sum[GEAR_EQUIP_IDS] = {')
    for i in range(EQUIP_IDS):
        e = va(0x100eb846) + i * 0xaa
        f = [struct.unpack_from('<i', img, e + o)[0] for o in (0, 4, 8, 0x10, 0x14, 0x18, 0x1c, 0x20)]
        f += [struct.unpack_from('<i', img, va(0x10101d90) + i * 12 + o)[0] for o in (0, 4)]
        print('    {' + ', '.join(str(x) for x in f) + '},')
    print('};')
    print('/* the options\' other fields (0x100ff058 + n x 0x98): +8 handling, +0xc stealth, +0x10 speed, +0x14 speed cap, +0x18')
    print(' * sensors; and their weight / capacity (0x10103398 + n x 8). Options 34.. are the head\'s flags (bits 0-7, 9-11). */')
    print('static const struct { int hand, stealth, speed, scap, sensor, weight, cap; } gear_option_sum[GEAR_OPTIONS] = {')
    for q in range(51):
        e = va(0x100ff058) + q * 0x98
        f = [struct.unpack_from('<i', img, e + o)[0] for o in (8, 0xc, 0x10, 0x14, 0x18)]
        f += [struct.unpack_from('<i', img, va(0x10103398) + q * 8 + o)[0] for o in (0, 4)]
        print('    {' + ', '.join(str(x) for x in f) + '},')
    print('};')
    print('/* 0x10103398 + n x 8 as a whole (the option weights above, and the torso record\'s armor kind / quality, +0x64 / +0x68) */')
    print('static const int gear_weight_tab[64] = {' + ', '.join(str(struct.unpack_from('<i', img, va(0x10103398) + q * 8)[0]) for q in range(64)) + '};')
    print('/* Weapon stats: 0x101192f0 + type * 0xd6. beam: the shot function is 0x100baf31 (else 0x100bb425); burst +0x2c,')
    print(' * auto +0x20 (0: one burst per trigger pull), gravity +0x38 (x -0.0296 per tick squared), speed +0x3c (units per')
    print(' * tick), reload +0x54 and interval +0x58 (ticks), life +0x5c (ticks), power +0x78, multiplier +0x7c, chip +0x80,')
    print(' * mode +0x84 (u16), the shot sound +0xb2. */')
    print('typedef struct { int beam, burst, autofire, gravity; float speed; int reload, interval, life, power; float mult;')
    print('                 int chip, mode; const char *sound; int rounds; int kind; float min_range, max_range; int guided; int flags;')
    print('                 int sflags;')
    print('                 const char *hit_sound; int flash; }')
    print('    gear_weapon_stats;   /* rounds +0x30 per magazine; kind +0x10 (the shot kind); the reticle\'s lock band +0x4c..+0x50;')
    print('                          * guided: +0x86 bit 0; flags +8: bit 0 / 1 the shot sound (0x100ba550); hit_sound +0xc2: a hit that pierces')
    print('                          * the armor (0x1005c1f8); sflags +0x86, the shot\'s +0x7e: bit 0 guided, 0x20 kept level, 0x40 a')
    print('                          * beam\'s look; flash +0x18: the effect started when it fires (0x100ba550, the muzzle')
    print('                          * flash; a vehicle of kind 9: 0x1e; a model named S...: 0x4c unless 0x1a) */')
    print('static const gear_weapon_stats gear_weapon_stat[GEAR_WEAPON_TYPES] = {')
    for t in range(n):
        e = va(0x101192f0 + t * 0xd6)
        I = lambda o: struct.unpack_from('<i', img, e + o)[0]
        F = lambda o: struct.unpack_from('<f', img, e + o)[0]
        H = lambda o: struct.unpack_from('<H', img, e + o)[0]
        snd = img[e + 0xb2:e + 0xc2].split(b'\0')[0].decode('latin1')
        print('    {%d, %d, %d, %d, %s, %d, %d, %d, %d, %s, %d, %d, "%s", %d, %d, %s, %s, %d, %d, %d, "%s", %d},' % (
            I(4) == 0x100baf31, I(0x2c), I(0x20), I(0x38), cf(F(0x3c)), I(0x54), I(0x58), I(0x5c), I(0x78), cf(F(0x7c)),
            I(0x80), H(0x84), snd, I(0x30), I(0x10), cf(F(0x4c)), cf(F(0x50)), H(0x86) & 1, I(8), I(0x86), img[e + 0xc2:e + 0xd2].split(b'\0')[0].decode('latin1'), I(0x18)))
    print('};')
    print('/* Critical hits (0x1004e320): a gear\'s crit slots per zone from its type (+0xbdb: 1..5 by the torso, 0x1004e6a0)')
    print(' * and the zone\'s group (0x1010e670); counts 0x1010e8e8, slots 0x1010e6b8 (0x24c per type, 0x50 per group). */')
    print('#define GEAR_CRIT_TYPES 6')
    print('static const int gear_crit_group[17] = {' + ', '.join(str(struct.unpack_from('<i', img, va(0x1010e670) + 4 * z)[0]) for z in range(17)) + '};')
    print('static const struct { int n; unsigned short id[20]; } gear_crit[GEAR_CRIT_TYPES][7] = {')
    for t in range(6):
        rows = []
        for g in range(7):
            n = struct.unpack_from('<i', img, va(0x1010e8e8) + g * 4 + t * 0x24c)[0]
            ids = [struct.unpack_from('<i', img, va(0x1010e6b8) + i * 4 + g * 0x50 + t * 0x24c)[0] for i in range(n)]
            rows.append('{%d, {%s}}' % (n, ', '.join('0x%x' % x for x in ids) or '0'))
        print('    {' + ', '.join(rows) + '},')
    print('};')
    print('/* Effects (0x1011b968 + n * 0x3c, the effect step 0x1001b3a1): +8 > 0 and a chance +0xc (percent, -1 always) let')
    print(' * the sound at +0x2c play, placed at the effect - once the effect has started (src/effects.h). life +0: ticks the')
    print(' * effect lasts (counted down by 0x1001c40d); flags +0x28: bit 0 it follows the struck vehicle, bit 1 no limit on')
    print(' * effects of the type close by; splash +0x18: it harms what is near it each frame, power +0x20 a tick (0x1001c40d ->')
    print(' * 0x1001c75c / 0x1001c96a). */')
    print('#define GEAR_EFFECTS 0x4d')
    print('static const struct { int gate, chance; const char *sound; int life, flags, splash; float power; } gear_effect[GEAR_EFFECTS] = {')
    for k in range(0x4d):
        e = va(0x1011b968 + k * 0x3c)
        g, c = struct.unpack_from('<ii', img, e + 8)
        nm = img[e + 0x2c:e + 0x3c].split(b'\0')[0].decode('latin1')
        print('    {%d, %d, "%s", %d, %d, %d, %s},' % (g, c, nm, struct.unpack_from('<i', img, e)[0], struct.unpack_from('<i', img, e + 0x28)[0],
                                                struct.unpack_from('<i', img, e + 0x18)[0], cf(struct.unpack_from('<f', img, e + 0x20)[0])))
    print('};')
    voice_sets(img, va)

def voice_sets(img, va):
    print('/* The squad mates\' reply voices (0x1010a128: 20 sets of 10 names, 0x18 bytes each; 0x100173e1). A mate\'s set:')
    print(' * its vehicle record\'s +0x192 (0xffff: 0, below 9: + 1, else - 1; 0x10017766). */')
    print('static const char gear_voice_set[20][10][9] = {')
    for k in range(20):
        p = struct.unpack_from('<I', img, va(0x1010a128) + 4 * k)[0]
        names = [img[va(p) + 0x18 * i:va(p) + 0x18 * i + 9].split(b'\0')[0].decode('latin1') for i in range(10)]
        print('    {' + ', '.join('"%s"' % n for n in names) + '},')
    print('};')

if __name__ == '__main__':
    main()
