#include "gearmodel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static float rdf(const uint8_t *p) { int32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* A part's segment type (+8) -> damage zone, for the GDFs that leave +0x10 at 0 (infantry, vehicles). INFERRED: the
 * pairing the gears' parts carry in both fields (hip and torso 1-2 -> 2, head 3 -> 1, left arm 6-12 -> 4, right arm
 * 14-20 -> 3, left leg 23-26 -> 6, right leg 28-31 -> 5, backpack 32 -> 7, hand weapon 42 -> 10); a vehicle's body
 * (66) -> 2, its chassis zone. The original finds the zone from the polygon hit (0x100229d9), not traced. */
static int seg_zone(int seg)
{
    if (seg == 1 || seg == 2) return 2;
    if (seg == 3) return 1;
    if (seg >= 6 && seg <= 12) return 4;
    if (seg >= 14 && seg <= 20) return 3;
    if (seg >= 23 && seg <= 26) return 6;
    if (seg >= 28 && seg <= 31) return 5;
    if (seg == 32) return 7;
    if (seg == 42) return 10;
    if (seg == 66) return 2;
    return 0;
}

typedef struct { const uint8_t *d; size_t len; int n; gearmodel *g; int depth; } walker;

static const uint8_t *part(const walker *w, int i)
{
    size_t o = 0x88 + (size_t)i * 0x438;
    return i >= 0 && i < w->n && o + 0x438 <= w->len ? w->d + o : NULL;
}

static void walk(walker *w, int i, float x, float y, float z)
{
    const uint8_t *p = part(w, i);
    if (!p || w->depth > 32) return;
    if (i < 96) { w->g->rec[i][0] = x; w->g->rec[i][1] = y; w->g->rec[i][2] = z; if (i >= w->g->nrec) w->g->nrec = i + 1; }
    char name[9];
    /* detail 0's model; a name starting 'g' (the gears') means the next slot's (0x1004dc8f / 0x1004da65) */
    int slot = (p[0x7c] == 'g' || p[0x7c] == 'G') ? 1 : 0;
    memcpy(name, p + 0x7c + slot * 0x20, 8);
    name[8] = 0;
    for (char *c = name; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    if (!strncmp(name, "sol_", 4)) { if (-y > w->g->stand) w->g->stand = -y; }
    else if (!strncmp(name, "eye_", 4)) { w->g->eye[0] = x; w->g->eye[1] = y; w->g->eye[2] = z; w->g->found_eye = 1; }
    else if (!strncmp(name, "pof_", 4)) {
        if (w->g->npof < GEARMODEL_MAX_POF) {
            gearmodel_point *q = &w->g->pof[w->g->npof++];
            memcpy(q->name, name, 9);
            q->x = x; q->y = y; q->z = z;
        }
    } else if (name[0] && w->g->nparts < GEARMODEL_MAX) {
        gearmodel_part *q = &w->g->part[w->g->nparts++];
        memcpy(q->model, name, 9);
        memcpy(q->model0, p + 0x7c, 8);
        q->model0[8] = 0;
        for (char *c = q->model0; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        q->x = x; q->y = y; q->z = z;
        q->zone = rd32(p + 0x10);
        q->rec = i;
        if (q->zone <= 0) q->zone = seg_zone(rd32(p + 8));
    }
    /* the children in order, the k-th placed by the k-th used link */
    int links[9], nl = 0;
    for (int k = 0; k < 9; k++) {
        size_t b = 0x168 + (size_t)k * 0x48;
        if (b + 0x38 <= 0x438 && rd32(p + b) == 1) links[nl++] = (int)b;
    }
    int c = rd32(p + 0x78), j = 0;
    w->depth++;
    for (int guard = 0; c > 0 && guard < GEARMODEL_MAX; guard++, j++) {
        float ox = 0, oy = 0, oz = 0;
        const uint8_t *cp0 = part(w, c);
        if (j < nl) { ox = rdf(p + links[j] + 0x24); oy = rdf(p + links[j] + 0x28); oz = rdf(p + links[j] + 0x2c); }
        /* less the child's own pivot (+0x15c) */
        if (cp0) { ox -= rdf(cp0 + 0x15c); oy -= rdf(cp0 + 0x160); oz -= rdf(cp0 + 0x164); }
        walk(w, c, x + ox, y + oy, z + oz);
        const uint8_t *cp = part(w, c);
        c = cp ? rd32(cp + 0x54) : 0;
    }
    w->depth--;
}

int gearmodel_parse(const uint8_t *d, size_t len, gearmodel *g)
{
    memset(g, 0, sizeof *g);
    if (!d || len < 0x88 + 0x438) return -1;
    walker w = {d, len, rd32(d + 0x84), g, 0};
    walk(&w, 0, 0, 0, 0);
    size_t cb = 0x88 + (size_t)(w.n > 0 ? w.n : 0) * 0x438;
    if (cb + 0x3c <= len && (rd32(d + cb) == 1 || rd32(d + cb) == 2)) {
        g->vclass = rd32(d + cb + 0x28);
        if (g->vclass == 2) g->vclass = 4;
    }
    return g->nparts ? 0 : -1;
}

int gearmodel_load(hg_archive *a, const char *gdf, gearmodel *g)
{
    size_t len;
    uint8_t *d = hga_load(a, gdf, "GDF", &len, NULL);
    int r = gearmodel_parse(d, d ? len : 0, g);
    free(d);
    return r;
}

void gearmodel_mount(const gearmodel *g, float x, float y, float z, float heading, int kind, float out[3])
{
    static const char *const pof_by_kind[17] = {
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        "pof_ewen", "pof_awra", "pof_hwrh", "pof_awla", "pof_hwlh", "pof_twru", "pof_twlu", "pof_twrl", "pof_twll",
    };
    if (kind < 8) return;
    /* version 1 vehicles name their mounts another way (0x1004f86d builds the name, not traced): INFERRED, their
     * first POF point */
    const char *want = kind <= 16 ? pof_by_kind[kind] : NULL;
    for (int i = 0; i < g->npof; i++) {
        if (want && strcmp(g->pof[i].name, want)) continue;
        float r = heading * 3.14159265f / 180, s = sinf(r), c = cosf(r);
        float px = g->pof[i].x, py = g->pof[i].y, pz = g->pof[i].z;
        /* at rest a hand weapon hangs muzzle down (the Hunter's rifle mount is below its soles); the original aims
         * the arm first (not modelled). INFERRED: such mounts fire from beside the torso, 100 up and 120 ahead */
        if (py < -0.5f * g->stand) { py = 100.0f; pz = 120.0f; }
        out[0] = x + px * c + pz * s;
        out[1] = y + py;
        out[2] = z - px * s + pz * c;
        return;
    }
}
