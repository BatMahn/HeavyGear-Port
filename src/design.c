#include "design.h"

#include "fsutil.h"
#include "shell_picture.h"
#include "shell_tables.h"
#include "vdf.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static void wr32(uint8_t *p, int32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void wr16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static int type_of(int id) { return id >= 0 && id < SHELL_EQUIP_IDS ? shell_equip_type[id] : -1; }

/* ---- the items' node trees (the picture builders); names: the code's part suffix, "=" a whole name ---- */

typedef struct { const char *name; int parent; } tnode;

/* 0x1013281e */
static const tnode tree_torso[] = {{"TRHP", -1}, {"TRLP", 0}, {"TRRP", 0}, {"TRLR", 0}, {"TRRR", 0}, {NULL, 0}, {NULL, 0}, {"TRSO", 0}, {NULL, 7}, {NULL, 7}, {NULL, 7}, {NULL, 7}, {NULL, 7}, {NULL, 7}, {NULL, 7}, {NULL, 7}};
/* 0x1013319a */
static const tnode tree_engine[] = {{"ENBP", -1}, {NULL, 0}};
/* 0x10132da5 */
static const tnode tree_head[] = {{"HDHD", -1}, {"=EYE_HDHD", 0}};
/* 0x10133416 */
static const tnode tree_arms[] = {{NULL, -1}, {"LASH", 0}, {"LAUP", 1}, {"LALW", 2}, {NULL, 3}, {"LAHN", 3}, {NULL, 5}, {"LATH", 5}, {"LALK", 7}, {"LAUK", 8}, {"RASH", 0}, {"RAUP", 10}, {"RALW", 11}, {NULL, 12}, {"RAHN", 12}, {NULL, 14}, {"RATH", 14}, {"RALK", 16}, {"RAUK", 17}};
/* 0x10133d1d */
static const tnode tree_legs[] = {{NULL, -1}, {"LLUL", 0}, {"LLLL", 1}, {"LLRT", 2}, {"LLFT", 2}, {"=SOL_LLFT", 4}, {"RLUL", 0}, {"RLLL", 6}, {"RLRT", 7}, {"RLFT", 7}, {"=SOL_RLFT", 9}};
/* 0x101349fe, the code "SA11" */
static const tnode tree_mammoth[] = {{"TRHP", -1}, {"TRLB", 0}, {"LBFL", 1}, {"LBFT", 2}, {"LBRL", 1}, {"LBRT", 4}, {"LLUL", 1}, {"LLLL", 6}, {"LLFO", 7}, {"=SOL_LLFT", 8}, {"LLFT", 8}, {"LLLT", 8}, {"LLRT", 8}, {"LLBT", 8}, {"TRRB", 0}, {"RBFL", 14}, {"RBFT", 15}, {"RBRL", 14}, {"RBRT", 17}, {"RLUL", 14}, {"RLLL", 19}, {"RLFO", 20}, {"=SOL_RLFT", 21}, {"RLFT", 21}, {"RLLT", 21}, {"RLRT", 21}, {"RLBT", 21}, {"TRSO", 0}, {"=EYE_HDHD", 27}, {NULL, 27}, {NULL, 27}, {NULL, 27}, {"LASH", 27}, {"LAUP", 32}, {NULL, 33}, {"RASH", 27}, {"RAUP", 35}, {NULL, 36}};
/* 0x101358c4, the code "SN11" */
static const tnode tree_naga[] = {{"TRSO", -1}, {"=EYE_HDHD", 0}, {NULL, 0}, {NULL, 0}, {NULL, 0}, {"LLUL", 0}, {"LLLL", 5}, {"LLRW", 6}, {"LLFT", 6}, {"=SOL_LLFT", 8}, {"LLFW", 8}, {"RLUL", 0}, {"RLLL", 11}, {"RLRW", 12}, {"RLFT", 12}, {"=SOL_RLFT", 14}, {"RLFW", 14}, {"LASH", 0}, {NULL, 17}, {"RASH", 0}, {NULL, 19}};

/* ---- ODFs (0x1005c21d: the name, the file's first 300 bytes, its links, the 0x6c tail) ---- */

typedef struct { char name[16]; uint8_t *d; size_t len; int type, zone, nl, cls; } odf;

typedef struct { odf *o; int parent, child, sib; } inode;
#define NODES 48
typedef struct { inode n[NODES]; int count; int roots[2], nroots; } itree;

typedef struct { hg_archive *a; odf c[160]; int n; } odfs;

