#include "modelview.h"

#include "gearmodel.h"
#include "material.h"
#include "raster.h"
#include "shell_picture.h"
#include "shell_tables.h"
#include "tex.h"
#include "wtb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char name[40]; int ok; uint32_t *px; uint8_t *xel; raster_texture rt, xrt; } mv_tex;

typedef struct {
    int      n;
    uint16_t idx[WTB_MAX_POLY];
    int      tex;
    uint32_t color;
} mv_poly;

typedef struct {
    char        name[11];
    int         ok;
    int         nverts, npolys;
    wtb_vertex *v;
    mv_poly    *p;
} mv_mesh;

struct modelview {
    hg_archive  *a;
    material_set mat;
    int          mat_loaded;
    mv_tex      *tex;   int ntex;
    mv_mesh     *mesh;  int nmesh;
    gearmodel    gm;
    int          has;
    int          part_mesh[GEARMODEL_MAX];
    char         gear[32];
    float        mn[3], mx[3];
    uint8_t     *luma;      /* FOG.TBL (32 x 256), NULL if absent */
    int          luma_tried;
    raster_target rt;
    /* the last drawing's view (for modelview_screen / modelview_pick) */
    int          drawn, vx0, vy0, vw, vh;
    float        place[3], turn;
};

modelview *modelview_open(hg_archive *a)
{
    modelview *v = calloc(1, sizeof *v);
    if (v) v->a = a;
    return v;
}

void modelview_close(modelview *v)
{
    if (!v) return;
    for (int i = 0; i < v->ntex; i++) { free(v->tex[i].px); free(v->tex[i].xel); }
    free(v->luma);
    free(v->tex);
    for (int i = 0; i < v->nmesh; i++) { free(v->mesh[i].v); free(v->mesh[i].p); }
    free(v->mesh);
    if (v->mat_loaded) material_free(&v->mat);
    raster_free(&v->rt);
    free(v);
}

/* 0x1012b93c: the shell's tables, loaded once */
static void load_materials(modelview *v)
{
    if (v->mat_loaded) return;
    v->mat_loaded = 1;
    for (int i = 0; i < SHELL_TLTS; i++) material_load(&v->mat, v->a, shell_tlt[i], "TLT");
    for (int i = 0; i < SHELL_MLTS; i++) material_load(&v->mat, v->a, shell_mlt[i], "MLT");
}

