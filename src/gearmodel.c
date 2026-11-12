#include "gearmodel.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static float rdf(const uint8_t *p) { int32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* A part's segment type (+8) -> damage zone: the loader (0x1004c8c4) overwrites every part's +0x10 with
 * 0x1003cd60(+8), and a hit takes the struck part's zone (the object's +0x26, 0x100191d7 / 0x10018a9c). */
static int seg_zone(int seg)
{
    switch (seg) {
    case 1: case 2: case 0x42: return 2;
    case 3: return 1;
    case 5: case 6: case 7: case 8: case 0x5e: return 4;
    case 0xd: case 0xe: case 0xf: case 0x10: return 3;
    case 0x16: case 0x17: case 0x18: case 0x3d: case 0x3e: case 0x4c: case 0x4d: return 6;
    case 0x1b: case 0x1c: case 0x1d: case 0x3f: case 0x40: case 0x4e: case 0x4f: return 5;
    case 0x20: return 7;
    case 0x21: return 0xe;
    case 0x22: return 0x10;
    case 0x23: return 0xd;
    case 0x24: return 0xf;
    case 0x26: return 0xb;
    case 0x27: return 9;
    case 0x29: return 0xc;
    case 0x2a: return 10;
    case 0x2c: return 8;
    }
    return 0;
}

typedef struct { const uint8_t *d; size_t len; int n; gearmodel *g; int depth; int parent; } walker;

static const uint8_t *part(const walker *w, int i)
{
    size_t o = 0x88 + (size_t)i * 0x438;
    return i >= 0 && i < w->n && o + 0x438 <= w->len ? w->d + o : NULL;
}