static odf *odf_get(odfs *s, const char *name)
{
    for (int i = 0; i < s->n; i++) if (!strcmp(s->c[i].name, name)) return s->c[i].d ? &s->c[i] : NULL;
    if (s->n >= 160) return NULL;
    odf *o = &s->c[s->n++];
    memset(o, 0, sizeof *o);
    snprintf(o->name, sizeof o->name, "%s", name);
    char lower[16];
    size_t k = 0;
    for (; name[k] && k + 1 < sizeof lower; k++) lower[k] = (char)tolower((unsigned char)name[k]);
    lower[k] = 0;
    size_t len;
    uint8_t *d = hga_load(s->a, lower, "ODF", &len, NULL);
    if (!d || len < 300) { free(d); return NULL; }
    o->d = d; o->len = len;
    o->type = rd32(d + 4);
    o->zone = rd32(d + 8);
    o->nl = rd32(d + 0x124);
    if (o->nl < 0 || o->nl > 10 || 300 + (size_t)o->nl * 0x48 + 4 > len) { o->nl = 0; }
    o->cls = rd32(d + 300 + (size_t)o->nl * 0x48);
    return o;
}

static void odfs_free(odfs *s)
{
    for (int i = 0; i < s->n; i++) free(s->c[i].d);
    s->n = 0;
}

/* an ODF copy with its own type (0x10134291 gives weapons their slot type by id) */
static odf *odf_typed(odfs *s, const char *name, int type)
{
    odf *o = odf_get(s, name);
    if (!o || o->type == type) return o;
    if (s->n >= 160) return NULL;
    odf *c = &s->c[s->n++];
    *c = *o;
    c->d = malloc(o->len);
    if (!c->d) { s->n--; return NULL; }
    memcpy(c->d, o->d, o->len);
    c->type = c->cls = type;
    snprintf(c->name, sizeof c->name, "%s", o->name);   /* found by the first lookup only */
    c->name[sizeof c->name - 1] = 0;
    return c;
}

/* 0x10134291: a torso weapon (class 0x48 / 0x49) or arm weapon (0x28) gets its slot type by its id */
static int weapon_type(int id, int cls)
{
    if (cls == 0x48 || cls == 0x49) {
        if (id >= 0x59 && id <= 0x67) return cls == 0x48 ? 0x21 : 0x22;
        if (id >= 0x68 && id <= 0x76) return cls == 0x48 ? 0x23 : 0x24;
        if (id >= 0xa7 && id <= 0xbd) return 0x22;
        if (id >= 0xe2 && id <= 0xf8) return 0x24;
    } else if (cls == 0x28) {
        if ((id >= 0xbe && id <= 0xe1) || (id >= 0x167 && id <= 0x18b)) return 0x26;
        if ((id >= 0xf9 && id <= 0x11c) || (id >= 0x18c && id <= 0x1b0)) return 0x27;
    }
    return -1;
}

/* the variant an item draws (0x1013804f's pairs: the bit picks the first) */
static int variant(int id, int bit)
{
    for (int i = 0; i < SHELL_PAIRS; i++) if (shell_pair[i][0] == id) return bit ? shell_pair[i][1] : shell_pair[i][2];
    return id;
}

static int is_pair(int id)
{
    for (int i = 0; i < SHELL_PAIRS; i++) if (shell_pair[i][0] == id) return 1;
    return 0;
}

static int item_tree(odfs *s, int id, int bit, itree *t)
{
    memset(t, 0, sizeof *t);
    int v = variant(id, bit);
    if (v < 0 || v >= SHELL_EQUIP_IDS || !shell_equip_code[v][0]) return -1;
    const tnode *spec = NULL;
    int n = 0;
    const char *code = shell_equip_code[v];
    int ty = type_of(v);
#define SPEC(a) (spec = a, n = (int)(sizeof a / sizeof a[0]))
    if (v == 7) { SPEC(tree_mammoth); code = "SA11"; }
    else if (v == 15) { SPEC(tree_naga); code = "SN11"; }
    else if (ty == 0) SPEC(tree_torso);
    else if (ty == 1) SPEC(tree_engine);
    else if (ty == 2) SPEC(tree_head);
    else if (ty == 3) SPEC(tree_arms);
    else if (ty == 4) SPEC(tree_legs);
#undef SPEC
    if (spec) {
        for (int i = 0; i < n && i < NODES; i++) {
            inode *q = &t->n[t->count++];
            q->parent = spec[i].parent;
            q->child = q->sib = -1;
            if (spec[i].name) {
                char nm[24];
                if (spec[i].name[0] == '=') snprintf(nm, sizeof nm, "%s", spec[i].name + 1);
                else snprintf(nm, sizeof nm, "%s%s", code, spec[i].name);
                q->o = odf_get(s, nm);
            }
            if (q->parent >= 0) {
                inode *p = &t->n[q->parent];
                if (p->child < 0) p->child = i;
                else { int c = p->child; while (t->n[c].sib >= 0) c = t->n[c].sib; t->n[c].sib = i; }
            }
        }
    } else {
        /* a weapon: its one ODF */
        odf *o = odf_get(s, code);
        if (!o) return -1;
        int wt = weapon_type(v, o->cls);
        if (wt >= 0) o = odf_typed(s, code, wt);
        t->n[0].o = o;
        t->n[0].parent = t->n[0].child = t->n[0].sib = -1;
        t->count = 1;
    }
    /* the instances: the root, or an empty root's children (0x1011cab7) */
    if (t->n[0].o) { t->roots[0] = 0; t->nroots = 1; }
    else for (int c = t->n[0].child; c >= 0 && t->nroots < 2; c = t->n[c].sib) t->roots[t->nroots++] = c;
    return t->nroots ? 0 : -1;
}