static int load_tex(modelview *v, const char *name)
{
    for (int i = 0; i < v->ntex; i++) if (!strcmp(v->tex[i].name, name)) return v->tex[i].ok ? i : -1;
    mv_tex *nt = realloc(v->tex, (size_t)(v->ntex + 1) * sizeof *nt);
    if (!nt) return -1;
    v->tex = nt;
    mv_tex *e = &v->tex[v->ntex++];
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", name);
    size_t len;
    uint8_t *d = hga_load(v->a, name, "TEX", &len, NULL);
    hg_texture t;
    if (d && !tex_parse_tex(d, len, &t)) {
        e->px = malloc((size_t)t.w * t.h * 4);
        if (e->px) {
            for (int i = 0; i < t.w * t.h; i++) {
                const uint8_t *p = t.pal[t.idx[i]];
                uint32_t al = t.type == 0 ? 255u : 255u - p[3];
                e->px[i] = (al << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
            }
            e->rt.w = t.w; e->rt.h = t.h; e->rt.texels = e->px;
            e->ok = 1;
        }
        tex_free(&t);
    }
    free(d);
    /* the software renderer's texels: the XEL of the same name, at its own size (GATOETOP's is 64 x 64, its TEX 32 x 32) */
    size_t xl;
    uint8_t *xd = hga_load(v->a, name, "XEL", &xl, NULL);
    hg_texture x;
    if (xd && !tex_parse_xel(xd, xl, &x)) {
        e->xel = x.idx;
        e->xrt.w = x.w; e->xrt.h = x.h; e->xrt.idx = e->xel;
        e->ok = 1;
    }
    free(xd);
    return e->ok ? v->ntex - 1 : -1;
}

static int load_mesh(modelview *v, const char *name)
{
    for (int i = 0; i < v->nmesh; i++) if (!strcmp(v->mesh[i].name, name)) return v->mesh[i].ok ? i : -1;
    mv_mesh *nm = realloc(v->mesh, (size_t)(v->nmesh + 1) * sizeof *nm);
    if (!nm) return -1;
    v->mesh = nm;
    mv_mesh *ms = &v->mesh[v->nmesh++];
    memset(ms, 0, sizeof *ms);
    snprintf(ms->name, sizeof ms->name, "%s", name);
    size_t len;
    uint8_t *d = hga_load(v->a, name, "WTB", &len, NULL);
    wtb_model m;
    if (!d || wtb_parse(d, len, &m)) { free(d); return -1; }
    free(d);
    int nv = 0, np = 0;
    for (int i = 0; i < m.count; i++) { nv += m.obj[i].nverts; np += m.obj[i].npolys; }
    ms->v = malloc((size_t)(nv ? nv : 1) * sizeof *ms->v);
    ms->p = malloc((size_t)(np ? np : 1) * sizeof *ms->p);
    if (!ms->v || !ms->p) { wtb_free(&m); return -1; }
    int vb = 0;
    for (int i = 0; i < m.count; i++) {
        const wtb_object *o = &m.obj[i];
        for (int j = 0; j < o->nverts; j++) ms->v[ms->nverts++] = o->v[j];
        for (int j = 0; j < o->npolys; j++) {
            const wtb_poly *g = &o->p[j];
            mv_poly *q = &ms->p[ms->npolys++];
            q->n = g->n;
            for (int k = 0; k < g->n; k++) q->idx[k] = (uint16_t)(g->idx[k] + vb);
            q->tex = -1;
            q->color = 0xff808080u;
            const tlt_entry *te = o->legacy ? NULL : material_find_label(&v->mat, g->label);
            const mlt_entry *me = te ? material_find_texture(&v->mat, te->texture) : NULL;
            if (me) {
                q->color = 0xff000000u | ((uint32_t)me->rgb[0] << 16) | ((uint32_t)me->rgb[1] << 8) | me->rgb[2];
                if (me->type != 0 && me->type != 1 && me->type != 4) {
                    char t[40];
                    if (me->type == 3) snprintf(t, sizeof t, "%s%s%u", me->name, me->p4c < 10 ? "0" : "", me->p4c);
                    else snprintf(t, sizeof t, "%s", me->name);
                    q->tex = load_tex(v, t);
                }
            }
        }
        vb += o->nverts;
    }
    wtb_free(&m);
    ms->ok = 1;
    return v->nmesh - 1;
}

static int setup(modelview *v)
{
    load_materials(v);
    for (int k = 0; k < 3; k++) { v->mn[k] = 1e30f; v->mx[k] = -1e30f; }
    for (int i = 0; i < v->gm.nparts; i++) {
        int m = v->part_mesh[i] = load_mesh(v, v->gm.part[i].model0);
        if (m < 0) continue;
        const mv_mesh *ms = &v->mesh[m];
        const float o[3] = {v->gm.part[i].x, v->gm.part[i].y, v->gm.part[i].z};
        for (int j = 0; j < ms->nverts; j++) {
            const float p[3] = {ms->v[j].x + o[0], ms->v[j].y + o[1], ms->v[j].z + o[2]};
            for (int k = 0; k < 3; k++) { if (p[k] < v->mn[k]) v->mn[k] = p[k]; if (p[k] > v->mx[k]) v->mx[k] = p[k]; }
        }
    }
    v->has = v->mn[0] <= v->mx[0];
    return v->has ? 0 : -1;
}

int modelview_set_gear(modelview *v, const char *gdf)
{
    if (!gdf || !gdf[0]) { v->has = 0; v->gear[0] = 0; return -1; }
    if (v->has && !strcmp(v->gear, gdf)) return 0;
    snprintf(v->gear, sizeof v->gear, "%s", gdf);
    v->has = 0;
    if (gearmodel_load(v->a, gdf, &v->gm)) return -1;
    return setup(v);
}

int modelview_set_data(modelview *v, const uint8_t *d, size_t len, const char (*extra)[9])
{
    v->gear[0] = 0;
    v->has = 0;
    if (gearmodel_parse(d, len, &v->gm)) return -1;
    for (int r = 0; extra && r < v->gm.nrec && r < 96; r++) {
        if (!extra[r][0] || v->gm.nparts >= GEARMODEL_MAX) continue;
        gearmodel_part *q = &v->gm.part[v->gm.nparts++];
        memset(q, 0, sizeof *q);
        snprintf(q->model, sizeof q->model, "%s", extra[r]);
        snprintf(q->model0, sizeof q->model0, "%s", extra[r]);
        q->x = v->gm.rec[r][0]; q->y = v->gm.rec[r][1]; q->z = v->gm.rec[r][2];
        q->rec = r;
    }
    return setup(v);
}

/* The camera (traced): the shell's camera (0x10204500) sits at the origin looking along +z, unturned; its zoom
 * (+0x18) is 1.0 and its aspect (+0x44) 480 x 4/3 / 640 = 1.0 (0x10178ff3). Projection (0x10056736, set up per
 * drawing by 0x100c4a40 from the view's rectangle): centre ((x0 + 1 + x1) >> 1, (y0 + 1 + y1) >> 1), focal length
 * ((x1 - x0 + 1) >> 1) x zoom on both axes (x aspect), y up; near = half width x 0.5 + 1.
 * The object (the design's root, 0x1011b5fd): turned 180 degrees about y (it faces the camera), then by the arrow
 * keys; placed at the root item's view place (place[]). Its quaternion becomes the matrix of 0x1003d83f, applied
 * as M v + t (0x1003c8d0): for a turn a about y, x' = x cos a + z sin a, z' = z cos a - x sin a. */
typedef struct { float ca, sa, px, py, pz, cx, cy, f, nearz; } mv_xf;

static void xf_setup(const modelview *v, mv_xf *t)
{
    float a = v->turn * 3.14159265f / 180;
    t->ca = cosf(a); t->sa = sinf(a);
    t->px = v->place[0]; t->py = v->place[1]; t->pz = v->place[2];
    int x0 = v->vx0, y0 = v->vy0, x1 = v->vx0 + v->vw - 1, y1 = v->vy0 + v->vh - 1;
    int hw = (x1 - x0 + 1) >> 1;
    if (hw < 2) hw = 1;
    t->cx = (float)(((x0 + 1 + x1) >> 1) - x0);   /* in the view's own pixels */
    t->cy = (float)(((y0 + 1 + y1) >> 1) - y0);
    t->f = (float)hw * 1.0f;
    t->nearz = (float)hw * 0.5f + 1.0f;
}

/* a point of the model (from its root) to the camera */
static void xf_point(const mv_xf *t, float x, float y, float z, float out[3])
{
    out[0] = x * t->ca + z * t->sa + t->px;
    out[1] = y + t->py;
    out[2] = z * t->ca - x * t->sa + t->pz;
}

/* SOFTWARE look's luma for a vertex (the shell's lighting, 0x1003ba56 with the camera's light from the shell's set-up,
 * 0x1012b1e8: a point light (+0x28 = 0) at (0, 10000, -3000), ambient (+0x2a) 45, intensity 128 (0x1020438c)).
 * Per object (each part is one, 0x1003b3ca): the light's offset from the object's place, light - t, NOT turned into
 * the object's frame (0x1003ba56 with +0x28 = 0; only a directional light, +0x28 = 1, is turned), against the
 * object's own vertices and normals - so the light turns with the model. i = clamp(dot(normal, rel - vertex) /
 * |rel - vertex| x 128, -127, 127); L = 45 + i x 128 / 128 when that is > 0, clamped to 128; row = L x 255 >> 10 (a
 * textured polygon's draw word, 0x10039900), clamped to 4..31. The shell hands the renderer row << 3 (0x10035fff's
 * caller, software: no | 7) and the table upside down (0x10035fff), so the TBL row is 31 - (1 - (row << 3) / 255) x
 * 31, as world.c. Returns that index; *light the L for the true-colour look. */
static float vertex_luma(const float rel[3], const wtb_vertex *v, float *light)
{
    float lx = rel[0] - v->x, ly = rel[1] - v->y, lz = rel[2] - v->z;
    /* the length (0x1003e364): the largest of |x|, |y|, |z| plus a quarter of the other two */
    float ax = fabsf(lx), ay = fabsf(ly), az = fabsf(lz), big = ax, o1 = az, o2 = ay;
    if (ax < az) { big = az; o1 = ax; }
    if (big < ay) { float b0 = big; big = ay; o2 = b0; }
    float len = (big * 4.0f + o1 + o2) / 4.0f;
    int L = 45;
    if (ax < 1e-7f && ay < 1e-7f && az < 1e-7f) L += 0x7f;   /* at the light: 0x7f (0x100480ac) */
    else if (len > 0) {
        int i = (int)((v->nx * lx + v->ny * ly + v->nz * lz) / len * 128.0f);
        if (i > 127) i = 127;
        if (i < -127) i = -127;
        int li = (int)((float)(i * 128) / 128.0f);
        if (li > 0) L += li;
    }
    if (L > 0x80) L = 0x80;
    int row = L * 255 >> 10;
    if (row < 4) row = 4;
    if (row > 31) row = 31;
    *light = (float)L / 128.0f;
    float f = (float)(row << 3) / 255.0f;
    return (1.0f - f) * 31.0f;
}

static void load_luma(modelview *v)
{
    if (v->luma_tried) return;
    v->luma_tried = 1;
    size_t len;
    uint8_t *d = hga_load(v->a, "FOG", "TBL", &len, NULL);   /* the shell's luma table (0x1012b1e8 -> 0x10035fff) */
    if (d && len >= 32 * 256) v->luma = d; else free(d);
}

void modelview_draw(modelview *v, uint32_t *out, const uint8_t (*pal)[3], int x0, int y0, int x1, int y1,
                    const float place[3], float turn)
{
    if (!v->has || x1 <= x0 || y1 <= y0) return;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (v->rt.w != w || v->rt.h != h) { raster_free(&v->rt); if (raster_init(&v->rt, w, h)) return; }
    raster_clear(&v->rt, 0);
    load_luma(v);
    int sw = pal && v->luma;
    v->rt.pal = sw ? pal : NULL;
    v->rt.luma = sw ? v->luma : NULL;
    v->rt.bilinear = !sw;
    v->drawn = 1; v->vx0 = x0; v->vy0 = y0; v->vw = w; v->vh = h; v->turn = turn;
    for (int k = 0; k < 3; k++) v->place[k] = place[k];
    mv_xf t;
    xf_setup(v, &t);
    for (int i = 0; i < v->gm.nparts; i++) {
        int mi = v->part_mesh[i];
        if (mi < 0) continue;
        const mv_mesh *ms = &v->mesh[mi];
        const gearmodel_part *pt = &v->gm.part[i];
        /* the part's object: its place, and the light's offset from it (vertex_luma) */
        float tp[3], rel[3];
        xf_point(&t, pt->x, pt->y, pt->z, tp);
        rel[0] = 0.0f - tp[0]; rel[1] = 10000.0f - tp[1]; rel[2] = -3000.0f - tp[2];
        for (int j = 0; j < ms->npolys; j++) {
            const mv_poly *q = &ms->p[j];
            raster_vertex rv[WTB_MAX_POLY];
            int ok = 1;
            for (int k = 0; k < q->n && ok; k++) {
                const wtb_vertex *mv = &ms->v[q->idx[k]];
                float c[3];
                xf_point(&t, mv->x + pt->x, mv->y + pt->y, mv->z + pt->z, c);
                if (c[2] < t.nearz) { ok = 0; break; }
                float lit, iz = 1.0f / c[2];
                rv[k].lum = vertex_luma(rel, mv, &lit);
                rv[k].x = t.cx + c[0] * t.f * iz;
                rv[k].y = t.cy - c[1] * t.f * iz;
                rv[k].iw = iz; rv[k].u = mv->u; rv[k].v = mv->v;
                rv[k].r = rv[k].g = rv[k].b = lit;
                rv[k].fog = 1.0f;
            }
            if (!ok) continue;
            const raster_texture *tx = NULL;
            if (q->tex >= 0) {
                const mv_tex *e = &v->tex[q->tex];
                tx = sw && e->xel ? &e->xrt : e->px ? &e->rt : NULL;
            }
            for (int k = 1; k + 1 < q->n; k++) raster_triangle(&v->rt, &rv[0], &rv[k], &rv[k + 1], tx, q->color);
        }
    }
    for (int j = 0; j < h; j++) {
        int yy = y0 + j;
        if (yy < 0 || yy >= 480) continue;
        for (int i = 0; i < w; i++) {
            int xx = x0 + i;
            if (xx < 0 || xx >= 640 || v->rt.depth[(size_t)j * w + i] <= 0) continue;
            out[(size_t)yy * 640 + xx] = v->rt.color[(size_t)j * w + i] | 0xff000000u;
        }
    }
}

int modelview_screen(const modelview *v, int rec, float *sx, float *sy)
{
    if (!v->has || !v->drawn || rec < 0 || rec >= v->gm.nrec) return -1;
    mv_xf t;
    xf_setup(v, &t);
    float c[3];
    xf_point(&t, v->gm.rec[rec][0], v->gm.rec[rec][1], v->gm.rec[rec][2], c);
    if (c[2] < t.nearz) return -1;
    *sx = (float)v->vx0 + t.cx + c[0] * t.f / c[2];
    *sy = (float)v->vy0 + t.cy - c[1] * t.f / c[2];
    return 0;
}

/* the part under (x, y) in the last drawing: its record, -1 none (the original tests each instance's polygons and
 * takes the nearest, 0x100c0efb) */
int modelview_pick(const modelview *v, int x, int y)
{
    if (!v->has || !v->drawn) return -1;
    mv_xf t;
    xf_setup(v, &t);
    float px = (float)x - (float)v->vx0, py = (float)y - (float)v->vy0, best = 0;
    int hit = -1;
    for (int i = 0; i < v->gm.nparts; i++) {
        int mi = v->part_mesh[i];
        if (mi < 0) continue;
        const mv_mesh *ms = &v->mesh[mi];
        const gearmodel_part *pt = &v->gm.part[i];
        for (int j = 0; j < ms->npolys; j++) {
            const mv_poly *q = &ms->p[j];
            float sx[WTB_MAX_POLY], syy[WTB_MAX_POLY], iw[WTB_MAX_POLY];
            int ok = 1;
            for (int k = 0; k < q->n && ok; k++) {
                const wtb_vertex *mv = &ms->v[q->idx[k]];
                float c[3];
                xf_point(&t, mv->x + pt->x, mv->y + pt->y, mv->z + pt->z, c);
                if (c[2] < t.nearz) { ok = 0; break; }
                iw[k] = 1.0f / c[2];
                sx[k] = t.cx + c[0] * t.f * iw[k];
                syy[k] = t.cy - c[1] * t.f * iw[k];
            }
            if (!ok) continue;
            for (int k = 1; k + 1 < q->n; k++) {
                float x0 = sx[0], y0 = syy[0], x1 = sx[k], y1 = syy[k], x2 = sx[k + 1], y2 = syy[k + 1];
                float d = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
                if (d == 0) continue;
                float u = ((px - x0) * (y2 - y0) - (x2 - x0) * (py - y0)) / d;
                float w = ((x1 - x0) * (py - y0) - (px - x0) * (y1 - y0)) / d;
                if (u < 0 || w < 0 || u + w > 1) continue;
                float z = iw[0] + u * (iw[k] - iw[0]) + w * (iw[k + 1] - iw[0]);
                if (z > best) { best = z; hit = pt->rec; }
            }
        }
    }
    return hit;
}