static void walk(walker *w, int i, float x, float y, float z, int prec, float ox, float oy, float oz)
{
    const uint8_t *p = part(w, i);
    if (!p || w->depth > 32) return;
    if (i < 96) {
        gearmodel *g = w->g;
        g->rec[i][0] = x; g->rec[i][1] = y; g->rec[i][2] = z;
        if (i >= g->nrec) g->nrec = i + 1;
        g->rec_parent[i] = prec;
        g->rec_off[i][0] = ox; g->rec_off[i][1] = oy; g->rec_off[i][2] = oz;
        /* the object's name (+0x90): the drawn model's, upper case (in the running original: GD12RASH, POF_HWRH,
         * EYE_HDHD, DMN1TRHP), from its character 4 */
        int sl = (p[0x7c] == 'g' || p[0x7c] == 'G') ? 1 : 0;
        const uint8_t *mn = p + 0x7c + sl * 0x20;
        g->rec_tag[i][0] = 0;
        if (mn[0] && mn[1] && mn[2] && mn[3] && mn[4]) {
            for (int c = 0; c < 4; c++) g->rec_tag[i][c] = (char)toupper(mn[4 + c]);
            g->rec_tag[i][4] = 0;
        }
    }
    char name[9];
    int self = -1, saved = w->parent;
    /* detail 0's model; a name starting 'g' (the gears') means the next slot's (0x1004dc8f / 0x1004da65) */
    int slot = (p[0x7c] == 'g' || p[0x7c] == 'G') ? 1 : 0;
    memcpy(name, p + 0x7c + slot * 0x20, 8);
    name[8] = 0;
    for (char *c = name; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    if (!strncmp(name, "sol_", 4)) {
        if (-y > w->g->stand) w->g->stand = -y;
        if (w->g->nsol < 4) { w->g->sol_side[w->g->nsol] = name[4]; w->g->sol_rec[w->g->nsol++] = i; }
    } else if (!strncmp(name, "eye_", 4)) {
        w->g->eye[0] = x; w->g->eye[1] = y; w->g->eye[2] = z; w->g->found_eye = 1;
        w->g->eye_rec = i;
        w->g->eye_local[0] = ox; w->g->eye_local[1] = oy; w->g->eye_local[2] = oz;
    } else if (!strncmp(name, "pof_", 4)) {
        if (w->g->npof < GEARMODEL_MAX_POF) {
            gearmodel_point *q = &w->g->pof[w->g->npof++];
            memcpy(q->name, name, 9);
            q->x = x; q->y = y; q->z = z;
            q->rec = i;
        }
    } else if (name[0] && w->g->nparts < GEARMODEL_MAX) {
        gearmodel_part *q = &w->g->part[w->g->nparts++];
        memcpy(q->model, name, 9);
        memcpy(q->model0, p + 0x7c, 8);
        q->model0[8] = 0;
        for (char *c = q->model0; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        q->x = x; q->y = y; q->z = z;
        q->zone = seg_zone(rd32(p + 8));   /* 0x1004c8c4: +0x10 = 0x1003cd60(+8), whatever the file holds */
        q->rec = i;
        q->parent = w->parent;
        self = w->g->nparts - 1;
    }
    if (self >= 0) w->parent = self;
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
        walk(w, c, x + ox, y + oy, z + oz, i, ox, oy, oz);
        const uint8_t *cp = part(w, c);
        c = cp ? rd32(cp + 0x54) : 0;
    }
    w->depth--;
    w->parent = saved;
}

int gearmodel_parse(const uint8_t *d, size_t len, gearmodel *g)
{
    memset(g, 0, sizeof *g);
    if (!d || len < 0x88 + 0x438) return -1;
    walker w = {d, len, rd32(d + 0x84), g, 0, -1};
    g->eye_rec = -1;
    walk(&w, 0, 0, 0, 0, -1, 0, 0, 0);
    gearmodel_pose(g, NULL);
    size_t cb = 0x88 + (size_t)(w.n > 0 ? w.n : 0) * 0x438;
    if (cb + 0x3c <= len && (rd32(d + cb) == 1 || rd32(d + cb) == 2)) {
        g->vclass = g->vclass_raw = rd32(d + cb + 0x28);
        g->faction = rd32(d + cb + 0x2c);
        char bn[0x21];
        memcpy(bn, d + cb + 4, 0x20);
        bn[0x20] = 0;
        g->infantry = strstr(bn, "infa") != NULL;
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
    /* version 1 GDFs (0x1004f86d) give the record's kind (0x21..0x2c) and pick the same names by it: the kind's zone
     * (0x1003cd60) is the version 2 kind */
    if (kind > 16) kind = seg_zone(kind);
    if (kind < 8 || kind > 16) return;
    const char *want = pof_by_kind[kind];
    /* a missing POF is reported ("the POF seg %s is missing") and the slot gets no mount */
    for (int i = 0; i < g->npof; i++) {
        if (strcmp(g->pof[i].name, want)) continue;
        float r = heading * 3.14159265f / 180, s = sinf(r), c = cosf(r);
        float px = g->pof[i].x, py = g->pof[i].y, pz = g->pof[i].z;
        /* the mount object's world place (0x1009518b) on the posed skeleton: gears and infantry hold their hand
         * weapon by their standing animation (gearmodel_stand; the Hunter's rifle muzzle 125.7, 98.3, 344.4 from its
         * root, as in the running original) */
        out[0] = x + px * c + pz * s;
        out[1] = y + py;
        out[2] = z - px * s + pz * c;
        return;
    }
}

int gearmodel_part_gone(const gearmodel *g, int k, const int *zone_gone)
{
    for (int guard = 0; k >= 0 && k < g->nparts && guard < GEARMODEL_MAX; guard++, k = g->part[k].parent) {
        int z = g->part[k].zone;
        if (z >= 1 && z <= 17 && zone_gone[z - 1]) return 1;
    }
    return 0;
}

static void m3mul(const float a[9], const float b[9], float o[9])
{
    float t[9];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) t[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
    memcpy(o, t, sizeof t);
}

/* the first record in the objects' search order (0x1009623a: self, children, siblings - the GDF's preorder) whose name
 * has the tag at 4..7; the records are numbered in that order */
static int find_tag(const gearmodel *g, const char *tag)
{
    if (!tag[0] || !strcmp(tag, "SKIP")) return -1;
    for (int i = 0; i < g->nrec && i < 96; i++) if (g->rec_tag[i][0] && !memcmp(g->rec_tag[i], tag, 4)) return i;
    return -1;
}

/* the skeleton from rec_q: the parts', points' and eye's places and rotations; with set_stand the standing height
 * becomes the lower sole's depth, else the parts are lowered to keep that sole on the ground */
static void pose_q(gearmodel *g, unsigned flags, int set_stand)
{
    static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float local[96][9];
    for (int i = 0; i < 96; i++) anim_quat_matrix(g->rec_q[i], local[i]);
    /* the records in order: a parent always comes before its children */
    float pos[96][3];
    for (int i = 0; i < g->nrec && i < 96; i++) {
        int p = g->rec_parent[i];
        if (p < 0 || p >= i) {
            pos[i][0] = g->rec_off[i][0]; pos[i][1] = g->rec_off[i][1]; pos[i][2] = g->rec_off[i][2];
            memcpy(g->rec_m[i], local[i], sizeof id);
            continue;
        }
        const float *m = g->rec_m[p], *o = g->rec_off[i];
        for (int k = 0; k < 3; k++) pos[i][k] = pos[p][k] + m[k * 3] * o[0] + m[k * 3 + 1] * o[1] + m[k * 3 + 2] * o[2];
        m3mul(m, local[i], g->rec_m[i]);
    }
    /* stood on the lower sole (0x1001386b's end: gears and infantry; a sole of a leg gone isn't counted) */
    float low = 0;
    int have = 0;
    for (int s = 0; s < g->nsol; s++) {
        int r = g->sol_rec[s];
        if ((g->sol_side[s] == 'l' && (flags & ANIM_LSOLE)) || (g->sol_side[s] == 'r' && (flags & ANIM_RSOLE))) continue;
        if (r >= 0 && r < 96 && (!have || pos[r][1] < low)) { low = pos[r][1]; have = 1; }
    }
    float dy = 0;
    if (set_stand) { if (have) g->stand = -low; }
    else if (have) dy = -(g->stand + low);
    g->root_dy = dy;
    for (int i = 0; i < g->nrec && i < 96; i++) { g->rec[i][0] = pos[i][0]; g->rec[i][1] = pos[i][1] + dy; g->rec[i][2] = pos[i][2]; }
    for (int k = 0; k < g->nparts; k++) {
        int r = g->part[k].rec;
        if (r < 0 || r >= 96) continue;
        g->part[k].x = pos[r][0]; g->part[k].y = pos[r][1] + dy; g->part[k].z = pos[r][2];
        memcpy(g->part[k].m, g->rec_m[r], sizeof id);
    }
    for (int k = 0; k < g->npof; k++) {
        int r = g->pof[k].rec;
        if (r < 0 || r >= 96) continue;
        g->pof[k].x = pos[r][0]; g->pof[k].y = pos[r][1] + dy; g->pof[k].z = pos[r][2];
    }
    if (g->found_eye && g->eye_rec >= 0 && g->eye_rec < 96) {
        int r = g->eye_rec;
        g->eye[0] = pos[r][0]; g->eye[1] = pos[r][1] + dy; g->eye[2] = pos[r][2];
    }
}

void gearmodel_pose(gearmodel *g, const anim3di *an)
{
    for (int i = 0; i < 96; i++) { g->rec_q[i][0] = g->rec_q[i][1] = g->rec_q[i][2] = 0; g->rec_q[i][3] = 1; }
    if (an)
        for (int t = 0; t < an->ntracks; t++) {
            if (!an->track[t].nrot) continue;   /* 0x1001386b binds a track with keys (or any at frame 0) */
            int r = find_tag(g, an->track[t].tag);
            if (r >= 0) memcpy(g->rec_q[r], an->track[t].q0, sizeof g->rec_q[r]);
        }
    pose_q(g, 0, 1);
}

void gearmodel_animate(gearmodel *g, const anim_player *p, unsigned flags, int head)
{
    if (!p->an) return;
    for (int t = 0; t < p->an->ntracks; t++) {
        float q[4];
        if (anim_track_q(p, t, q)) continue;
        if (!head && !memcmp(p->an->track[t].tag, "HDHD", 4)) continue;
        int r = find_tag(g, p->an->track[t].tag);
        if (r >= 0) memcpy(g->rec_q[r], q, sizeof q);
    }
    pose_q(g, flags, 0);
}

int gearmodel_stand(gearmodel *g, hg_archive *a, const int *types, const int *kinds, int nweapons, int version)
{
    if (g->vclass != 1 && g->vclass != 9) return -1;
    g->anim_set = anim_weapon_set(types, kinds, nweapons, version, g->infantry);
    int id = g->anim_set | (g->vclass == 9 ? 0x1000 : 0);
    anim3di an;
    if (anim_load(a, anim_stand_name(id), &an)) return -1;
    gearmodel_pose(g, &an);
    anim_free(&an);
    return 0;
}