/* ---- loading ---- */

int design_load(design *g, hg_archive *a, const char *gdf)
{
    memset(g, 0, sizeof *g);
    size_t len;
    uint8_t *d = hga_load(a, gdf, "GDF", &len, NULL);
    if (!d || len < 0x88 || rd32(d) != 0) { free(d); return -1; }
    size_t o = 0x88 + (size_t)(uint32_t)rd32(d + 0x84) * 0x438;
    int neq = o + 0x3c <= len ? rd32(d + o + 0x24) : -1;
    size_t t = o + 0x3c + (size_t)(neq > 0 ? neq : 0) * 0x6c;
    if (neq < 0 || t + 4 > len) { free(d); return -1; }
    snprintf(g->name, sizeof g->name, "%.31s", (const char *)d + o + 4);
    g->side = rd32(d + o + 0x2c);
    int tl = rd32(d + t);
    const uint8_t *p = d + t + 4;
    if (tl < 2 || t + 4 + (size_t)tl > len) { free(d); return -1; }
    g->b30 = p[0];
    g->b31 = p[1];
    /* 0x100c0a61: the items in order, each hung on the instance of the given preorder index */
    int ninst = 0, inst_item[DESIGN_ITEMS * 2], inst_k[DESIGN_ITEMS * 2];
    /* the instances' preorder: rebuilt after each item (its parent precedes it, so the partial order is the final) */
    for (int k = 0; k < (tl - 2) / 0x12 && g->nitem < DESIGN_ITEMS; k++) {
        const uint8_t *r = p + 2 + k * 0x12;
        int sid = (int16_t)(r[0] | r[1] << 8), par = (int16_t)(r[2] | r[3] << 8);
        design_item *it = &g->item[g->nitem];
        memset(it, 0, sizeof *it);
        it->id = sid >> 1;
        it->bit = sid & 1;
        it->parent = it->pinst = -1;
        if (par >= 0) {
            /* the preorder of the instances so far */
            int order[DESIGN_ITEMS * 2], no = 0;
            int stack[DESIGN_ITEMS * 2], sp = 0;
            for (int i = ninst - 1; i >= 0; i--) if (g->item[inst_item[i]].parent < 0) stack[sp++] = i;
            while (sp && no < DESIGN_ITEMS * 2) {
                int i = stack[--sp];
                order[no++] = i;
                for (int j = ninst - 1; j >= 0; j--)
                    if (g->item[inst_item[j]].parent == inst_item[i] && g->item[inst_item[j]].pinst == inst_k[i]) stack[sp++] = j;
            }
            if (par >= no) { free(d); return -1; }
            it->parent = inst_item[order[par]];
            it->pinst = inst_k[order[par]];
        }
        memcpy(it->slot, r + 8, 4);
        it->armor = (int16_t)(r[12] | r[13] << 8);
        it->options = rd32(r + 14);
        int n = 1;
        int ty = type_of(it->id);
        if (ty == 3 || ty == 4) n = 2;
        for (int q = 0; q < n && ninst < DESIGN_ITEMS * 2; q++) { inst_item[ninst] = g->nitem; inst_k[ninst] = q; ninst++; }
        g->nitem++;
    }
    free(d);
    int torso = design_part(g, 0);
    g->model = torso == 7 ? 2 : torso == 15 ? 3 : 1;
    return g->nitem ? 0 : -1;
}

void design_free(design *g)
{
    free(g->gdf);
    memset(g, 0, sizeof *g);
}

int design_part(const design *g, int type)
{
    for (int i = 0; i < g->nitem; i++) if (type_of(g->item[i].id) == type) return g->item[i].id;
    return -1;
}

/* ---- building ---- */

typedef struct {
    int item, k;         /* the design item and its instance */
    itree *t;
    int root;            /* the tree node */
    int rec;             /* its record */
} inst;

