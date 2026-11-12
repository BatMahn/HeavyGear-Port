#include "smk.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *p;
    size_t n, bit;      /* bits read */
} bits;

static int b1(bits *b)
{
    if (b->bit >= b->n * 8) { b->bit++; return 0; }
    int v = (b->p[b->bit >> 3] >> (b->bit & 7)) & 1;
    b->bit++;
    return v;
}

static unsigned bn(bits *b, int n)
{
    unsigned v = 0;
    for (int i = 0; i < n; i++) v |= (unsigned)b1(b) << i;
    return v;
}

/* an 8-bit Huffman tree as a node array: node i is a leaf (value) or has children */
typedef struct {
    int16_t child[2][512];
    int16_t value[512];
    int     n, present;
} tree8;

static int t8_build(bits *b, tree8 *t, int depth)
{
    if (t->n >= 511 || depth > 32) return -1;
    int me = t->n++;
    if (b1(b)) {
        t->value[me] = -1;
        t->child[0][me] = (int16_t)t->n;
        if (t8_build(b, t, depth + 1) < 0) return -1;
        t->child[1][me] = (int16_t)t->n;
        if (t8_build(b, t, depth + 1) < 0) return -1;
    } else {
        t->value[me] = (int16_t)bn(b, 8);
    }
    return me;
}

static int t8_get(bits *b, const tree8 *t)
{
    if (!t->present) return 0;
    int i = 0;
    while (t->value[i] < 0) i = t->child[b1(b)][i];
    return t->value[i];
}

/* the big (16-bit) tree: a node array with the three "last value" leaves (the escapes) */
typedef struct {
    int32_t *node;      /* >= 0x40000000: an inner node, its right child's offset in the low bits; else a value */
    int      n, cap;
    int      last[3];   /* the leaves that hold the last three values */
    int      dummy;     /* where an absent escape points */
} bigtree;

#define NODE 0x40000000

static int bt_build(bits *b, bigtree *t, const tree8 *lo, const tree8 *hi, const int esc[3], int depth)
{
    if (depth > 64) return -1;
    if (t->n >= t->cap) {
        int nc = t->cap ? t->cap * 2 : 256;
        int32_t *nn = realloc(t->node, (size_t)nc * sizeof *nn);
        if (!nn) return -1;
        t->node = nn;
        t->cap = nc;
    }
    int me = t->n++;
    if (b1(b)) {
        if (bt_build(b, t, lo, hi, esc, depth + 1) < 0) return -1;
        int right = t->n - me - 1;   /* offset from the node past the left subtree */
        t->node[me] = NODE | right;
        if (bt_build(b, t, lo, hi, esc, depth + 1) < 0) return -1;
    } else {
        int v = t8_get(b, lo) | t8_get(b, hi) << 8;
        for (int i = 0; i < 3; i++)
            if (v == esc[i]) { t->last[i] = me; v = 0; }
        t->node[me] = v;
    }
    return 0;
}

static int bt_read(bits *b, bigtree *t)
{
    memset(t, 0, sizeof *t);
    t->last[0] = t->last[1] = t->last[2] = -1;
    if (!b1(b)) {
        /* no tree: every code is 0 */
        t->node = calloc(4, sizeof *t->node);
        if (!t->node) return -1;
        t->n = 1;
        t->dummy = 1;
        t->last[0] = t->last[1] = t->last[2] = 1;
        return 0;
    }
    static tree8 lo, hi;
    memset(&lo, 0, sizeof lo);
    memset(&hi, 0, sizeof hi);
    if (b1(b)) { lo.present = 1; if (t8_build(b, &lo, 0) < 0) return -1; b1(b); }
    if (b1(b)) { hi.present = 1; if (t8_build(b, &hi, 0) < 0) return -1; b1(b); }
    int esc[3];
    for (int i = 0; i < 3; i++) esc[i] = (int)bn(b, 16);
    if (bt_build(b, t, &lo, &hi, esc, 0) < 0) return -1;
    b1(b);
    /* the escapes not found point past the tree */
    if (t->n + 1 > t->cap) {
        int32_t *nn = realloc(t->node, (size_t)(t->n + 1) * sizeof *nn);
        if (!nn) return -1;
        t->node = nn;
        t->cap = t->n + 1;
    }
    t->dummy = t->n;
    t->node[t->dummy] = 0;
    for (int i = 0; i < 3; i++) if (t->last[i] < 0) t->last[i] = t->dummy;
    return 0;
}

