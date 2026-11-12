#include "structures.h"

#include <math.h>
#include <string.h>

void structures_init(structures *st, const world *w)
{
    memset(st, 0, sizeof *st);
    const mission *m = world_mission(w);
    for (int i = 0; i < m->ntgt && st->n < 512; i++, st->n++) {
        const mission_target *g = &m->tgt[i];
        int hp = g->hp < 0 ? -g->hp : g->hp;
        st->s[i].hp = hp == 32000 ? 0 : hp;
        st->s[i].boom = g->hp >= 0 ? 0x32 : (hp & 1) ? 0x24 : 0x16;   /* 0x100c1152 flags 1 / 2, 0x1001cc6c */
        st->s[i].inst = world_obj_instance(w, g->obj);
        st->s[i].wreck = world_obj_instance(w, g->alt);
        st->s[i].obj = g->obj;
        st->s[i].alt = g->alt;
    }
}

static int seg_box(const float a[3], const float b[3], const float mn[3], const float mx[3], float *t)
{
    float t0 = 0, t1 = 1;
    for (int k = 0; k < 3; k++) {
        float d = b[k] - a[k];
        if (fabsf(d) < 1e-6f) {
            if (a[k] < mn[k] || a[k] > mx[k]) return 0;
            continue;
        }
        float u0 = (mn[k] - a[k]) / d, u1 = (mx[k] - a[k]) / d;
        if (u0 > u1) { float q = u0; u0 = u1; u1 = q; }
        if (u0 > t0) t0 = u0;
        if (u1 < t1) t1 = u1;
        if (t0 > t1) return 0;
    }
    *t = t0;
    return 1;
}

int structures_hit(const structures *st, const world *w, const float p0[3], const float p1[3], float *t)
{
    int best = -1;
    float bt = 2;
    for (int i = 0; i < st->n; i++) {
        if (st->s[i].dead || st->s[i].inst < 0) continue;
        float x, y, z, s, c, mn[3], mx[3];
        if (world_object_pose(w, st->s[i].inst, &x, &y, &z, &s, &c) != 0) continue;
        if (world_object_bounds(w, st->s[i].inst, mn, mx)) continue;
        /* reject far ones: the models are a few thousand units across at most */
        float r = 0;
        for (int k = 0; k < 3; k++) r = fmaxf(r, fmaxf(fabsf(mn[k]), fabsf(mx[k])));
        float lo[3] = {x - r, y - r, z - r}, hi[3] = {x + r, y + r, z + r}, tt;
        if (!seg_box(p0, p1, lo, hi, &tt)) continue;
        float a[3], b[3];
        const float *q[2] = {p0, p1};
        float *o[2] = {a, b};
        for (int e = 0; e < 2; e++) {
            float dx = q[e][0] - x, dz = q[e][2] - z;
            o[e][0] = dx * c - dz * s;
            o[e][1] = q[e][1] - y;
            o[e][2] = dx * s + dz * c;
        }
        if (seg_box(a, b, mn, mx, &tt) && tt < bt) { bt = tt; best = i; }
    }
    if (best >= 0) *t = bt;
    return best;
}

int structures_damage(structures *st, world *w, int i, int power)
{
    if (i < 0 || i >= st->n || st->s[i].dead || st->s[i].hp == 0) return 0;
    st->s[i].hp -= power;
    if (st->s[i].hp >= 1) return 0;
    st->s[i].hp = 0;
    st->s[i].dead = 1;
    world_show_obj(w, st->s[i].obj, 0);
    if (st->s[i].alt >= 0) world_show_obj(w, st->s[i].alt, 1);
    return 1;
}