typedef struct {
    design *g;
    odfs    s;
    vdf    *v;
    itree   tree[DESIGN_ITEMS];
    inst    in[DESIGN_ITEMS * 2];
    int     nin;
    int     order[DESIGN_ITEMS * 2], norder;   /* the instances' preorder */
    int     cur_item;
} builder;

static int rec_child(const vdf *v, int r) { return r >= 0 ? rd32(v->rec[r] + 0x78) : 0; }
static int rec_sib(const vdf *v, int r) { return rd32(v->rec[r] + 0x54); }

/* 0x100c0121: the record of instance i's n-th attach point (its empty nodes in preorder); -1 none */
static int attach_walk(const builder *b, const itree *t, int node, int rec, int *count, int n)
{
    if (node < 0 || rec < 0) return -1;
    const inode *q = &t->n[node];
    if (!q->o) { if (++*count == n + 1) return rec; return -1; }
    int c = q->child, r = rec_child(b->v, rec);
    for (; c >= 0; c = t->n[c].sib) {
        if (r <= 0) return -1;
        int x = attach_walk(b, t, c, r, count, n);
        if (x >= 0) return x;
        r = rec_sib(b->v, r);
    }
    return -1;
}

static int attach_rec(const builder *b, int i, int n)
{
    int count = 0;
    return attach_walk(b, b->in[i].t, b->in[i].root, b->in[i].rec, &count, n);
}

static int find_inst(const builder *b, int item, int k)
{
    for (int i = 0; i < b->nin; i++) if (b->in[i].item == item && b->in[i].k == k) return i;
    return -1;
}

static void preorder(builder *b)
{
    b->norder = 0;
    int stack[DESIGN_ITEMS * 2], sp = 0;
    for (int i = b->nin - 1; i >= 0; i--) if (b->g->item[b->in[i].item].parent < 0) stack[sp++] = i;
    while (sp) {
        int i = stack[--sp];
        b->order[b->norder++] = i;
        const inst *p = &b->in[i];
        for (int j = b->nin - 1; j >= 0; j--) {
            const design_item *it = &b->g->item[b->in[j].item];
            if (it->parent == p->item && it->pinst == p->k) stack[sp++] = j;
        }
    }
}

static int make_instances(builder *b)
{
    b->nin = 0;
    for (int i = 0; i < b->g->nitem; i++) {
        design_item *it = &b->g->item[i];
        if (item_tree(&b->s, it->id, it->bit, &b->tree[i])) return -1;
        itree *t = &b->tree[i];
        for (int k = 0; k < t->nroots; k++) {
            int rec = 0;
            if (it->parent >= 0) {
                int pi = find_inst(b, it->parent, it->pinst);
                if (pi < 0) return -1;
                rec = attach_rec(b, pi, it->slot[k]);
                if (rec < 0) return -1;
            }
            b->in[b->nin++] = (inst){i, k, t, t->roots[k], rec};
        }
    }
    preorder(b);
    return 0;
}

static void put_odf(uint8_t *r, const odf *o)
{
    memset(r + 0x14, 0, 0x20);
    snprintf((char *)r + 0x14, 0x20, "%s", o->name);
    memcpy(r + 0x7c, o->d + 0xc, 0xc0 + 0x2c);
    memset(r + 0x168, 0, 0x2d0);
    if (o->nl) memcpy(r + 0x168, o->d + 300, (size_t)o->nl * 0x48);
}

/* a POF part (0x101366b1: its four models named after it) */
static void put_pof(builder *b, int rec, int k, int seg)
{
    odf *o = odf_get(&b->s, shell_pof[k]);
    if (!o || rec <= 0) return;
    uint8_t *r = b->v->rec[rec];
    if (rec < 96) b->g->rec_item[rec] = b->cur_item;
    wr32(r + 0x10, o->zone);
    wr32(r + 8, seg);
    wr32(r + 4, 1);
    put_odf(r, o);
    for (int s = 0; s < 4; s++) { memset(r + 0x7c + s * 0x20, 0, 0x20); snprintf((char *)r + 0x7c + s * 0x20, 0x20, "%s", shell_pof[k]); }
}

