#!/usr/bin/env python3
"""Regenerate src/shell_tables.h from the game's HGSHELL.DLL 1.2: gen_shell_tables.py <HGSHELL.DLL> > src/shell_tables.h

Needs pefile. The shell's picture table (RE_NOTES "Shell") starts at 0x101ddd70: 1691 records of 41 bytes,
char name[9], char ext[4], i32 language (1: the art has a language variant), i32 parent (record index; a screen's
root is its own parent), i32 kind, i32 x0, y0, x1, y1 (relative to the parent, inclusive).

Also the font table (0x101ddab0: 10 records of 41 bytes, name[9] + "FNT"), the campaign table (0x101ace60: 39 records
of 127 bytes, char scenario[9], briefing[9], movie[9], title[100]) and the briefing map's front lines (0x101af0a0:
39 x 19 floats, the line's height on the map per campaign entry, at the x positions of 0x101a9fd8).
"""
import re, struct, sys
import pefile

TABLE, COUNT = 0x101ddd70, 1691
FONTS, NFONTS = 0x101ddab0, 10
CAMPAIGN, NCAMPAIGN = 0x101ace60, 39
FRONT, FRONT_X = 0x101af0a0, 0x101a9fd8

def cstr(b):
    return b.split(b'\0')[0].decode('latin1')

def flt(x):
    t = '%.9g' % x
    return t + ('f' if '.' in t or 'e' in t else '.0f')

def main():
    pe = pefile.PE(sys.argv[1])
    img = pe.get_memory_mapped_image()
    base = TABLE - pe.OPTIONAL_HEADER.ImageBase
    print('/* shell_tables.h - generated from HGSHELL.DLL 1.2 by tools/ref/gen_shell_tables.py (RE_NOTES "Shell"). */')
    print('/* The picture table at 0x101ddd70: name, ext, language, parent, kind, x0, y0, x1, y1. */')
    print(f'#define SHELL_PICTURES {COUNT}')
    print('static const shell_picture shell_pictures[SHELL_PICTURES] = {')
    for i in range(COUNT):
        r = img[base + i * 41: base + i * 41 + 41]
        name = r[:9].split(b'\0')[0].decode('latin1')
        ext = r[9:13].split(b'\0')[0].decode('latin1')
        if not ext.isalpha():
            ext = ''
        f = struct.unpack_from('<7i', r, 13)
        print('    {"%s", "%s", %d, %d, %d, %d, %d, %d, %d},' % ((name, ext) + f))
    print('};')
    ib = pe.OPTIONAL_HEADER.ImageBase
    print(f'#define SHELL_FONTS {NFONTS}')
    print('static const char *const shell_fonts[SHELL_FONTS] = {')
    print('    ' + ', '.join('"%s"' % cstr(img[FONTS - ib + i * 41:][:9]) for i in range(NFONTS)) + ',')
    print('};')
    print(f'#define SHELL_CAMPAIGN {NCAMPAIGN}')
    print('static const shell_campaign_entry shell_campaign[SHELL_CAMPAIGN] = {')
    for i in range(NCAMPAIGN):
        r = img[CAMPAIGN - ib + i * 127:][:127]
        print('    {"%s", "%s", "%s", "%s"},' % (cstr(r[0:9]), cstr(r[9:18]), cstr(r[18:27]), cstr(r[27:127])))
    print('};')
    print('static const float shell_front_x[19] = {%s};' % ', '.join(flt(v) for v in struct.unpack_from('<19f', img, FRONT_X - ib)))
    print('static const float shell_front[SHELL_CAMPAIGN][19] = {')
    for i in range(NCAMPAIGN):
        print('    {%s},' % ', '.join(flt(v) for v in struct.unpack_from('<19f', img, FRONT - ib + i * 0x4c)))
    print('};')
    gear_tables(img, ib)

