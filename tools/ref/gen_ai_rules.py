import sys
# gen_ai_rules.py - writes src/ai_rules.h from the original HGSIM.DLL: python3 tools/ref/gen_ai_rules.py <HGSIM.DLL> [out]
import struct
d=open(sys.argv[1] if len(sys.argv) > 1 else 'HGSIM.DLL','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; ns=struct.unpack_from('<H',d,pe+6)[0]; so=pe+24+struct.unpack_from('<H',d,pe+20)[0]
secs=[struct.unpack_from('<8sIIII',d,so+i*40) for i in range(ns)]
def off(va):
    rva=va-0x10000000
    for n,vs,v,rs,ro in secs:
        if v<=rva<v+max(vs,rs): return ro+rva-v if rva-v<rs else None
def u32(va): o=off(va); return struct.unpack_from('<I',d,o)[0] if o is not None else 0
def s16(va): o=off(va); return struct.unpack_from('<h',d,o)[0] if o is not None else 0
def u8(va): o=off(va); return d[o] if o is not None else 0
sets=[u32(0x101072d8+i*4) for i in range(12)]
out=['/* ai_rules.h - the AI\'s mode rule sets, dumped from HGSIM.DLL by tools/ref/gen_ai_rules.py (do not edit).',
' * The 12 sets at 0x101072d8 (the table 0x10834e80 + 4 x k, 0x1000ed62): each a count, then (mode, rules) pairs',
' * and the rules, 12 bytes each: i16 condition (0x10107dd0), i16 a1, i16 a2, u8 action (0x10107df0), i16 the mode',
' * set, i16 its argument. The selectors (0x1000fc1a): per class, 16 entries of (the leader\'s set, the others\').',
' */','#ifndef HG_AI_RULES_H','#define HG_AI_RULES_H','',
'typedef struct ai_rule_s { short cond, a1, a2; unsigned char act; short mode, arg; } ai_rule;',
'typedef struct { short mode, n; const ai_rule *r; } ai_mode_rules;',
'typedef struct { int n; const ai_mode_rules *m; } ai_rule_set;','']
for k,base in enumerate(sets):
    n=s16(base); pairs=u32(base+2); rules=u32(base+6); acc=0; ms=[]
    for i in range(n):
        mode=s16(pairs+i*4); cnt=s16(pairs+i*4+2); rs=[]
        for j in range(cnt):
            r=rules+(acc+j)*12; rs.append((s16(r),s16(r+2),s16(r+4),u8(r+6),s16(r+7),s16(r+9)))
        out.append('static const ai_rule ai_set%d_m%d[%d] = {%s};   /* 0x%08x */'%(k+1,i,cnt,', '.join('{%d, 0x%x, %d, %d, %d, 0x%x}'%(a,b&0xffff,c,d,e,f&0xffff) for a,b,c,d,e,f in rs),rules+acc*12))
        ms.append((mode,cnt,'ai_set%d_m%d'%(k+1,i))); acc+=cnt
    out.append('static const ai_mode_rules ai_set%d[%d] = {%s};'%(k+1,n,', '.join('{%d, %d, %s}'%m for m in ms)))
out.append('/* index 0 none; k: set k (0x10834e84 + (k - 1) x 4) */')
out.append('static const ai_rule_set ai_sets[13] = {{0, 0}, %s};'%', '.join('{%d, ai_set%d}'%(s16(sets[k]),k+1) for k in range(12)))
out.append('/* the selectors by class (0x1000fc1a): 1 / 9 0x10107c10, 2 0x10107cd0, 3 0x10107c50, 4 / 10 0x10107d10, 5 / 8 0x10107d50, 6 0x10107c90 */')
for name,va in [("ai_sel_c10",0x10107c10),("ai_sel_c50",0x10107c50),("ai_sel_c90",0x10107c90),("ai_sel_cd0",0x10107cd0),("ai_sel_d10",0x10107d10),("ai_sel_d50",0x10107d50)]:
    out.append('static const short %s[16][2] = {%s};'%(name,', '.join('{%d, %d}'%(s16(va+i*4),s16(va+i*4+2)) for i in range(16))))
out+=['','#endif','']
open(sys.argv[2] if len(sys.argv) > 2 else 'src/ai_rules.h','w').write('\n'.join(out))