/* 0x100bfa60 */
static void fill(builder *b, const inst *in, int node, int rec)
{
    if (node < 0 || rec < 0 || (rec == 0 && node != in->root)) return;
    const inode *q = &in->t->n[node];
    uint8_t *r = b->v->rec[rec];
    int id = b->g->item[in->item].id;
    if (q->o && rec < 96) b->g->rec_item[rec] = in->item;
    if (!q->o) memset(r + 0x14, 0, 0x20);
    else if (id == 0x79 || id == 0x7a || id == 0x7b) {
        put_pof(b, rec, 3, 0x37);   /* the grenades: POF_HWLH in the slot itself */
    } else {
        int ty = q->o->type, pk = -1, pseg = 0;
        static const int zone6[] = {0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x4a, 0x4c, 0x4d, 0x50, 0x51, 0x54, 0x56, 0x57, 0x5a, 0x5b};
        static const int zone5[] = {0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x4b, 0x4e, 0x4f, 0x52, 0x53, 0x55, 0x58, 0x59, 0x5c, 0x5d};
        int z = -1, seg = ty;
        if (ty == 1 || ty == 2) z = 2;
        else if (ty == 3) z = 1;
        else if (ty >= 5 && ty <= 0xc) z = 4;
        else if (ty >= 0xd && ty <= 0x14) z = 3;
        else if (ty == 0x20) z = 7;
        for (size_t i = 0; i < sizeof zone6 / sizeof *zone6; i++) if (ty == zone6[i]) z = 6;
        for (size_t i = 0; i < sizeof zone5 / sizeof *zone5; i++) if (ty == zone5[i]) z = 5;
        if ((ty >= 0x21 && ty <= 0x25) || ty == 0x48) {
            /* by the instance's root type (0x21 / 0x48 left upper, 0x22 left lower, 0x23 right upper, 0x24 right lower) */
            int s = in->t->n[in->root].o ? in->t->n[in->root].o->type : 0;
            if (s == 0x21 || s == 0x48) { seg = 0x21; z = 0xe; pk = 6; pseg = 0x38; }
            else if (s == 0x22) { seg = 0x22; z = 0x10; pk = 5; pseg = 0x39; }
            else if (s == 0x23) { seg = 0x23; z = 0xd; pk = 8; pseg = 0x3a; }
            else if (s == 0x24) { seg = 0x24; z = 0xf; pk = 7; pseg = 0x3b; }
            else seg = -2;
        } else if (ty == 0x26) { z = 0xb; pk = 0; pseg = 0x35; }
        else if (ty == 0x27) { z = 9; pk = 1; pseg = 0x34; }
        else if (ty == 0x29) { z = 0xc; pk = 4; pseg = 0x37; }
        else if (ty == 0x2a || ty == 0x2b) { seg = 0x2a; z = 10; pk = 4; pseg = 0x36; }
        else if (ty == 0x2c) { z = 8; pk = 2; pseg = 0x33; }
        if (z >= 0 && seg != -2) { wr32(r + 8, seg); wr32(r + 0x10, z); }
        if (pk >= 0) put_pof(b, rd32(r + 0x78), pk, pseg);
        put_odf(r, q->o);
    }
    fill(b, in, q->child, rd32(r + 0x78));
    if (node != in->root) fill(b, in, q->sib, rd32(r + 0x54));
}

/* 0x10119e00: a new item's options */
static int default_options(int id)
{
    switch (type_of(id) == 0 ? 0 : type_of(id) == 1 ? 1 : type_of(id) == 3 ? 2 : type_of(id) == 4 ? 3 : type_of(id) == 2 ? 4 : 5) {
    case 0: return 0x10;                                                  /* 0x1011a1b0(6, 10, 0xb, 0) */
    case 1: return 0x2d << 16 | (shell_equip_field[id][6] & 0xffff);      /* 0x1011a55e(0x2d, +0x36) */
    case 2: case 3: return 0;                                             /* 0x1011a75b / 0x1011aaca of the bases */
    case 4: return 2;                                                     /* 0x1011aec9(0, 1, 0 ...) */
    default: return 0x2f;                                                 /* 0x1011b444(0x2f, 0) */
    }
}

/* 0x100bf681: an instance's equipment record kind */
static int record_kind(const builder *b, const inst *in)
{
    int id = b->g->item[in->item].id, ty = type_of(id);
    int rt = in->t->n[in->root].o ? in->t->n[in->root].o->type : 0;
    switch (ty) {
    case 0: return 2;
    case 1: return 7;
    case 3: return rt == 6 ? 4 : 3;
    case 4: return rt == 0x17 ? 6 : 5;
    case 2: return 1;
    default:
        if (id == 0x79 || id == 0x7a || id == 0x7b) return 0xc;
        switch (rt) {
        case 0x21: case 0x48: return 0xe;
        case 0x22: return 0x10;
        case 0x23: return 0xd;
        case 0x24: return 0xf;
        case 0x26: return 0xb;
        case 0x27: return 9;
        case 0x29: return 0xc;
        case 0x2a: case 0x2b: return 10;
        case 0x2c: return 8;
        }
        return 0;
    }
}