static void bt_reset(bigtree *t)
{
    for (int i = 0; i < 3; i++) t->node[t->last[i]] = 0;
}

static int bt_get(bits *b, bigtree *t)
{
    int i = 0;
    while (t->node[i] & NODE) {
        if (b1(b)) i += t->node[i] & (NODE - 1);
        i++;
    }
    int v = t->node[i];
    if (v != t->node[t->last[0]]) {
        t->node[t->last[2]] = t->node[t->last[1]];
        t->node[t->last[1]] = t->node[t->last[0]];
        t->node[t->last[0]] = v;
    }
    return v;
}

struct smk {
    uint8_t *data;
    size_t   len;
    int      w, h, frames, flags;
    double   ms;
    uint32_t *size;
    uint8_t  *type;
    size_t   *off;
    bigtree  mmap, mclr, full, kind;
    uint8_t  *pix, pal[768];
    int      next;
};

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

void smk_free(smk *s)
{
    if (!s) return;
    free(s->data); free(s->size); free(s->type); free(s->off); free(s->pix);
    free(s->mmap.node); free(s->mclr.node); free(s->full.node); free(s->kind.node);
    free(s);
}

smk *smk_open(uint8_t *data, size_t len)
{
    if (!data || len < 104 || memcmp(data, "SMK2", 4)) { free(data); return NULL; }
    smk *s = calloc(1, sizeof *s);
    if (!s) { free(data); return NULL; }
    s->data = data; s->len = len;
    s->w = (int)le32(data + 4); s->h = (int)le32(data + 8); s->frames = (int)le32(data + 12);
    int32_t rate = (int32_t)le32(data + 16);
    s->ms = rate > 0 ? rate : rate < 0 ? -rate / 100.0 : 100.0;
    s->flags = (int)le32(data + 20);
    uint32_t trees = le32(data + 52);
    int nf = s->frames + (s->flags & 1);
    if (s->w <= 0 || s->h <= 0 || s->w > 4096 || s->h > 4096 || nf <= 0 || nf > 100000) goto bad;
    size_t at = 104;
    if (at + (size_t)nf * 5 + trees > len) goto bad;
    s->size = malloc((size_t)nf * sizeof *s->size);
    s->type = malloc((size_t)nf);
    s->off = malloc((size_t)nf * sizeof *s->off);
    s->pix = calloc((size_t)s->w * (size_t)s->h, 1);
    if (!s->size || !s->type || !s->off || !s->pix) goto bad;
    for (int i = 0; i < nf; i++) s->size[i] = le32(data + at + (size_t)i * 4);
    at += (size_t)nf * 4;
    memcpy(s->type, data + at, (size_t)nf);
    at += (size_t)nf;
    bits b = {data + at, trees, 0};
    if (bt_read(&b, &s->mmap) || bt_read(&b, &s->mclr) || bt_read(&b, &s->full) || bt_read(&b, &s->kind)) goto bad;
    at += trees;
    for (int i = 0; i < nf; i++) {
        s->off[i] = at;
        at += s->size[i] & ~3u;
        if (at > len) goto bad;
    }
    return s;
bad:
    smk_free(s);
    return NULL;
}

int smk_width(const smk *s) { return s->w; }
int smk_height(const smk *s) { return s->h; }
int smk_frames(const smk *s) { return s->frames; }
double smk_frame_ms(const smk *s) { return s->ms; }
const uint8_t *smk_pixels(const smk *s) { return s->pix; }
const uint8_t *smk_palette(const smk *s) { return s->pal; }

