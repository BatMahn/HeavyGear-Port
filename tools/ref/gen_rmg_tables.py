#!/usr/bin/env python3
"""gen_rmg_tables.py - dump the random mission generator's tables (HGSHELL 1.2, 0x100642c9 .. 0x1007199f) into
src/rmg_tables.h. Usage: gen_rmg_tables.py <HGSHELL.DLL> > src/rmg_tables.h. RE_NOTES "The random mission
generator". The static CStrings are found by their constructors (push str; mov ecx, cstr; call 0x100245c6) and the
locale table's run-time stores (0x10067d11) are applied over its static bytes."""
import struct, sys, re

d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
ns = struct.unpack_from('<H', d, pe + 6)[0]
so = pe + 24 + struct.unpack_from('<H', d, pe + 20)[0]
secs = [struct.unpack_from('<8sIIII', d, so + i * 40) for i in range(ns)]
BASE = 0x10000000

def off(va):
    rva = va - BASE
    for n, vs, v, rs, ro in secs:
        if v <= rva < v + rs:
            return ro + rva - v
    return None

def va_of(o):
    for n, vs, v, rs, ro in secs:
        if ro <= o < ro + rs:
            return BASE + v + o - ro

mem = {}
def u32(va):
    if va in mem: return mem[va]
    o = off(va)
    return struct.unpack_from('<I', d, o)[0] if o is not None else 0
def i32(va):
    v = u32(va); return v - (1 << 32) if v & 0x80000000 else v
def f64(va):
    return struct.unpack('<d', struct.pack('<II', u32(va), u32(va + 4)))[0]
def f32(va):
    return struct.unpack('<f', struct.pack('<I', u32(va)))[0]
def cstr(va, n=96):
    o = off(va)
    return d[o:o + n].split(b'\0')[0].decode('latin1') if o is not None else ''

# static CStrings: 68 <str> b9 <obj> e8 <rel32 -> 0x100245c6>
CS = {}
for m in re.finditer(rb'\x68(....)\xb9(....)\xe8(....)', d, re.S):
    s, obj, rel = struct.unpack('<IIi', m.group(1) + m.group(2) + m.group(3))
    call_va = va_of(m.start() + 10)
    if call_va is None or call_va + 5 + rel != 0x100245c6:
        continue
    CS[obj] = cstr(s)
def cs(va): return CS.get(va, '')

# the locale table's run-time stores (0x10067d11): mov ds:a, imm / mov r, ds:b ; mov ds:a, r
fo = off(0x10067d11); end = off(0x1006ae5c)
code = d[fo:end]; i = 0; reg = {}
while i < len(code):
    b = code[i]
    if code[i:i + 2] == b'\xc7\x05':
        a, v = struct.unpack_from('<II', code, i + 2); mem[a] = v; i += 10; continue
    if b == 0xa1:
        reg['eax'] = u32(struct.unpack_from('<I', code, i + 1)[0]); i += 5; continue
    if b == 0xa3:
        mem[struct.unpack_from('<I', code, i + 1)[0]] = reg.get('eax', 0); i += 5; continue
    if b in (0x8b, 0x89) and code[i + 1] in (0x0d, 0x15, 0x1d, 0x35, 0x3d):
        r = {0x0d: 'ecx', 0x15: 'edx', 0x1d: 'ebx', 0x35: 'esi', 0x3d: 'edi'}[code[i + 1]]
        a = struct.unpack_from('<I', code, i + 2)[0]
        if b == 0x8b: reg[r] = u32(a)
        else: mem[a] = reg.get(r, 0)
        i += 6; continue
    i += 1