static int load_vdf(hg_archive *a, int model, vdf *v)
{
    static const char *const file[4] = {"", "GEAR.VDF", "MAMMOTH.VDF", "NAGA.VDF"};
    char path[1100];
    if (model < 1 || model > 3 || fs_find_ci(hga_dir(a), file[model], path, sizeof path)) return -1;
    size_t len;
    uint8_t *d = fs_read_all(path, &len);
    int r = d ? vdf_parse((const char *)d, len, v) : -1;
    free(d);
    return r;
}

static void free_builder(builder *b)
{
    odfs_free(&b->s);
    free(b->v);
}

static int start(builder *b, design *g, hg_archive *a)
{
    memset(b, 0, sizeof *b);
    b->g = g;
    b->s.a = a;
    b->v = malloc(sizeof *b->v);
    if (!b->v || load_vdf(a, g->model, b->v)) return -1;
    return make_instances(b);
}

int design_build(design *g, hg_archive *a)
{
    builder *b = calloc(1, sizeof *b);
    if (!b) return -1;
    if (start(b, g, a)) { free_builder(b); free(b); return -1; }
    vdf *v = b->v;
    /* 0x100c01c9: the records cleared, then filled */
    for (int i = 0; i < v->n; i++) {
        uint8_t *r = v->rec[i];
        wr32(r + 0x10, 0);
        memset(r + 0x14, 0, 0x20);
        memset(r + 0x7c, 0, 0xc0);
        memset(r + 0x13c, 0, 0x2c);
        memset(r + 0x168, 0, 0x2d0);
    }
    for (int r = 0; r < 96; r++) g->rec_item[r] = -1;
    for (int i = 0; i < b->nin; i++) { b->cur_item = b->in[i].item; fill(b, &b->in[i], b->in[i].root, b->in[i].rec); }
    /* the equipment records */
    int nrec = b->norder + (g->model != 1 ? 6 : 0);
    size_t gb = 0x88 + (size_t)v->n * 0x438, er = gb + 0x3c, tr = er + (size_t)nrec * 0x6c;
    /* the items in the instances' preorder (0x100c03b9), each at its first instance */
    int items[DESIGN_ITEMS], ni = 0, seen[DESIGN_ITEMS] = {0};
    for (int o = 0; o < b->norder; o++) {
        int it = b->in[b->order[o]].item;
        if (!seen[it]) { seen[it] = 1; items[ni++] = it; }
    }
    size_t len = tr + 4 + 2 + (size_t)ni * 0x12;
    uint8_t *d = calloc(1, len);
    if (!d) { free_builder(b); free(b); return -1; }
    memcpy(d, v->head, 0x88);
    for (int i = 0; i < v->n; i++) {
        uint8_t *r = v->rec[i];
        memcpy(d + 0x88 + (size_t)i * 0x438, r, 0x438);
    }
    for (int o = 0; o < b->norder; o++) {
        const inst *in = &b->in[b->order[o]];
        const design_item *it = &g->item[in->item];
        uint8_t *q = d + er + (size_t)o * 0x6c;
        wr32(q, record_kind(b, in));
        wr32(q + 4, it->id);
        wr32(q + 8, it->armor);
        wr32(q + 0xc, it->options);
        wr32(q + 0x64, g->b31);
        wr32(q + 0x68, g->b30);
    }
    if (g->model != 1) {
        /* FUN_100c04e5: a strider's engine, arms, legs and head */
        static const int ids[2][4] = {{0x17, 0x37, 0x47, 0x27}, {0x1f, 0x3f, 0x4f, 0x2f}};
        static const int kinds[6] = {7, 4, 3, 6, 5, 1}, which[6] = {0, 1, 1, 2, 2, 3};
        for (int k = 0; k < 6; k++) {
            int id = ids[g->model - 2][which[k]];
            uint8_t *q = d + er + (size_t)(b->norder + k) * 0x6c;
            wr32(q, kinds[k]);
            wr32(q + 4, id);
            wr32(q + 8, shell_equip_field[id][2]);
            wr32(q + 0xc, default_options(id));
            wr32(q + 0x64, g->b31);
            wr32(q + 0x68, g->b30);
        }
    }
    /* the gear block (0x100c04e5: version 2, the name, the count, class 1, the side) */
    wr32(d + gb, 2);
    snprintf((char *)d + gb + 4, 0x20, "%s", g->name);
    wr32(d + gb + 0x24, nrec);
    wr32(d + gb + 0x28, 1);
    wr32(d + gb + 0x2c, g->side);
    /* the trailing data (0x100c025b) */
    wr32(d + tr, 2 + ni * 0x12);
    d[tr + 4] = g->b30;
    d[tr + 5] = g->b31;
    for (int k = 0; k < ni; k++) {
        const design_item *it = &g->item[items[k]];
        uint8_t *q = d + tr + 6 + (size_t)k * 0x12;
        wr16(q, it->id << 1 | (it->bit & 1));
        int par = -1;
        if (it->parent >= 0) {
            int pi = find_inst(b, it->parent, it->pinst);
            for (int o = 0; o < b->norder; o++) if (b->order[o] == pi) par = o;
        }
        wr16(q + 2, par);
        memcpy(q + 8, it->slot, 4);
        wr16(q + 12, it->armor);
        wr32(q + 14, it->options);
    }
    free(g->gdf);
    g->gdf = d;
    g->len = len;
    free_builder(b);
    free(b);
    return 0;
}