/* the 6-bit palette scale (smacker's table) */
static uint8_t pal6(int v) { v &= 0x3f; return (uint8_t)(v << 2 | v >> 4); }

static int palette(smk *s, const uint8_t *p, size_t n)
{
    uint8_t old[768];
    memcpy(old, s->pal, 768);
    size_t i = 0;
    for (int c = 0; c < 256 && i < n;) {
        int t = p[i++];
        if (t & 0x80) {
            c += (t & 0x7f) + 1;               /* kept */
        } else if (t & 0x40) {
            int k = (t & 0x3f) + 1;
            if (i >= n) return -1;
            int from = p[i++];
            for (; k-- && c < 256 && from < 256; c++, from++) memcpy(s->pal + c * 3, old + from * 3, 3);
        } else {
            if (i + 2 > n) return -1;
            s->pal[c * 3] = pal6(t); s->pal[c * 3 + 1] = pal6(p[i]); s->pal[c * 3 + 2] = pal6(p[i + 1]);
            i += 2;
            c++;
        }
    }
    return 0;
}

static const int block_run[64] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
    128, 256, 512, 1024, 2048};

static int decode(smk *s, int n)
{
    const uint8_t *p = s->data + s->off[n];
    size_t len = s->size[n] & ~3u;
    if (s->type[n] & 1) {
        if (!len) return -1;
        size_t pl = (size_t)p[0] * 4;
        if (pl > len || palette(s, p + 1, pl - 1)) return -1;
        p += pl; len -= pl;
    }
    for (int t = 0; t < 7; t++)
        if (s->type[n] & (2 << t)) {
            if (len < 4) return -1;
            size_t al = le32(p);
            if (al > len) return -1;
            p += al; len -= al;
        }
    bits b = {p, len, 0};
    bt_reset(&s->mmap); bt_reset(&s->mclr); bt_reset(&s->full); bt_reset(&s->kind);
    int bw = s->w / 4, bh = s->h / 4, blocks = bw * bh, blk = 0, stride = s->w;
    while (blk < blocks) {
        int type = bt_get(&b, &s->kind), run = block_run[(type >> 2) & 0x3f];
        switch (type & 3) {
        case 0:   /* mono */
            while (run-- && blk < blocks) {
                int clr = bt_get(&b, &s->mclr), map = bt_get(&b, &s->mmap);
                uint8_t hi = (uint8_t)(clr >> 8), lo = (uint8_t)clr;
                uint8_t *o = s->pix + (blk / bw) * 4 * stride + (blk % bw) * 4;
                for (int y = 0; y < 4; y++, o += stride)
                    for (int x = 0; x < 4; x++, map >>= 1) o[x] = map & 1 ? hi : lo;
                blk++;
            }
            break;
        case 1:   /* full */
            while (run-- && blk < blocks) {
                uint8_t *o = s->pix + (blk / bw) * 4 * stride + (blk % bw) * 4;
                for (int y = 0; y < 4; y++, o += stride) {
                    int v = bt_get(&b, &s->full);
                    o[2] = (uint8_t)v; o[3] = (uint8_t)(v >> 8);
                    v = bt_get(&b, &s->full);
                    o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8);
                }
                blk++;
            }
            break;
        case 2:   /* skip */
            blk += run;
            break;
        case 3: { /* fill */
            uint8_t c = (uint8_t)(type >> 8);
            while (run-- && blk < blocks) {
                uint8_t *o = s->pix + (blk / bw) * 4 * stride + (blk % bw) * 4;
                for (int y = 0; y < 4; y++, o += stride) memset(o, c, 4);
                blk++;
            }
            break;
        }
        }
    }
    return 0;
}

int smk_frame(smk *s, int n)
{
    if (n < 0 || n >= s->frames) return -1;
    if (n < s->next) {
        s->next = 0;
        memset(s->pal, 0, sizeof s->pal);
        memset(s->pix, 0, (size_t)s->w * (size_t)s->h);
    }
    while (s->next <= n)
        if (decode(s, s->next++)) return -1;
    return 0;
}