def gear_tables(img, ib):
    I = lambda a: struct.unpack_from('<i', img, a - ib)[0]
    print('/* The briefing\'s gear lists (0x1009a38b): the stock GDFs (0x101c56f0, 13 bytes each),')
    print(' * and the gear tech check (0x1005bb6d): each equipment id\'s type (0x101b0004 + id x 0xaa: 0 torso, 1, 2 head, 3,')
    print(' * 4, 5..10) and tech level (0x101eec48), and each option\'s tech level (0x101ef3a0). */')
    a = img.find(b'GACHEETA.GDF')
    print('#define SHELL_STOCK_GEARS 25')
    print('static const char shell_stock_gear[SHELL_STOCK_GEARS][9] = {' +
          ', '.join('"%s"' % img[a + 13 * i:a + 13 * i + 8].split(b'.')[0].decode('latin1') for i in range(25)) + '};')
    print('#define SHELL_EQUIP_IDS 470')
    print('static const signed char shell_equip_type[SHELL_EQUIP_IDS] = {' + ', '.join(str(I(0x101b0004 + i * 0xaa)) for i in range(470)) + '};')
    print('static const signed char shell_equip_tech[SHELL_EQUIP_IDS] = {' + ', '.join(str(I(0x101eec48 + i * 4)) for i in range(470)) + '};')
    print('static const signed char shell_option_tech[52] = {' + ', '.join(str(I(0x101ef3a0 + i * 4)) for i in range(52)) + '};')
    garage_tables(img, ib)
    material_tables(img, ib)
    design_tables(img, ib)

PUSH_IMM = re.compile(rb'(?:\x6a(.)|\x68(....))', re.S)

def design_tables(img, ib):
    # the VDF classes' segment ids (0x10206a30: 0x42 x 0x44 bytes, the name first; FUN_1005c3c6)
    print('/* the gear editor\'s GDF assembly (src/design.c): the VDF classes in segment id order (0x10206a30, FUN_1005c3c6),')
    print(' * the POF parts (0x101366b1: "POF_AWLA" + n x 9) and the left / right pairs of the weapons that go either side')
    print(' * (0x1013804f -> 0x101348aa(id, a, b)) */')
    print('#define SHELL_SEGMENTS 0x42')
    print('static const char *const shell_segment[SHELL_SEGMENTS] = {' + ', '.join(
        '"%s"' % cstr(img[0x10206a30 + i * 0x44 - ib:0x10206a30 + i * 0x44 + 0x20 - ib]) for i in range(0x42)) + '};')
    a = img.find(b'POF_AWLA')
    print('static const char shell_pof[9][9] = {' + ', '.join('"%s"' % cstr(img[a + 9 * k:a + 9 * k + 9]) for k in range(9)) + '};')
    pairs = []
    for c in calls_to(img, ib, 0x1013804f, 0x10138f00, 0x101348aa):
        # push b; push a; push id; mov ecx, [ebp-4]; call
        w = bytes(img[c - 0x14 - ib:c - ib])
        vals = []
        for m in PUSH_IMM.finditer(w[:-3]):
            vals.append(m.group(1)[0] if m.group(1) is not None else struct.unpack('<i', m.group(2))[0])
        if len(vals) < 3:
            sys.exit('gen_shell_tables: pair call at %x moved' % c)
        b, a_, idn = vals[-3:]
        pairs.append((idn, a_, b))
    print('#define SHELL_PAIRS %d' % len(pairs))
    print('static const short shell_pair[SHELL_PAIRS][3] = {' + ', '.join('{%d, %d, %d}' % p for p in pairs) + '};')