def q(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'
out = []
P = out.append
P('/* rmg_tables.h - generated from HGSHELL.DLL 1.2 by tools/ref/gen_rmg_tables.py (RE_NOTES "The random mission')
P(' * generator"). Do not edit. */')
P('#ifndef HG_RMG_TABLES_H\n#define HG_RMG_TABLES_H\n')
P('typedef struct { int id; double p; } rmg_pick;')
# templates 0x101a80d8: [side][type 10] {type, n, (id, p)}
P('/* 0x101a80d8: the scenario template by side and mission type: n picks (template id, probability) */')
P('static const struct { int n; rmg_pick e[4]; } rmg_tmpl[2][10] = {')
for side in range(2):
    row = []
    for t in range(10):
        b = 0x101a80d8 + side * 0x140 + t * 0x20
        n = i32(b + 4)
        es = ', '.join('{%d, %r}' % (i32(b + 8 + k * 12), f64(b + 12 + k * 12)) for k in range(n))
        row.append('{%d, {%s}}' % (n, es))
    P('    {' + ', '.join(row) + '},')
P('};')
P('/* 0x101a8358: the art (time of day, art set) by rand(4) */')
P('static const int rmg_art[4][2] = {' + ', '.join('{%d, %d}' % (i32(0x101a8358 + k * 8), i32(0x101a835c + k * 8)) for k in range(4)) + '};')
P('/* 0x1020d534 (set up by 0x1005cefa): the ground meshes of each geo set */')
P('static const struct { int n; int id[8]; } rmg_mesh[3] = {')
for k in range(3):
    n = i32(0x1020d534 + k * 0x20)
    P('    {%d, {%s}},' % (n, ', '.join(str(i32(0x1020d538 + k * 0x20 + j * 4)) for j in range(n))))
P('};')
# objectives 0x101a8378 [33] {id, n, ptr picks, geo[3] {count, ptr ids}}
P('/* 0x101a8378: per template (0x33 + i) the objective count picks and each objective slot\'s geo ids */')
P('static const struct rmg_obj_s { int id, n; rmg_pick e[4]; int ngeo[3]; int geo[3][40]; } rmg_obj[33] = {')
for k in range(33):
    b = 0x101a8378 + k * 0x18
    n = i32(b + 4); p = u32(b + 8)
    es = ', '.join('{%d, %r}' % (i32(p + j * 12), f64(p + 4 + j * 12)) for j in range(n)) if p else ''
    ng = []; gs = []
    for s in range(3):
        g = u32(b + 0xc + s * 4)
        if g:
            c = i32(g); lp = u32(g + 4)
            ng.append(str(c)); gs.append('{' + ', '.join(str(i32(lp + j * 4)) for j in range(c)) + '}')
        else:
            ng.append('0'); gs.append('{0}')
    P('    {%d, %d, {%s}, {%s}, {%s}},' % (i32(b), n, es, ', '.join(ng), ', '.join(gs)))
P('};')
P('/* 0x101a8690: the location record suffix by geo set and objective count */')
P('static const struct { int n; const char *s[4]; } rmg_loc[3][4] = {')
for s in range(3):
    row = []
    for n in range(4):
        b = 0x101a8690 + s * 0x20 + n * 8
        c = i32(b); p = u32(b + 4)
        row.append('{%d, {%s}}' % (c, ', '.join(q(cs(p + j * 0x10)) for j in range(c))))
    P('    {' + ', '.join(row) + '},')
P('};')
P('/* 0x101a86f0: the waypoint record suffix by template, generator mode and geo set */')
P('static const struct rmg_way_s { int id, n; const char *s[3][4]; } rmg_way[33][3] = {')
for k in range(33):
    row = []
    for mo in range(3):
        b = 0x101a86f0 + mo * 0xc + k * 0x24
        idv = i32(b); c = i32(b + 4); p = u32(b + 8)
        sets = []
        for s in range(3):
            lp = u32(p + s * 4) if p else 0
            sets.append('{' + ', '.join(q(cs(lp + j * 0x10)) for j in range(c)) + '}' if lp and c > 0 else '{0}')
        row.append('{%d, %d, {%s}}' % (idv, c, ', '.join(sets)))
    P('    {' + ', '.join(row) + '},')
P('};')
P('/* 0x101a8b98: the enemy picks by threat (0 .. 0x1a): (enemy kind, probability) */')
P('static const struct { int n; rmg_pick e[9]; } rmg_enemy[27] = {')
for k in range(27):
    b = 0x101a8b98 + k * 0xc
    c = i32(b + 4); p = u32(b + 8)
    P('    {%d, {%s}},' % (c, ', '.join('{%d, %r}' % (i32(p + j * 12), f64(p + 4 + j * 12)) for j in range(c))))
P('};')
P('static const char *const rmg_enemy_rec[9] = {' + ', '.join(q(cs(0x1024ad90 + j * 0x10)) for j in range(9)) + '};')
P('static const char *const rmg_enemy_name[2][9] = {')
for s in range(2):
    P('    {' + ', '.join(q(cs(u32(0x1020da10 + s * 4) + j * 0x10)) for j in range(9)) + '},')
P('};')
P('static const char *const rmg_side_letter[2] = {' + ', '.join(q(cs(0x1024aa40 + j * 0x10)) for j in range(2)) + '};')
P('static const char *const rmg_mode_letter[3] = {' + ', '.join(q(cs(0x1024aa70 + j * 0x10)) for j in range(3)) + '};')
P('static const char *const rmg_objective_name[34] = {' + ', '.join(q(cs(0x10249370 + j * 0x10)) for j in range(34)) + '};')
P('static const char *const rmg_terrain[21] = {' + ', '.join(q(cs(0x10249650 + j * 0x10)) for j in range(21)) + '};')
P('static const char *const rmg_allegiance[2] = {' + ', '.join(q(cs(0x10248f00 + j * 0x10)) for j in range(2)) + '};')
P('/* the mission name (0x10070502): word 1 by side and template (0x101a8ce0 / 0x101a8de8), word 2 1 .. 10, and the')
P(' * rare names by mission type (0x101a8ef0: the type\'s list, its first entry taken) */')
P('static const int rmg_name_word1[2][33] = {')
for s in range(2):
    p = u32(0x1020da08 + s * 4)
    P('    {' + ', '.join(str(i32(p - 0x194 + idv * 8)) for idv in range(0x33, 0x54)) + '},')
P('};')
P('static const char *const rmg_word1[13] = {' + ', '.join(q(cs(0x1024a5d0 + j * 0x10)) for j in range(13)) + '};')
P('static const char *const rmg_word2[11] = {' + ', '.join(q(cs(0x102498a0 + j * 0x10)) for j in range(11)) + '};')
P('static const char *const rmg_rare_name[13] = {' + ', '.join(q(cs(u32(0x101a8ef4 + t * 8))) for t in range(13)) + '};')
P('static const int rmg_rare_n[13] = {' + ', '.join(str(i32(0x101a8ef0 + t * 8)) for t in range(13)) + '};')
P('/* 0x101a8f58: the hours (from, to) by time of day */')
P('static const int rmg_hours[4][2] = {' + ', '.join('{%d, %d}' % (i32(0x101a8f58 + k * 8), i32(0x101a8f5c + k * 8)) for k in range(4)) + '};')
P('/* 0x101a99a0 (0x10065af2): the template\'s briefing movie index, 99-terminated; 0x10254d80 the names */')
rows = []; k = 0
while i32(0x101a99a0 + k * 12) != 99 and k < 64:
    rows.append('{%d, %d}' % (i32(0x101a99a0 + k * 12), i32(0x101a99a4 + k * 12))); k += 1
P('static const int rmg_tmpl_movie[][2] = {' + ', '.join(rows) + ', {99, -1}};')
P('static const char *const rmg_movie_name[16] = {' + ', '.join(q(cs(0x10254d80 + j * 0x10)) for j in range(16)) + '};')
P('/* 0x1020e428: the locale by region (0 .. 16) and the front line there: 24 entries (name, threshold) from the')
P(' * north; 0x1001a7978 the names, 32 bytes each */')
P('static const struct { int name; float t; } rmg_locale[17][24] = {')
for r in range(17):
    P('    {' + ', '.join('{%d, %r}' % (i32(0x1020e428 + r * 0x120 + j * 12), f32(0x1020e430 + r * 0x120 + j * 12)) for j in range(24)) + '},')
P('};')
o = d.find(b'Arctic Basin\0'); names = []
while True:
    t = d[o + len(names) * 32:o + len(names) * 32 + 32].split(b'\0')[0]
    if not t or len(names) >= 128: break
    names.append(t.decode('latin1'))
P('static const char *const rmg_locale_name[%d] = {' % len(names))
for j in range(0, len(names), 6):
    P('    ' + ', '.join(q(x) for x in names[j:j + 6]) + ',')
P('};')
P('\n#endif')
print('\n'.join(out))