/* ---- dropping ---- */

typedef struct { int item, k, local, rec; } point;

/* the attach points of item i in order (its instances', each in preorder), with the classes they take */
static int points(const builder *b, int item, point *p, int max, int (*cls)[8], int *ncls)
{
    int n = 0;
    for (int ii = 0; ii < b->nin; ii++) {
        const inst *in = &b->in[ii];
        if (in->item != item) continue;
        /* the instance's subtree: each empty leaf, numbered, its classes from the parent part's link */
        int stack[NODES], sp = 0, local = 0;
        stack[sp++] = in->root;
        /* preorder by walking with an explicit order */
        int ord[NODES], no = 0;
        while (sp) {
            int x = stack[--sp];
            ord[no++] = x;
            int kids[NODES], nk = 0;
            for (int c = in->t->n[x].child; c >= 0; c = in->t->n[c].sib) kids[nk++] = c;
            while (nk) stack[sp++] = kids[--nk];
        }
        for (int o = 0; o < no && n < max; o++) {
            int x = ord[o];
            const inode *q = &in->t->n[x];
            if (q->o || q->child >= 0 || q->parent < 0) continue;
            const odf *po = in->t->n[q->parent].o;
            int pos = 0;
            for (int c = in->t->n[q->parent].child; c >= 0 && c != x; c = in->t->n[c].sib) pos++;
            ncls[n] = 0;
            if (po && pos < po->nl) {
                const uint8_t *l = po->d + 300 + (size_t)pos * 0x48;
                int cnt = rd32(l + 0x30);
                for (int c = 0; c < cnt && c < 5; c++) cls[n][ncls[n]++] = rd32(l + 0x34 + c * 4);
            }
            p[n] = (point){item, in->k, local, attach_rec(b, ii, local)};
            local++;
            n++;
        }
    }
    return n;
}

static int fits(int type, const int *cls, int n)
{
    for (int i = 0; i < n; i++) if (cls[i] == type || (type == 0x49 && (cls[i] == 0x22 || cls[i] == 0x24))) return 1;
    return 0;
}

/* the item on (item, k)'s attach point `local`, -1 none */
static int occupant(const design *g, int item, int k, int local)
{
    for (int i = 0; i < g->nitem; i++) {
        const design_item *it = &g->item[i];
        if (it->parent != item || it->pinst != k) continue;
        int n = type_of(it->id) == 3 || type_of(it->id) == 4 ? 2 : 1;
        for (int q = 0; q < n; q++) if (it->slot[q] == local) return i;
    }
    return -1;
}

/* remove item i and everything hung on it (0x100bf1dd) */
void design_remove(design *g, int i)
{
    for (int j = g->nitem - 1; j >= 0; j--) if (g->item[j].parent == i) { design_remove(g, j); if (j < i) i--; }
    for (int j = i + 1; j < g->nitem; j++) g->item[j - 1] = g->item[j];
    g->nitem--;
    for (int j = 0; j < g->nitem; j++) if (g->item[j].parent > i) g->item[j].parent--;
}