def material_tables(img, ib):
    # 0x1012b93c: every TLT (through 0x1012d2e2) then every MLT (0x1012d2fe) the shell's 3D views use, each call
    # pushing the name first (68 imm32; mov reg, [ebp+8]; push reg; call)
    names = {0x1012d2e2: [], 0x1012d2fe: []}
    lo = 0x1012b93c
    hi = lo + 0x2000
    for c in sorted(calls_to(img, ib, lo, hi, 0x1012d2e2) + calls_to(img, ib, lo, hi, 0x1012d2fe)):
        tgt = c + 5 + struct.unpack_from('<i', img, c + 1 - ib)[0]
        if img[c - 9 - ib] != 0x68:
            sys.exit('gen_shell_tables: material list call at %x moved' % c)
        names[tgt].append(cstr(img[struct.unpack_from('<I', img, c - 8 - ib)[0] - ib:][:16]))
    print('/* the shell\'s 3D material tables (0x1012b93c): the TLT files, then the MLT files, it loads */')
    for tag, t in (('TLT', 0x1012d2e2), ('MLT', 0x1012d2fe)):
        print('#define SHELL_%sS %d' % (tag, len(names[t])))
        print('static const char *const shell_%s[SHELL_%sS] = {%s};' % (tag.lower(), tag, ', '.join('"%s"' % n for n in names[t])))

def calls_to(img, ib, lo, hi, target):
    return [a for a in range(lo, hi - 5)
            if img[a - ib] == 0xe8 and a + 5 + struct.unpack_from('<i', img, a + 1 - ib)[0] == target]

# the id pushed for each call of the gear editor's item adder 0x100f267f: push id, then the three other arguments
# (two code shapes: 0x100f4562 and 0x100f2d9d)
ADD_ITEM = [re.compile(rb'(?:\x6a(.)|\x68(....))\x8b.\xf8[\x50-\x57]\x8b.\x0c[\x50-\x57]\x8b.\x08[\x50-\x57]\Z', re.S),
            re.compile(rb'(?:\x6a(.)|\x68(....))\x8b.\xe8[\x50-\x57]\x8b.\xe4\x8b.\x14[\x50-\x57]\x8b.\xe4\x8b.\x0c[\x50-\x57]\Z', re.S)]

def item_groups(img, ib, lo, hi, step):
    groups, prev = [], None
    for c in calls_to(img, ib, lo, hi, 0x100f267f):
        w, v = bytes(img[c - 0x20 - ib:c - ib]), None
        for p in ADD_ITEM:
            m = p.search(w)
            if m:
                v = m.group(1)[0] if m.group(1) is not None else struct.unpack('<i', m.group(2))[0]
                break
        if v is None:
            sys.exit('gen_shell_tables: no id for the call at %x' % c)
        if prev is None or c - prev > step:
            groups.append([])
        groups[-1].append(v)
        prev = c
    return groups

