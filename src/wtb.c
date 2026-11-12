/*
 * wtb.c - WTBO parser (HGSIM.DLL 0x100c5e73).
 */
#include "wtb.h"

#include "bytes.h"

#include <stdlib.h>
#include <string.h>

static void copy_name(char *dst, const uint8_t *src, int n)
{
    /* stored negated when the first byte is above 0x80 (0x100c5e73: 0x100 - c for each of the 16 bytes) */
    int neg = src[0] > 0x80;
    int k;
    for (k = 0; k < n; k++) {
        uint8_t c = neg ? (uint8_t)(0x100 - src[k]) : src[k];
        if (!c) break;
        dst[k] = (char)c;
    }
    dst[k] = 0;
}

/* HG layout: 44-byte vertices, 0x38/0x3e-byte polygons */
static long parse_hg(const uint8_t *d, size_t len, wtb_object *o)
{
    int nv = o->nverts, np = o->npolys;
    size_t p = 0x20 + (size_t)nv * 44;
    if (p > len) return -1;
    o->v = calloc((size_t)(nv ? nv : 1), sizeof *o->v);
    o->p = calloc((size_t)(np ? np : 1), sizeof *o->p);
    for (int i = 0; i < nv; i++) {
        const uint8_t *q = d + 0x20 + (size_t)i * 44;
        wtb_vertex *v = &o->v[i];
        v->x = rd_f32(q); v->y = rd_f32(q + 4); v->z = rd_f32(q + 8);
        v->nx = (float)-rd_f64(q + 12); v->ny = (float)-rd_f64(q + 20); v->nz = (float)-rd_f64(q + 28);
        v->u = rd_f32(q + 36); v->v = rd_f32(q + 40);
    }
    for (int i = 0; i < np; i++) {
        if (p + 0x30 > len) return -1;
        const uint8_t *q = d + p;
        wtb_poly *g = &o->p[i];
        int n = rd_u16(q + 0x2e);
        size_t sz = n < 5 ? 0x38 : 0x3e;
        if (n < 1 || n > WTB_MAX_POLY || p + sz > len) return -1;
        memcpy(g->label, q, 16);
        g->label[16] = 0;
        g->f10 = rd_u16(q + 0x10);
        memcpy(g->rgb, q + 0x13, 3);
        g->nx = (float)-rd_f64(q + 0x16); g->ny = (float)-rd_f64(q + 0x1e); g->nz = (float)-rd_f64(q + 0x26);
        g->n = n;
        for (int k = 0; k < n; k++) {
            g->idx[k] = rd_u16(q + 0x30 + k * 2);
            if (g->idx[k] >= nv) return -1;
        }
        p += sz;
    }
    return (long)p;
}

/* MW2 DOS layout (the 45 TL1MBA* tiles): 16-byte vertices { i32 x, y, z; u16, u16 }, 12/18-byte polygons
 * { u16 colour, u16 n, u16 idx[4 or 7] } */
static long parse_legacy(const uint8_t *d, size_t len, wtb_object *o)
{
    int nv = o->nverts, np = o->npolys;
    size_t p = 0x20 + (size_t)nv * 16;
    if (p > len) return -1;
    o->v = calloc((size_t)(nv ? nv : 1), sizeof *o->v);
    o->p = calloc((size_t)(np ? np : 1), sizeof *o->p);
    for (int i = 0; i < nv; i++) {
        const uint8_t *q = d + 0x20 + (size_t)i * 16;
        o->v[i].x = (float)rd_i32(q); o->v[i].y = (float)rd_i32(q + 4); o->v[i].z = (float)rd_i32(q + 8);
    }
    for (int i = 0; i < np; i++) {
        if (p + 4 > len) return -1;
        const uint8_t *q = d + p;
        wtb_poly *g = &o->p[i];
        int n = rd_u16(q + 2);
        size_t sz = n <= 4 ? 12 : 18;
        if (n < 1 || n > WTB_MAX_POLY || p + sz > len) return -1;
        g->f10 = rd_u16(q);    /* the colour word */
        g->n = n;
        for (int k = 0; k < n; k++) {
            g->idx[k] = rd_u16(q + 4 + k * 2);
            if (g->idx[k] >= nv) return -1;
        }
        p += sz;
    }
    return (long)p;
}

int wtb_parse(const uint8_t *d, size_t len, wtb_model *out)
{
    memset(out, 0, sizeof *out);
    size_t off = 0;
    int cap = 0;
    while (off + 0x20 <= len) {
        const uint8_t *h = d + off;
        if (memcmp(h, "WTBO", 4)) break;
        if (out->count == cap) {
            cap = cap ? cap * 2 : 2;
            out->obj = realloc(out->obj, (size_t)cap * sizeof *out->obj);
        }
        wtb_object *o = &out->obj[out->count++];
        memset(o, 0, sizeof *o);
        copy_name(o->name, h + 8, 16);
        o->nverts = rd_i16(h + 0x18);
        o->npolys = rd_i16(h + 0x1a);
        o->flags = rd_u16(h + 0x1c);
        if (o->nverts < 0 || o->npolys < 0) { wtb_free(out); return -1; }
        long used = parse_hg(h, len - off, o);
        if (used < 0 || (used != (long)(len - off) && memcmp(h + used, "WTBO", 4))) {
            /* not the HG layout to the byte: try MW2's */
            free(o->v); free(o->p); o->v = NULL; o->p = NULL;
            used = parse_legacy(h, len - off, o);
            if (used < 0) { wtb_free(out); return -1; }
            o->legacy = 1;
        }
        off += (size_t)used;
    }
    if (!out->count || off != len) { wtb_free(out); return -1; }
    return 0;
}

void wtb_free(wtb_model *m)
{
    for (int i = 0; i < m->count; i++) { free(m->obj[i].v); free(m->obj[i].p); }
    free(m->obj);
    memset(m, 0, sizeof *m);
}