int design_drop(design *g, hg_archive *a, int id, float mx, float my, const float (*pos)[2])
{
    if (type_of(id) < 0) return -1;
    if (id == 7 || id == 15 || type_of(id) == 0) {
        /* a torso: a new design (FUN_100bec8a with no parent clears it) */
        g->nitem = 0;
        design_item *it = &g->item[g->nitem++];
        memset(it, 0, sizeof *it);
        it->id = id;
        it->parent = it->pinst = -1;
        it->armor = shell_equip_field[id][2];
        it->options = default_options(id);
        g->model = id == 7 ? 2 : id == 15 ? 3 : 1;
        g->dirty = 1;
        return 0;
    }
    builder *b = calloc(1, sizeof *b);
    if (!b) return -1;
    if (start(b, g, a)) { free_builder(b); free(b); return -1; }
    int nvar = is_pair(id) ? 2 : 1;
    itree dt[2];
    for (int vv = 0; vv < nvar; vv++) if (item_tree(&b->s, id, nvar == 2 ? 1 - vv : 0, &dt[vv])) { free_builder(b); free(b); return -1; }
    int best = -1, best_bit = 0;
    unsigned char best_slot[4] = {0};
    float bd = 3.4e38f;
    for (int target = 0; target < g->nitem; target++) {
        point p[24];
        int cls[24][8], ncls[24];
        int np = points(b, target, p, 24, cls, ncls);
        int ok[2] = {0, 0};
        unsigned char sl[2][4] = {{0}};
        int first[2] = {-1, -1};
        for (int vv = 0; vv < nvar; vv++) {
            ok[vv] = 1;
            for (int k = 0; k < dt[vv].nroots && ok[vv]; k++) {
                int rt = dt[vv].n[dt[vv].roots[k]].o ? dt[vv].n[dt[vv].roots[k]].o->type : -1, j = 0;
                while (j < np && !fits(rt, cls[j], ncls[j])) j++;
                if (j >= np) ok[vv] = 0;
                else { sl[vv][k] = (unsigned char)p[j].local; if (!k) first[vv] = j; }
            }
        }
        int use = -1;
        if (nvar == 1) use = ok[0] ? 0 : -1;
        else {
            /* 0x100c515d: a free point first, then one not holding the same id, then the first variant */
            int o0 = ok[0] ? occupant(g, target, p[first[0]].k, p[first[0]].local) : -1;
            int o1 = ok[1] ? occupant(g, target, p[first[1]].k, p[first[1]].local) : -1;
            if (ok[0] && ok[1]) {
                if (o0 >= 0 && o1 >= 0) {
                    if (g->item[o0].id == id) ok[0] = 0;
                    if (g->item[o1].id == id) ok[1] = 0;
                    if (ok[0] && ok[1]) ok[1] = 0;
                } else if (o0 < 0) ok[1] = 0;
                else ok[0] = 0;
            }
            use = ok[0] ? 0 : ok[1] ? 1 : -1;
        }
        if (use < 0) continue;
        int j = first[use];
        float d = 0;
        if (pos && p[j].rec >= 0) { float dx = pos[p[j].rec][0] - mx, dy = pos[p[j].rec][1] - my; d = dx * dx + dy * dy; }
        if (best < 0 || d < bd) {
            bd = d; best = target;
            best_bit = nvar == 2 ? 1 - use : 0;
            memcpy(best_slot, sl[use], 4);
            /* the instance the point belongs to */
            best_slot[3] = (unsigned char)p[j].k;
        }
    }
    free_builder(b);
    free(b);
    if (best < 0 || g->nitem >= DESIGN_ITEMS) return -1;
    int pk = best_slot[3];
    best_slot[3] = 0;
    /* what is on the point goes (0x100bf1dd) */
    int occ = occupant(g, best, pk, best_slot[0]);
    if (occ >= 0) { design_remove(g, occ); if (occ < best) best--; }
    design_item *it = &g->item[g->nitem++];
    memset(it, 0, sizeof *it);
    it->id = id;
    it->bit = best_bit;
    it->parent = best;
    it->pinst = pk;
    memcpy(it->slot, best_slot, 3);
    it->armor = shell_equip_field[id][2];
    it->options = default_options(id);
    g->dirty = 1;
    return 0;
}

void design_clear(design *g)
{
    g->nitem = 0;
    g->dirty = 1;
}

/* ---- saving ---- */

static int is_stock(const char *file)
{
    for (int i = 0; i < SHELL_STOCK_GEARS; i++) {
        size_t m = strlen(shell_stock_gear[i]), k = 0;
        for (; k < m && file[k] && toupper((unsigned char)file[k]) == shell_stock_gear[i][k]; k++) {}
        if (k == m && (file[k] == 0 || file[k] == '.')) return 1;
    }
    return 0;
}

void design_save_name(const char *dir, const char *current, const char *name, char *out, size_t n)
{
    if (current && current[0] && !is_stock(current)) {
        if (strchr(current, '.')) snprintf(out, n, "%s", current);
        else snprintf(out, n, "%s.GDF", current);
        return;
    }
    for (int k = 0; k < 100000; k++) {
        char num[8], base[16];
        snprintf(num, sizeof num, "%d", k);
        int room = 8 - (int)strlen(num), m = 0;
        for (; name && name[m] && m < room; m++) base[m] = name[m];
        for (; m < room; m++) base[m] = '_';
        base[m] = 0;
        snprintf(out, n, "%s%s.GDF", base, num);
        char path[1100];
        if (fs_find_ci(dir, out, path, sizeof path)) return;
    }
}

int design_write(const design *g, const char *dir, const char *file)
{
    if (!g->gdf) return -1;
    char path[1100];
    fs_join(path, sizeof path, dir, file);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(g->gdf, 1, g->len, f) == g->len;
    return fclose(f) == 0 && ok ? 0 : -1;
}