def garage_tables(img, ib):
    I = lambda a: struct.unpack_from('<i', img, a - ib)[0]
    print('/* The gear editor (B07, FUN_1013fa9f). Each equipment id\'s name (0x101affa2 + id x 0xaa) and whether it is')
    print(' * offered (0x101afffc + id x 0xaa); the parts lists (0x100f4562: North, South; each gear\'s torso, engine,')
    print(' * head, arms, legs in turn) and the weapons lists by the design\'s model (0x100f2d9d: gear.vdf, mammoth.vdf,')
    print(' * naga.vdf), in the order the items are added. */')
    print('static const char *const shell_equip_name[SHELL_EQUIP_IDS] = {' + ', '.join(
        '"%s"' % cstr(img[0x101affa2 + i * 0xaa - ib:0x101affa2 + i * 0xaa + 0x50 - ib]) for i in range(470)) + '};')
    print('/* each id\'s short name (0x101aff98 + id x 0xaa: "LAC", "LRP 24"; the briefing\'s weapons list, FUN_1008f502) */')
    print('static const char *const shell_equip_short[SHELL_EQUIP_IDS] = {' + ', '.join(
        '"%s"' % cstr(img[0x101aff98 + i * 0xaa - ib:0x101aff98 + i * 0xaa + 0xa - ib]) for i in range(470)) + '};')
    print('/* each id\'s ODF code (0x101b0008 + id x 0xaa: a gear\'s parts "GA11" + the part, a weapon\'s whole name) */')
    print('static const char *const shell_equip_code[SHELL_EQUIP_IDS] = {' + ', '.join(
        '"%s"' % cstr(img[0x101b0008 + i * 0xaa - ib:0x101b0008 + i * 0xaa + 0xe - ib]) for i in range(470)) + '};')
    print('static const unsigned char shell_equip_offered[SHELL_EQUIP_IDS] = {' + ', '.join(
        str(I(0x101afffc + i * 0xaa)) for i in range(470)) + '};')
    F = lambda a: struct.unpack_from('<f', img, a - ib)[0]
    print('/* the list items\' statistics (0x100f267f and the sums 0x1011a1b0 ... 0x1011b531): per id the record\'s i32s at')
    print(' * 0x101b001e + 4k + id x 0xaa (k 0..8: +0x1e +0x22 load cap, +0x26 armour, +0x2a struct, +0x2e strength / power,')
    print(' * +0x32 stealth / range, +0x36 HP / sensors, +0x3a speed / ammo, +0x3e max speed) and its weight')
    print(' * (0x101ef470 + id x 0xc); per option (0x101c3824 + n x 0x98): +0 load, +0xc struct, +0x10 struct factor,')
    print(' * +0x14 strength, +0x18 stealth, +0x1c speed, +0x20 max speed, +0x24 sensors; per option its weight and load')
    print(' * capacity (0x101f0a78 + n x 8) */')
    print('static const int shell_equip_field[SHELL_EQUIP_IDS][9] = {' + ', '.join(
        '{' + ', '.join(str(I(0x101b001e + 4 * k + i * 0xaa)) for k in range(9)) + '}' for i in range(470)) + '};')
    print('static const int shell_equip_weight[SHELL_EQUIP_IDS] = {' + ', '.join(str(I(0x101ef470 + i * 0xc)) for i in range(470)) + '};')
    print('/* where the 3D view puts an item that is the root of what it shows (0x101b0012 / 0x101b0016 / 0x101b001a + id x')
    print(' * 0xaa, floats; FUN_100bec8a takes the z x 1.5 (0x101aa918) and FUN_1011d7ff moves the root object there) */')
    print('static const float shell_equip_view[SHELL_EQUIP_IDS][3] = {' + ', '.join(
        '{%s, %s, %s}' % (flt(F(0x101b0012 + i * 0xaa)), flt(F(0x101b0016 + i * 0xaa)), flt(F(0x101b001a + i * 0xaa)))
        for i in range(470)) + '};')
    print('typedef struct { int load, struct_add; float struct_mul; int strength, stealth, speed, max_speed, sensors; } shell_option_stat;')
    print('static const shell_option_stat shell_option[52] = {' + ', '.join(
        '{%d, %d, %s, %d, %d, %d, %d, %d}' % (I(o), I(o + 0xc), flt(F(o + 0x10)), I(o + 0x14), I(o + 0x18), I(o + 0x1c),
                                              I(o + 0x20), I(o + 0x24)) for o in (0x101c3824 + n * 0x98 for n in range(52))) + '};')
    print('static const int shell_option_load[52][2] = {' + ', '.join(
        '{%d, %d}' % (I(0x101f0a78 + n * 8), I(0x101f0a7c + n * 8)) for n in range(52)) + '};')
    parts = item_groups(img, ib, 0x100f4562, 0x100f4bd9, 0x16)   # a call every 0x16 bytes within a list
    arms = item_groups(img, ib, 0x100f2d9d, 0x100f4562, 0x30)
    if len(parts) != 2 or len(arms) != 3:
        sys.exit('gen_shell_tables: the gear editor lists moved')
    print('#define SHELL_GARAGE_PARTS %d' % len(parts[0]))
    print('static const short shell_garage_parts[2][SHELL_GARAGE_PARTS] = {' + ', '.join(
        '{' + ', '.join(str(v) for v in g) + '}' for g in parts) + '};')
    n = max(len(g) for g in arms)
    print('#define SHELL_GARAGE_ARMS %d' % n)
    print('static const short shell_garage_arms[3][SHELL_GARAGE_ARMS + 1] = {' + ', '.join(
        '{' + ', '.join(str(v) for v in g + [-1] * (n + 1 - len(g))) + '}' for g in arms) + '};')

if __name__ == '__main__':
    main()
