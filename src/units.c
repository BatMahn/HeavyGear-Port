#include "units.h"
#include "hgrand.h"
#include "weapon_heat.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "bytes.h"
#include "gear_tables.h"
#include "ai_rules.h"
#include "objectives.h"

#define PI_F 3.14159265f

static int same_ci(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return !*a && !*b;
}

static void cover_load(units *us, const world *w);
static int  cover_pick(units *us, const unit *u, const unit *t, float range);
static int  cover_near(const units *us, const unit *u, float range);
static int  cover_fold(units *us, const unit *u, const unit *t, float range, int kind);
static int  cover_walk(units *us, world *w, int i, int k, int t, float *turn, float *thr);
static int  ai_probe(units *us, world *w, int i, float want, float *turn, float *thr);
static int  ai_threat(units *us, world *w, int i);
static int  ai_incoming(const units *us, const combat_shots *s, int i);

static float ground_at(void *ctx, float x, float z)
{
    int f;
    float h = world_ground_height((const world *)ctx, x, z, &f);
    return f ? h : 0.0f;
}

/* A version 1 vehicle's top speed (0x1004f86d's sums over its GDF records, then 0x10050db8): speed points S and their
 * cap M from the records: kind 1 (the chassis) by its id 0..5: M + 3 / 12 / 9 / 10 / 8 / 8, S - 0 / 10 / 15 / 14 /
 * 25 / 30 (others M + 3); kinds 0x1b..0x1f by id 0 / 1 / 2: S + 1 / 2 / 3, M + 2 / 3 / 6, and per option (the 20 ints
 * from +0x14) 2, 7, 0xc, 0x11: S + 1, M + 1; 4, 9, 0xe, 0x13: S + 1, M + 2; 5, 0xa, 0xf, 0x14: S + 2, M + 1; 0x1a, 0x1b:
 * S - 1, M - 1; kind 0x20 (the engine): S + (int)sqrt(+0x64, or +0xc when that is 0). Points = S held to 1 .. M, plus
 * (int)(points x 0.2 / 0.15 / 0.1) by the header's +0x38 bits 0x10 / 8 / 4; x 47.1744 (+0x4d4). 0 if not version 1. */
static float v1_top_speed(hg_archive *a, const char *gdf)
{
    size_t len;
    uint8_t *d = hga_load(a, gdf, "GDF", &len, NULL);
    if (!d) return 0;
    float top = 0;
    if (len >= 0x88 && rd_u32(d) == 0) {
        size_t o = 0x88 + (size_t)rd_u32(d + 0x84) * 0x438;
        if (o + 0x3c <= len && rd_u32(d + o) == 1) {
            int n = rd_i32(d + o + 0x24), S = 0, M = 0;
            unsigned hf = rd_u32(d + o + 0x38);
            for (int k = 0; k < n && o + 0x3c + (size_t)(k + 1) * 0x6c <= len; k++) {
                const uint8_t *r = d + o + 0x3c + (size_t)k * 0x6c;
                int kind = rd_i32(r), id = rd_i32(r + 4);
                if (kind == 1) {
                    static const int dm[6] = {3, 12, 9, 10, 8, 8}, ds[6] = {0, -10, -15, -14, -25, -30};
                    if (id >= 0 && id < 6) { M += dm[id]; S += ds[id]; } else M += 3;
                } else if (kind >= 0x1b && kind <= 0x1f) {
                    if (id == 0) { S += 1; M += 2; } else if (id == 1) { S += 2; M += 3; } else if (id == 2) { S += 3; M += 6; }
                    for (int q = 0; q < 0x14; q++) {
                        switch (rd_i32(r + 0x14 + q * 4)) {
                        case 2: case 7: case 0xc: case 0x11: S += 1; M += 1; break;
                        case 4: case 9: case 0xe: case 0x13: S += 1; M += 2; break;
                        case 5: case 0xa: case 0xf: case 0x14: S += 2; M += 1; break;
                        case 0x1a: case 0x1b: S -= 1; M -= 1; break;
                        }
                    }
                } else if (kind == 0x20) {
                    int q = rd_i32(r + 0x64);
                    S += (int)sqrt((double)(q ? q : rd_i32(r + 0xc)));
                }
            }
            int pts = S < 1 ? 1 : S > M ? M : S;
            if (hf & 0x10) pts += (int)(pts * 0.2);
            else if (hf & 8) pts += (int)(pts * 0.15);
            else if (hf & 4) pts += (int)(pts * 0.1);
            top = (float)pts * 47.1744f;
        }
    }
    free(d);
    return top;
}

/* the difficulty's internal multiplier (0x1004e4cc: hgdif.cfg +5 -> x 12 / 6 / 1), for the player's group */
static float internal_scale = 6.0f;
void units_set_internal_scale(float k) { internal_scale = k; }

void units_spawn(units *us, world *w, hg_archive *a)
{
    memset(us, 0, sizeof *us);
    hg_rand_reset();   /* InitRandom (0x10089da0) runs as the sim starts */
    const mission *m = world_mission(w);
    /* a vehicle's slot (+0x10, 0x100c540d / 0x100c546b -> 0x100425c9): its group's count so far, counted up; the
     * first class 2 / 8 vehicle (the record's +0) of the player's group (0x10110e58, set by the player's GPS) after
     * the player takes slot 4 instead, uncounted */
    int count[16] = {0}, lead_slot[16], slot4[16] = {0}, pgroup = -1, pgroup_any = -1;
    for (int g = 0; g < 16; g++) { lead_slot[g] = 0; us->lead[g] = -1; }
    for (int i = 0; i < m->ngps; i++) if (m->gps[i].role == 0) pgroup_any = m->gps[i].group & 15;
    us->pgroup = pgroup_any >= 0 ? pgroup_any : 0;
    for (int g = 0; g < 16; g++) us->gobj[g] = -2;
    for (int i = 0; i < m->ngps && us->n < UNITS_MAX; i++) {
        const mission_gps *gp = &m->gps[i];
        int g = gp->group & 15;
        if (gp->role == 0) {   /* the player */
            pgroup = g;
            if (gp->leader) { lead_slot[g] = count[g]; us->lead[g] = -2; }   /* -2: the player's unit */
            count[g]++;
            continue;
        }
        unit *u = &us->u[us->n];
        memset(u, 0, sizeof *u);
        u->gps = i;
        snprintf(u->gear, sizeof u->gear, "%s", gp->gear);
        int raw = gearmodel_load(a, gp->gear, &u->gm) ? -1 : u->gm.vclass_raw;
        int slot;
        if (g == pgroup && (raw == 2 || raw == 8) && !slot4[g]) { slot = 4; slot4[g] = 1; }
        else slot = count[g]++;
        if (gp->leader) { lead_slot[g] = slot; us->lead[g] = us->n; }   /* 0x10042603 from the loader */
        u->group = g; u->slot = slot; u->leader = gp->leader;
        u->vflags = gp->vflags;
        u->react = (gp->react ? gp->react : 1) + 1;   /* 0x1000a223 */
        u->skill = gp->skill ? gp->skill : 5;
        u->side = m->group_side[g];
        u->target = -1;
        u->order_target = -1;
        u->ai_state = u->ai_prev = u->ai_next = -1;
        u->cover = u->ai_got = -1;
        u->start_at = 0x43e + rand() % 0x16a;
        if (raw < 0) continue;
        /* the difficulty's internal multiplier (0x1004e4cc) goes to the vehicles whose group (+8) is the player's
         * (single play), by the difficulty (hgdif.cfg +5, units_set_internal_scale) */
        combat_init(&u->cb, a, gp->gear, g == pgroup_any ? internal_scale : 1.0f);
        if (u->cb.gr.version == 1) u->s.top_speed = v1_top_speed(a, gp->gear);
        u->cb.vclass = u->gm.vclass ? u->gm.vclass : 1;
        u->cb.strike_zone = u->gm.nparts && u->gm.part[0].rec == 0 ? u->gm.part[0].zone : 0;
        {
            int ty[GEAR_MAX_WEAPONS], ki[GEAR_MAX_WEAPONS];
            for (int q = 0; q < u->cb.gr.nweapons; q++) { ty[q] = u->cb.gr.weapon[q].type; ki[q] = u->cb.gr.weapon[q].kind; }
            gearmodel_stand(&u->gm, a, ty, ki, u->cb.gr.nweapons, u->cb.gr.version);   /* the standing pose (0x1001386b) */
        }
        us->n++;
    }
    for (int i = 0; i < us->n; i++) {
        unit *u = &us->u[i];
        /* the group's formation by name (0x100424be, case-insensitive); formation 0 until one is named */
        const mission_form *f = m->nform ? &m->form[0] : NULL;
        for (int k = 0; k < m->nform; k++)
            if (same_ci(m->form[k].name, m->group_form[u->group])) f = &m->form[k];
        mission_start st = mission_group_start(m, u->group);
        float x = 0, z = 0, hd = 0;
        if (st.found) {
            u->placed = 1;
            x = st.x; z = st.z; hd = st.heading;
            if (!u->leader && f && u->slot < f->nslots && lead_slot[u->group] < f->nslots) {
                float ox = f->x[u->slot] - f->x[lead_slot[u->group]], oz = f->z[u->slot] - f->z[lead_slot[u->group]];
                float s = sinf(hd * PI_F / 180), c = cosf(hd * PI_F / 180);
                x += ox * c + oz * s;
                z += -ox * s + oz * c;
                hd = f->heading[u->slot];   /* the slot's heading as it stands (0x100428c2; MEASURED: mission 1's star
                                             * slots face 0 with the leader at 90) */
            }
        }
        /* on the ground at the standing height; class 5 (the Orca) starts at its cruise height, the gear block's
         * +0x520 = 4000 set for classes 5 and 8 (0x100c4e37 on; MEASURED: mission 1's Orca at ground + 4000, while
         * its class 8 Redjackets stand on their soles, a difference not traced) */
        float gy = ground_at(w, x, z);
        float h = u->gm.vclass == 5 ? 4000.0f : u->gm.stand;
        sim_gear_init(&u->s, x, gy + h, z, hd, u->s.top_speed, h);
        u->alt_land = h;   /* +0x588 = +0x520 (0x100c4e37 on) */
        u->s.vclass = u->gm.vclass_raw ? u->gm.vclass_raw : 1;   /* the class's control rules (0x10060491) */
        for (int k = 0; k < u->gm.nparts; k++) {
            u->obj[k] = world_add_object(w, u->gm.part[k].model, 0, 0, 0, 0);
            if (!u->placed) world_show_object(w, u->obj[k], 0);
        }
    }
    units_update(us, w);
}

void units_update(units *us, world *w)
{
    for (int i = 0; i < us->n; i++) {
        unit *u = &us->u[i];
        if (u->player) continue;
        /* a destroyed vehicle stays where it fell as a wreck (0x10059e9d sets its state 4; the vehicle stays in the
         * table), less its destroyed parts. Not done: its fall and smoke (the state 4 animation), it keeps its pose */
        if (u->cb.dead && !u->shown_dead) {
            for (int k = 0; k < u->gm.nparts; k++)
                if (gearmodel_part_gone(&u->gm, k, u->cb.zone_gone))
                    world_show_object(w, u->obj[k], 0);
            u->shown_dead = 1;
        }
        if (u->cb.dead) continue;
        float s = sinf(u->s.heading * PI_F / 180), c = cosf(u->s.heading * PI_F / 180);
        /* the torso's turn (0x1005d75c: the dum_trso object, +0x48c, turned by the twist / 1.5 for classes 1 / 9, the
         * twist for the others, 0x1009556a): about its record's origin, it and the records under it */
        int tr = -1;
        for (int r = 0; r < u->gm.nrec && r < 96; r++) if (!memcmp(u->gm.rec_tag[r], "TRSO", 4)) { tr = r; break; }
        /* gears and infantry (0x1005d75c, 0x1005ffa6 / 0x10060357): the object turned order 1 by (the tilt / 3 / 2 about
         * x, the twist / 1.5 about y, 0): R = Ry(twist / 1.5) Rx(tilt / 6); classes 4 / 10 by the twist alone; others
         * by the twist (their pitch, +0x6c with the 0x560 bit 0x2000, isn't done) */
        float ta = 0, tx = 0;
        if (tr >= 0) {
            ta = u->s.twist.cur;
            if (u->gm.vclass_raw == 1 || u->gm.vclass_raw == 9) { ta /= 1.5f; tx = u->s.tilt.cur / 6.0f; }
        }
        float ts = sinf(ta * PI_F / 180), tc = cosf(ta * PI_F / 180);
        float xs = sinf(tx * PI_F / 180), xc = cosf(tx * PI_F / 180);
        const float rt[9] = {tc, ts * xs, ts * xc, 0, xc, -xs, -ts, tc * xs, tc * xc};   /* Ry(ta) Rx(tx) */
        for (int k = 0; k < u->gm.nparts; k++) {
            if (gearmodel_part_gone(&u->gm, k, u->cb.zone_gone))
                world_show_object(w, u->obj[k], 0);
            const gearmodel_part *p = &u->gm.part[k];
            float px = p->x, py = p->y, pz = p->z, pm[9];
            memcpy(pm, p->m, sizeof pm);
            int under = 0;
            for (int r = p->rec, n = 0; r >= 0 && r < 96 && n < 96; r = u->gm.rec_parent[r], n++)
                if (r == tr) { under = 1; break; }
            if (under && (ta != 0 || tx != 0)) {
                const float *o = u->gm.rec[tr];
                float dx = px - o[0], dy = py - o[1], dz = pz - o[2];
                px = o[0] + rt[0] * dx + rt[1] * dy + rt[2] * dz;
                py = o[1] + rt[3] * dx + rt[4] * dy + rt[5] * dz;
                pz = o[2] + rt[6] * dx + rt[7] * dy + rt[8] * dz;
                for (int r = 0; r < 3; r++)
                    for (int q = 0; q < 3; q++)
                        pm[r * 3 + q] = rt[r * 3] * p->m[q] + rt[r * 3 + 1] * p->m[3 + q] + rt[r * 3 + 2] * p->m[6 + q];
            }
            world_move_object(w, u->obj[k], u->s.x + px * c + pz * s, u->s.y + py, u->s.z - px * s + pz * c,
                              u->s.heading);
            {   /* the part's posed rotation under the vehicle's yaw */
                const float ry[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
                float m[9];
                for (int r = 0; r < 3; r++)
                    for (int q = 0; q < 3; q++)
                        m[r * 3 + q] = ry[r * 3] * pm[q] + ry[r * 3 + 1] * pm[3 + q] + ry[r * 3 + 2] * pm[6 + q];
                world_set_object_matrix(w, u->obj[k], m);
            }
        }
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

int units_hit(const units *us, const world *w, int owner, const float p0[3], const float p1[3], float *t, int *zone)
{
    int best = -1;
    float bt = 2.0f;
    for (int i = 0; i < us->n; i++) {
        const unit *u = &us->u[i];
        /* wrecks stay in the scene and stop shots (0x100229d9 tests every object but the firer's; 0x1005c1f8 harms no
         * dead vehicle: combat_damage_raw) */
        if (i == owner || !u->placed) continue;
        /* reject segments nowhere near (the parts lie within a few hundred units of the origin) */
        float cx = u->s.x, cy = u->s.y, cz = u->s.z;
        float lo[3] = {cx - 1500, cy - 1500, cz - 1500}, hi[3] = {cx + 1500, cy + 1500, cz + 1500};
        float tt;
        if (!seg_box(p0, p1, lo, hi, &tt)) continue;
        float s = sinf(u->s.heading * PI_F / 180), c = cosf(u->s.heading * PI_F / 180);
        for (int k = 0; k < u->gm.nparts; k++) {
            const gearmodel_part *p = &u->gm.part[k];
            if (gearmodel_part_gone(&u->gm, k, u->cb.zone_gone)) continue;
            float mn[3], mx[3];
            if (world_object_bounds(w, u->obj[k], mn, mx)) continue;
            /* into the part's frame: world = R(heading) * local + position, R: x' = x c + z s, z' = -x s + z c */
            float px = u->s.x + p->x * c + p->z * s, py = u->s.y + p->y, pz = u->s.z - p->x * s + p->z * c;
            float a[3], b[3];
            const float *q[2] = {p0, p1};
            float *r[2] = {a, b};
            for (int e = 0; e < 2; e++) {
                float dx = q[e][0] - px, dz = q[e][2] - pz;
                r[e][0] = dx * c - dz * s;
                r[e][1] = q[e][1] - py;
                r[e][2] = dx * s + dz * c;
            }
            if (seg_box(a, b, mn, mx, &tt) && tt < bt) { bt = tt; best = i; *zone = p->zone; }
        }
    }
    if (best >= 0) *t = bt;
    return best;
}

typedef struct { const unit *u; } mount_ctx;

static void unit_mount(void *ctx, int kind, float from[3])
{
    const unit *u = ((const mount_ctx *)ctx)->u;
    gearmodel_mount(&u->gm, u->s.x, u->s.y, u->s.z, u->s.heading, kind, from);
}

/* a weapon's reach (0x1000502f): its flight (life x speed) held to its max range (+0x50); with +0x86 bit 0x40 the
 * max range */
static float weapon_reach(int t)
{
    const gear_weapon_stats *st = &gear_weapon_stat[t];
    if (st->sflags & 0x40) return st->max_range;
    float r = (float)st->life * st->speed;
    return st->max_range <= r ? st->max_range : r;
}

/* ==== The AI's weapon choice (traced: 0x100073b7, 0x100060a7, 0x10006473 and their helpers) ==== */

/* 0x1000502f: the weapon's band at the distance (+0xd4): 3 nearer than +0x4c, 2 within its reach, 1 at it, 0 beyond */
static int ai_band(int t, float d)
{
    const gear_weapon_stats *st = &gear_weapon_stat[t];
    float r = weapon_reach(t);
    return d < st->min_range ? 3 : d < r ? 2 : d <= r ? 1 : 0;
}

static int ai_slot_ok(const combatant *c, int k)
{
    int t = c->gr.weapon[k].type;
    return k >= 0 && k < c->gr.nweapons && t >= 0 && t < GEAR_WEAPON_TYPES && c->state[k] >= 0;
}

/* 0x10006cb4: the weapon types for infantry (0, 1, 9, 0xc) */
static int ai_for_infantry(int t) { return t == 0 || t == 1 || t == 9 || t == 0xc; }

/* 0x10005ef1: the selected weapon ready (+8 == 1), loaded and the target in its band (1 / 2) */
static int ai_ready(const combatant *c, float d)
{
    int k = c->sel;
    if (!ai_slot_ok(c, k) || c->state[k] != 1 || c->ammo[k] == 0) return 0;
    int b = ai_band(c->gr.weapon[k].type, d);
    return b == 1 || b == 2;
}

/* 0x100073b7: choose again - the selected weapon out of its band (0 / 3), the target's kind (infantry, 0x10006b27)
 * not the weapon's (0x10006cb4), it empty, or another weapon of more power (+0x78) in its band (0x10007313) */
static int ai_rechoose(const combatant *c, int tinf, float d)
{
    int k = c->sel;
    if (!ai_slot_ok(c, k)) return 1;
    int t = c->gr.weapon[k].type, b = ai_band(t, d);
    if (b == 3 || b == 0) return 1;
    if (tinf != ai_for_infantry(t)) return 1;
    if (c->ammo[k] < 1 && c->ammo[k] != -1) return 1;
    for (int q = 0; q < c->gr.nweapons; q++) {
        if (q == k || !ai_slot_ok(c, q)) continue;
        int tq = c->gr.weapon[q].type;
        if ((unsigned)gear_weapon_stat[t].power < (unsigned)gear_weapon_stat[tq].power && ai_band(tq, d) == 2) return 1;
    }
    return 0;
}

/* 0x100060a7: over the slots from the next (0x100bbcef: selectable, +8 & 4), each that 0x10005ef1 passes scores +1 as
 * the first, else +2 endless ammunition or more than the best's; +1 when its +0x86 & 0x40 matches the aim being
 * blocked (0x10006af8 / 0x10004d60); +1 when it is for infantry as the target is; by power (+0x78): +1 the first, +3
 * more than the best's (not infantry), +5 less (infantry); +2 guided (+0x86 & 1); not artillery (0x10006d13: kinds
 * 0x3d, 0x42, 0x43): +2 with an artillery piece selected, +1 an aircraft target (0x10066eeb), +4 a structure; the
 * best (ties to the later) is selected. 1 if one was */
static int ai_pick(combatant *c, int tinf, int tair, int tstruct, int blocked, float d)
{
    int start = c->sel, best = -1, bs = 0, bammo = 0, bpow = 0;
    int cur_art = 0;
    if (ai_slot_ok(c, start)) {
        int kd = gear_weapon_stat[c->gr.weapon[start].type].kind;
        cur_art = kd == 0x3d || kd == 0x42 || kd == 0x43;
    }
    int n = c->gr.nweapons;
    for (int s = 1; s <= n; s++) {
        int k = n ? (start + s) % n : 0;
        if (!ai_slot_ok(c, k) || !(gear_weapon_stat[c->gr.weapon[k].type].flags & 4)) continue;
        int save = c->sel;
        c->sel = k;
        int ok = ai_ready(c, d);
        c->sel = save;
        if (!ok) continue;
        int t = c->gr.weapon[k].type, sc = 0, am = c->ammo[k];
        const gear_weapon_stats *st = &gear_weapon_stat[t];
        if (bammo == 0) sc += 1;
        else if (am == -1) sc += 2;
        else if (bammo < am) sc += 2;
        if (((st->sflags & 0x40) != 0) == blocked) sc += 1;
        if (ai_for_infantry(t) == tinf) sc += 1;
        int pw = st->power;
        if (bpow == 0) sc += 1;
        else if (!tinf) { if ((unsigned)bpow < (unsigned)pw) sc += 3; }
        else if ((unsigned)pw < (unsigned)bpow) sc += 5;
        if (st->sflags & 1) sc += 2;
        if (!(st->kind == 0x3d || st->kind == 0x42 || st->kind == 0x43)) {
            if (cur_art) sc += 2;
            if (tair) sc += 1;
            if (tstruct) sc += 4;
        }
        if (bs <= sc) { bs = sc; best = k; bammo = am; bpow = pw; }
    }
    if (best >= 0) c->sel = best;
    return best >= 0;
}

/* 0x10006473 (skill rows with +8, +0x1be): every weapon in its band (2), loaded and working is armed together - its
 * slot's group (+0xc) 1, the others 0 - while the heat they add (the weapon table's +0x44) stays below the row's
 * limit (+0x1e0 = (int)(percentage / 100 x 65)) over the vehicle's heat (+0x4e8: nothing in the shipped sim raises it,
 * 0x100619b8 only behind 0x10148e0c, never set: 0); reaching it arms none. The first guided one ready is selected,
 * else the group's first (0x100bc0e4); the fire controller then pulls the group (control +0x28). 1 if any */
static int ai_pick_all(combatant *c, float d, int heat_limit)
{
    int any = -1, guided = -1;
    float heat = 0.0f;
    for (int k = 0; k < c->gr.nweapons; k++) {
        c->wgroup[k] = 0;
        if (!ai_slot_ok(c, k) || c->ammo[k] == 0 || ai_band(c->gr.weapon[k].type, d) != 2) continue;
        int ty = c->gr.weapon[k].type;
        float h = ty >= 0 && ty < 46 ? weapon_heat[ty] : 0.0f;
        if ((float)heat_limit <= heat + h) { any = -1; break; }
        heat += h;
        c->wgroup[k] = 1;
        if (any < 0) any = k;
        if (guided < 0 && (gear_weapon_stat[ty].sflags & 1) && c->state[k] == 1) guided = k;
    }
    c->fire_all = any >= 0;
    if (guided >= 0) c->sel = guided;
    else if (any >= 0) c->sel = any;
    return any >= 0;
}

/* The AI skill table (0x10107c0c -> 0x10107b08, 0x1c bytes a row, picked by +0x1b5; 0x1000a52e): +4 a range (+0x274,
 * 15000 if 0: 0x100a6212), +8 (+0x1be: 1 / 2 the second fire flag), +0xc a heat limit percentage (+0x1e0 = (int)(v
 * / 100 x 65): ai_pick_all's limit), +0x10 the lock
 * percentage (+0x1e4, 0x10006672), +0x14 (+0x1ec), +0x18 (+0x1f0): 1 picks the leading fire controller (0x10005570)
 * over 0x10005a87 (0x10005300). */
static const struct { int range, flag, heat, lock, x14, lead; } skill_row[9] = {
    {0, 0, 75, 25, 0, 0}, {20000, 0, 75, 25, 0, 0}, {20000, 1, 100, 15, 0, 0}, {15000, 2, 75, 25, 1, 0},
    {15000, 2, 75, 60, 0, 0}, {25000, 0, 75, 100, 0, 0}, {15000, 0, 80, 85, 0, 1}, {25000, 0, 75, 25, 0, 0},
    {30000, 0, 75, 35, 0, 1},
};

static float wrap180(float a)
{
    a = fmodf(a, 360.0f);
    if (a > 180.0f) a -= 360.0f;
    if (a < -180.0f) a += 360.0f;
    return a;
}

/* a unit's eye (its EYE_ point turned with it; 100 up without one) */
static void unit_eye(const unit *u, float e[3])
{
    float s = sinf(u->s.heading * PI_F / 180), c = cosf(u->s.heading * PI_F / 180);
    float ex = u->gm.found_eye ? u->gm.eye[0] : 0, ey = u->gm.found_eye ? u->gm.eye[1] : 100.0f,
          ez = u->gm.found_eye ? u->gm.eye[2] : 0;
    e[0] = u->s.x + ex * c + ez * s;
    e[1] = u->s.y + ey;
    e[2] = u->s.z - ex * s + ez * c;
}

/* The scene ray (0x100bcf44 -> 0x100229d9 / 0x100aa018): from the vehicle's eye along its aim (heading, pitch) out to
 * `len`: the unit it strikes first, -2 if an object or the ground stops it first, -1 if nothing. The ground is
 * sampled every 500 (the port's ground; the original's tiles are scene objects). */
static int aim_ray(const units *us, world *w, int i, float heading, float pitch, float len)
{
    const unit *u = &us->u[i];
    float e[3], hr = heading * PI_F / 180, pr = pitch * PI_F / 180;
    unit_eye(u, e);
    float f[3] = {e[0] + sinf(hr) * cosf(pr) * len, e[1] + sinf(pr) * len, e[2] + cosf(hr) * cosf(pr) * len};
    float tu = 2, to = 2, tg = 2;
    int zone, v = units_hit(us, w, i, e, f, &tu, &zone);
    if (v < 0) tu = 2;
    if (world_ray(w, e, f, &to) < 0) to = 2;
    int n = (int)(len / 500.0f);
    for (int k = 1; k < n; k++) {
        float t = (float)k / (float)n;
        int found;
        float g = world_ground_height(w, e[0] + (f[0] - e[0]) * t, e[2] + (f[2] - e[2]) * t, &found);
        if (found && g > e[1] + (f[1] - e[1]) * t) { tg = t; break; }
    }
    if (v >= 0 && tu <= to && tu <= tg) return v;
    return to < 2 || tg < 2 ? -2 : -1;
}

/* the bearing (degrees, the heading convention) and the pitch from a's eye to point p */
static void bearing_to(const unit *a, const float p[3], float *hd, float *pt, float *flat, float *dist)
{
    float e[3];
    unit_eye(a, e);
    float dx = p[0] - e[0], dy = p[1] - e[1], dz = p[2] - e[2];
    *flat = sqrtf(dx * dx + dz * dz);
    *dist = sqrtf(dx * dx + dy * dy + dz * dz);
    *hd = atan2f(dx, dz) * 180.0f / PI_F;
    *pt = atan2f(dy, *flat) * 180.0f / PI_F;
}

/* 0x10004d60 (with its gate): a vehicle facing within 90 degrees of the bearing to its target and the ray along its
 * aim, out to the target's distance, striking the target first or nothing */
static int aim_clear(const units *us, world *w, int i, int target, float pitch)
{
    const unit *u = &us->u[i], *t = &us->u[target];
    float p[3] = {t->s.x, t->s.y, t->s.z}, hd, pt, fl, d;
    bearing_to(u, p, &hd, &pt, &fl, &d);
    float a = fmodf(u->s.heading - hd + 720.0f, 360.0f);
    if (a > 90.0f && a < 270.0f) return 0;
    int r = aim_ray(us, w, i, u->s.heading, pitch, d);
    return r == target || r == -1;
}

/* the attack state's turn (0x1000d679): the control's turn = 819.2 x the heading error / 45, held to +-819.2 (of
 * 1024) */
static float steer(float err)
{
    float t = err / 45.0f;
    if (t > 1) t = 1;
    if (t < -1) t = -1;
    return 0.8f * t;
}

/* the throttle toward a distance (0x1000d2bd, its plain branch; the skating and crouched branches aren't modelled):
 * beyond twice the distance full (1/64 of the control's), from it to twice it 0.8, nearer 0; aircraft and class 2
 * always full; turning harder than 45 / 1024, 1 - |turn| x 1024 / 1536 (at least 0.1); below 0.1, 0. (Above 65 heat
 * it halves: the heat +0x4e8 stays 0 in the shipped sim, see ai_pick_all.) */
static float approach(const unit *u, float flat, float want)
{
    int cls = u->gm.vclass_raw;
    float t = flat > 2.0f * want ? 1.0f : flat >= want ? 0.8f : 0.0f;
    if (cls == 2 || cls == 8) t = 1.0f;
    if (t != 0 && fabsf(u->turn_ctl) * 1024.0f > 45.0f) {
        t = 1.0f - fabsf(u->turn_ctl) * 1024.0f / 1536.0f;
        if (t < 0.1f) t = 0.1f;
    }
    if (t < 0.1f) t = 0;
    if ((cls == 2 || cls == 8) && t == 0) t = 0.8f;
    return t;
}

/* 0x1000536e: the heading window of 0x10005a87: the target's apparent size, atan2(its +0x53c, the distance) in
 * degrees, held to 3 .. 10 (10 within 100 when under 10); x 1.2 for a gear's hand weapon (+0x5fb: the kind 10 one),
 * else x 1.5 for a guided one */
static float fire_window(const unit *u, const unit *t, float dist, int sel)
{
    float size = t->gm.stand, a = atan2f(size, dist) * 180.0f / PI_F, wnd;
    if (dist >= 100.0f || a >= 10.0f) wnd = a > 10.0f ? 10.0f : a < 3.0f ? 3.0f : a;
    else wnd = 10.0f;
    int ty = u->cb.gr.weapon[sel].type;
    if (u->gm.vclass_raw == 1 && u->cb.gr.weapon[sel].kind == 10) wnd *= 1.2f;
    else if (ty >= 0 && ty < GEAR_WEAPON_TYPES && (gear_weapon_stat[ty].sflags & 1)) wnd *= 1.5f;
    return wnd;
}

/* The sim's random numbers (0x10089e69): src/hgrand.c, the cursor shared by every sim caller */
int units_rand(int n)
{
    return hg_rand(n);
}

/* The combat AI's state tables (0x10123c48 + class x 8: i16 count, i16 m, a pointer to rows of 0x12 bytes: i16 state,
 * i16 n, i16 next[7]). A vehicle with no last state draws from the first count - m rows (0x100a6888), else from its
 * last state's row's n next states (row 0 if its last state has no row). Classes 0, 3, 6, 7, 10: state 0 only. */
typedef struct { short state, n, next[7]; } ai_row;
static const ai_row ai_rows_gear[13] = {   /* 0x10123998: classes 1 and 5 */
    {1, 5, {19, 1, 2, 3, 4}}, {2, 5, {7, 8, 4, 19, 19}}, {3, 4, {7, 8, 19, 19}}, {4, 4, {8, 7, 8, 19}},
    {19, 7, {3, 2, 8, 4, 1, 3, 8}}, {0, 7, {0, 1, 2, 3, 4, 4, 19}}, {24, 2, {7, 8}}, {25, 3, {19, 2, 7}},
    {7, 3, {20, 1, 8}}, {8, 5, {19, 1, 2, 3, 4}}, {27, 3, {19, 8, 7}}, {10, 3, {7, 7, 4}}, {26, 2, {3, 1}},
};
static const ai_row ai_rows_air[3] = {     /* 0x10123b28: classes 2 and 8 */
    {2, 2, {7, 13}}, {7, 3, {13, 2, 13}}, {13, 3, {13, 2, 13}},
};
static const ai_row ai_rows_ground[6] = {  /* 0x10123aa0: class 4 */
    {0, 4, {2, 19, 3, 8}}, {2, 4, {2, 19, 0, 3}}, {3, 4, {2, 0, 19}}, {19, 3, {2, 0, 8}}, {8, 4, {2, 19, 3}},
    {10, 1, {19}},
};
static const ai_row ai_rows_inf[13] = {    /* 0x10123b60: class 9 (and vehicles whose +0x538 is 1) */
    {0, 7, {0, 1, 2, 3, 4, 4, 4}}, {1, 6, {0, 1, 2, 3, 4, 4}}, {2, 4, {7, 8, 4, 4}}, {3, 4, {7, 8, 4, 4}},
    {4, 3, {5, 5, 8}}, {19, 7, {3, 2, 8, 4, 1, 3, 8}}, {5, 2, {7, 8}}, {6, 1, {6}}, {7, 5, {0, 1, 2, 8, 4}},
    {8, 6, {0, 1, 2, 3, 4, 4}}, {9, 3, {0, 8, 7}}, {10, 4, {7, 7, 4, 4}}, {11, 1, {3}},
};
static const ai_row ai_rows_one[1] = {{0, 1, {0}}};   /* 0x10123a88 */

static const ai_row *ai_table(int cls, int *count, int *m)
{
    switch (cls) {
    case 1: case 5: *count = 13; *m = 8; return ai_rows_gear;
    case 2: case 8: *count = 3; *m = 2; return ai_rows_air;
    case 4: *count = 6; *m = 2; return ai_rows_ground;
    case 9: *count = 13; *m = 7; return ai_rows_inf;
    default: *count = 1; *m = 0; return ai_rows_one;
    }
}

/* the states a random interruption forces (0x10123cf0, by rand(0x40) >> 4) */
static const short ai_interrupt[4] = {24, 26, 25, 27};

/* the 16 points around a vehicle (0x10123860: x and z divisors of the radius; 0 none, 1 the radius, else radius /
 * v), turned with the vehicle (0x100a7928) */
static const float ai_dirs[16][2] = {
    {0, 1}, {2, 1}, {1, 1}, {1, 2}, {1, 0}, {1, -2}, {1, -1}, {2, -1}, {0, -1}, {-2, -1}, {-1, -1}, {-1, -2},
    {-1, 0}, {-1, 2}, {-1, 1}, {-2, 1},
};

static void ai_point(const unit *about, int slot, float radius, float *x, float *z)
{
    float ox = ai_dirs[slot & 15][0], oz = ai_dirs[slot & 15][1];
    ox = ox == 0 ? 0 : ox == 1 ? radius : radius / ox;
    oz = oz == 0 ? 0 : oz == 1 ? radius : radius / oz;
    float s = sinf(about->s.heading * PI_F / 180), c = cosf(about->s.heading * PI_F / 180);
    *x = about->s.x + ox * c + oz * s;
    *z = about->s.z - ox * s + oz * c;
}

/* 0x100a6a5e: a state's start - its end tick (0x235a ticks on unless the state sets it) and its own values */
static void ai_enter(units *us, unit *u, int t)
{
    int now = us->ticks;
    u->ai_t1 = u->ai_t2 = 0;
    u->ai_side = 0;
    u->ai_step = 0;
    u->ai_pt = 0;
    u->ai_reverse = 0;
    u->ai_end = now + 0x235a;
    switch (u->ai_state) {
    case 0: u->ai_end = now + (units_rand(5) + 8) * 0xb5; break;
    case 1: u->ai_end = now + 0xe24; u->ai_side = -1; break;
    case 3: u->ai_side = units_rand(2) ? 1 : -1; break;
    case 4: u->ai_end = now + 0x1c48; u->ai_step = 1; break;
    case 7: {
        float dx = 0, dz = 0;
        if (t >= 0) { dx = us->u[t].s.x - u->s.x; dz = us->u[t].s.z - u->s.z; }
        int slot = sqrtf(dx * dx + dz * dz) >= 4000.0f ? (units_rand(2) ? 3 : 15) : (units_rand(2) ? 4 : 12);
        ai_point(u, slot, 10000.0f, &u->ai_px, &u->ai_pz);
        u->ai_pt = 1;
        break;
    }
    case 8: u->ai_end = now + (units_rand(0xb) + 10) * 0xb5; u->ai_reverse = 1; break;
    case 10: if (u->gm.vclass_raw != 8) u->ai_reverse = 1; u->ai_end = now + 0x5a8; break;
    case 19: u->ai_end = now + 0x2a6c; u->ai_step = 1; break;
    case 20: u->ai_end = now + 0xe24; u->ai_step = 1; break;
    case 21: u->ai_end = now + 0x389; u->ai_step = 1; break;
    case 14: u->ai_end = 0x7fffffff; u->ai_step = 1; u->ai_tm[0] = 0; u->ai_out = 0; u->ai_got = -1; u->cover = -1; break;
    case 16: case 17: {   /* 0x100a6a5e: 16 0x712 ticks; 17 0x3fa2, its peek after 0x5a8, step 0x11; both take the
                           * point (+0x236, 0x1002708f: +0x10 = 1, +0x14 = the vehicle) */
        if (u->ai_state == 16) u->ai_end = now + 0x712;
        else { u->ai_end = now + 0x3fa2; u->ai_t1 = now + 0x5a8; u->ai_step = 0x11; u->ai_r = 0; u->ai_tm[1] = 0;
               u->ai_tm[2] = 0; u->ai_shots = 0; }
        int k = u->cover;
        if (k >= 0 && k < us->ncov) { us->cov_taken[k] = 1; us->cov_by[k] = (int)(u - us->u); }
        break;
    }
    case 22: case 23: u->ai_end = 0x7fffffff; u->ai_step = 1; u->ai_r = 10000; u->ai_tm[0] = -1; u->ai_tm[1] = 0; break;
    case 24: case 25: u->ai_end = now + 0x2d4; break;
    case 26: u->ai_end = now + 0x43e; break;
    case 27: u->ai_t1 = now + (units_rand(4) + 1) * 0x5a; u->ai_end = u->ai_t1; u->ai_side = units_rand(2); break;
    }
}

/* 0x100a6956: the next state - a forced one (0x100a6603), else the class's rules (0x100a66b4 for gears, 0x100a67f6
 * for the others), else the table (0x100a6888) */
/* 0x100a7203's cover cases: 16 / 17 give the point up (0x100270bf) and stand (0x100ac08a; 17 also stops the
 * straffe, 0x100ac0bb(2)); 14 / 22 / 23 and 17 failing (+0x228 -1) forget the state, the last one and the points */
static int  anim_offscreen(const units *us, const unit *u);
static void ai_leave(units *us, unit *u)
{
    int st = u->ai_state;
    if (st == 16 || st == 17) {
        int k = u->cover;
        if (k >= 0 && k < us->ncov && us->cov_by[k] == (int)(u - us->u)) { us->cov_taken[k] = 0; us->cov_by[k] = -1; }
        u->ai_crouch = 0;
        if (st == 17) u->ai_straffe = 0;
    }
    if ((st == 14 || st == 17 || st == 22 || st == 23) && u->ai_out == -1) {
        u->ai_prev = -1;
        u->ai_got = -1;
        if (st != 17) u->cover = -1;
    } else u->ai_prev = st;
    u->ai_state = -1;
    u->ai_reverse = 0;
}

/* the cover table (0x10123ca8: 10 rows, the first pick rand(10 - 9), so state 14) */
static const ai_row ai_rows_cover[10] = {
    {14, 7, {17, 16, 16, 17, 17, 16, 16}}, {19, 1, {14}}, {25, 4, {19, 22, 14, 23}}, {26, 4, {19, 23, 22, 14}},
    {27, 4, {19, 14, 23, 22}}, {23, 6, {16, 22, 14, 23, 17, 23}}, {22, 5, {17, 22, 14, 16, 17}},
    {16, 3, {22, 23, 23}}, {17, 3, {22, 23, 22}}, {10, 1, {14}},
};

static int ai_choose(units *us, unit *u, int t, float flat)
{
    int cls = u->gm.vclass_raw;
    /* 0x100a64bd: a gear (not dying, +0xbdb != 5) takes the cover table while a free cover point lies within 28000
     * of it (0x100ac564 -> 0x100279a3), else its class's; switching to the cover table forgets the state and the
     * last one; switching to the class's with no last state starts by the target: 0 none or not a vehicle, 2 an
     * aircraft (0x10066eeb), else 1 */
    if (cls == 1 && !u->cb.dead) {
        int cov = cover_near(us, u, 28000.0f) > 0;
        if (cov && u->ai_tab != 2) { u->ai_tab = 2; u->ai_prev = -1; }
        else if (!cov && u->ai_tab != 1) {
            u->ai_tab = 1;
            if (u->ai_prev < 0) {
                if (t < 0) return 0;
                return us->u[t].gm.vclass_raw == 5 || us->u[t].gm.vclass_raw == 8 ? 2 : 1;
            }
        }
    }
    if (u->ai_next >= 0) { int s = u->ai_next; u->ai_next = -1; return s; }
    int t_air = t >= 0 && (us->u[t].gm.vclass_raw == 5 || us->u[t].gm.vclass_raw == 8);
    int self_air = cls == 5 || cls == 8;
    if (cls == 1) {
        /* (a gear target with its +0x560 bit 0x20 also gives 0: no code sets that bit) */
        if (t < 0 || t_air) return 0;
        if (flat <= 1000.0f && units_rand(0x13) == 3) return 0x15;
        if (flat <= 1000.0f && u->cb.gr.jump_jets && units_rand((0x14 - 10) * 2) == 0) return 0x14;
    } else {
        if (t < 0) return 0;
        if (t_air && !self_air) return 2;
    }
    int count, m;
    const ai_row *rows = ai_table(cls, &count, &m);
    if (cls == 1 && u->ai_tab == 2) { rows = ai_rows_cover; count = 10; m = 9; }
    if (u->ai_prev < 0) return rows[units_rand(count - m)].state;
    int r = 0;
    for (int k = 0; k < count; k++) if (rows[k].state == u->ai_prev) { r = k; break; }
    return rows[r].next[units_rand(rows[r].n)];
}

/* One tick of the state (0x100a58c0's switch) toward target t: its turn and throttle. Returns 1 when the state is
 * over (its own end, its time, or a forced next state). */
static int ai_step(units *us, world *w, int i, int t, float *turn, float *throttle, float attack_turn,
                   float attack_throttle, float err, float flat)
{
    unit *u = &us->u[i];
    int now = us->ticks, done = 0;
    /* the closing rate (0x100aaeec: the distance's change per tick, here per tick) */
    float closing = u->ai_lastd > 0 ? u->ai_lastd - flat : 0;
    u->ai_lastd = flat;
    float to_pt_err = 0, to_pt = 0;
    if (u->ai_pt) {
        float dx = u->ai_px - u->s.x, dz = u->ai_pz - u->s.z;
        to_pt = sqrtf(dx * dx + dz * dz);
        to_pt_err = wrap180(atan2f(dx, dz) * 180.0f / PI_F - u->s.heading);
    }
    *turn = 0; *throttle = 0;
    switch (u->ai_state) {
    case 0: {   /* attack (0x100a7e40): too close (under min(range, 4500)) forces state 10 */
        *turn = attack_turn; *throttle = attack_throttle;
        int sk = u->skill >= 0 && u->skill < 9 ? u->skill : 5;
        float rg = skill_row[sk].range ? (float)skill_row[sk].range : 15000.0f;
        if (flat < (rg >= 4500.0f ? 4500.0f : rg)) u->ai_next = 10;
        break;
    }
    case 1: {   /* a point around the target (0x100a7897 / 0x100a7f95): slot by the side it's on (0x100ab4d7), 15000
                 * out, renewed every 0x21f ticks; there, it turns to face the target */
        if (now >= u->ai_t1) {
            const unit *tu = &us->u[t];
            float b = wrap180(atan2f(u->s.x - tu->s.x, u->s.z - tu->s.z) * 180.0f / PI_F - tu->s.heading);
            int oct = (int)((b < 0 ? b + 360.0f : b) / 45.0f);
            if (oct > 7) oct = 0;
            int slot = oct == 0 ? (units_rand(2) ? 2 : 6) : oct == 1 ? 3 : oct == 7 ? 5 : oct < 5 ? 2 + (oct - 2) : oct - 1;
            ai_point(tu, slot * 2, 15000.0f, &u->ai_px, &u->ai_pz);
            u->ai_pt = 1;
            u->ai_t1 = now + 0x21f;
        }
        float a = approach(u, to_pt, 4000.0f);
        if (a == 0 && fabsf(err) > 0) *turn = steer(err);
        else { *turn = steer(to_pt_err); *throttle = a; }
        break;
    }
    case 2: case 3: {   /* closing in to 5500 (0x100a8135): over when within max(closing / 7.7, 1) x 8000; state 3 also
                         * probes, else swerves by 450 / 1024 of the turn, its side flipped every 0x21f ticks */
        *turn = attack_turn;
        *throttle = approach(u, flat, 5500.0f);
        float k = closing / 7.7f;
        if (k < 1.0f) k = 1.0f;
        if (flat <= k * 8000.0f) done = 1;
        if (u->ai_state == 3) {
            /* 0x100a58c0 case 3: the obstacle probe (0x100aa2ac) first; the swerve only when it doesn't steer */
            if (!ai_probe(us, w, i, wrap180(u->s.heading + err), turn, throttle))
                *turn += (float)u->ai_side * 450.0f / 1024.0f;
            if (u->ai_t1 <= now) { u->ai_side = -u->ai_side; u->ai_t1 = now + 0x21f; }
        }
        break;
    }
    case 4: case 20: {   /* 0x100a9434 / 0x100a9636, gears with jump jets: in to 5000 (20: within 1000, backwards for
                          * 0x2d4 ticks), then a jump (the control's +0x1e; jumps aren't modelled: it stands) and
                          * 0x5a8 ticks */
        if (u->ai_step == 1) {
            float lim = u->ai_state == 4 ? 5000.0f : 1000.0f;
            if (!u->cb.gr.jump_jets || (u->ai_state == 4 ? flat < lim : flat > lim)) return 1;   /* +0x514 < 1 */
            u->ai_step = 2;
            u->ai_t1 = now + 0x2d4;
            if (u->ai_state == 20) u->ai_reverse = 1;
        } else if (u->ai_step == 2) {
            *turn = attack_turn;
            *throttle = u->ai_reverse ? -1.0f : 1.0f;
            if (u->ai_state == 4 ? flat <= 5000.0f : now >= u->ai_t1) { u->ai_step = 3; u->ai_t2 = now + 0x5a8; }
        } else if (u->ai_step == 3) {
            if (now >= u->ai_t2) { u->ai_reverse = 0; return 1; }
        }
        break;
    }
    case 7: case 19: {   /* 7: a point 10000 beside itself, at 3000 (0x100a82ed); 19: a point 8000 out from the
                          * target, square to the line between them, either side, at 2000 (0x100a9a46), again on arrival */
        if (u->ai_state == 19 && u->ai_step == 1) {
            const unit *tu = &us->u[t];
            float dx = u->s.x - tu->s.x, dz = u->s.z - tu->s.z, l = sqrtf(dx * dx + dz * dz);
            float sd = units_rand(2) ? 1.0f : -1.0f;
            if (l < 1) l = 1;
            u->ai_px = tu->s.x + dz / l * sd * 8000.0f;
            u->ai_pz = tu->s.z - dx / l * sd * 8000.0f;
            u->ai_pt = 1;
            u->ai_step = 2;
            break;
        }
        float want = u->ai_state == 7 ? 3000.0f : 2000.0f;
        *turn = steer(to_pt_err);
        *throttle = approach(u, to_pt, want);
        if (u->ai_state == 7 && to_pt <= 3000.0f) done = 1;
        if (u->ai_state == 19 && to_pt <= 2000.0f) u->ai_step = 1;
        break;
    }
    case 8: case 10:   /* backwards facing the target (0x100a8294 / 0x100a8791: the control's +0x59, full) */
        *turn = attack_turn;
        *throttle = u->ai_reverse ? -1.0f : 1.0f;
        break;
    case 21:   /* 0x100a9869: backwards at a quarter for 0xb5 ticks, then at full (+0x56 set: not traced) */
        *turn = attack_turn;
        *throttle = u->ai_step == 1 ? -0.25f : -1.0f;
        if (u->ai_step == 1 && u->ai_t1 == 0) u->ai_t1 = now + 0xb5;
        if (u->ai_step == 1 && now >= u->ai_t1) u->ai_step = 2;
        break;
    case 24: break;   /* stands and fires (0x100a7ba8) */
    case 25: *turn = attack_turn; break;   /* turns to the target where it stands (0x100a7bf3) */
    case 26:   /* 0x100a7c41: stands facing the target; over within 5000 or once the heading is off by more than 1 */
        if (fabsf(err) > 1.0f) *turn = attack_turn;
        if (flat <= 5000.0f || err > 1.0f) done = 1;
        break;
    case 27: break;   /* 0x100a7d28: stands (0x100ac0bb, the torso: not traced) */
    case 14: case 22: case 23: {
        /* 14 (0x100a8aa4): a point against the target within 28000 (0x100ab7f6; no points: failed), walked to; 22 /
         * 23 (0x100a8c28 / 0x100a8dd6): the radius out by 10000 each try, the point nearest the target (22) or
         * farthest (23) within it (0x100abba2), walked to, again from the search when the walk fails. Reached: the
         * outcome 1 and the point kept (+0x22a); failed: -1 and the points forgotten. It aims all the while
         * (0x1000d1ca, 0x10005300: the fire controller runs) */
        const unit *tu = t >= 0 ? &us->u[t] : NULL;
        int r = 0;
        if (u->ai_step == 1) {
            int k;
            if (u->ai_state == 14) {
                k = us->ncov < 1 ? -1 : cover_pick(us, u, tu, 28000.0f);
                if (k < 0) r = -1;
                else { u->ai_step = 2; u->cover = k; }
            } else {
                u->ai_r += 10000;
                k = cover_fold(us, u, tu, (float)u->ai_r, u->ai_state == 22 ? 1 : 2);
                if (k >= 0 && k < us->ncov) { u->ai_step = 2; u->cover = k; }
            }
        } else if (u->ai_step == 2) {
            int wr = cover_walk(us, w, i, u->cover, t, turn, throttle);
            if (wr == 1) u->ai_step = 3;
            else if (wr == 2 || u->ai_state != 14) u->ai_step = 1;   /* 14 walks on; 22 / 23 search again */
        } else if (u->ai_step == 3) r = 1;
        else r = -1;
        if (r == 1) { u->ai_out = 1; u->ai_got = u->cover; done = 1; }
        else if (r == -1) { u->ai_out = -1; u->ai_got = u->cover = -1; done = 1; }
        else u->ai_out = 0;
        if (*turn == 0 && *throttle == 0 && fabsf(err) > 0) *turn = steer(err);
        break;
    }
    case 16: {
        /* 0x100a8f84: crouched at the point (0x100ac059), turning to the target beyond 10 degrees (0x1000d679, the
         * throttle 1/640 to turn, 0x100a7b49); over (0x100a9020) when a shot of another side comes its way within
         * 50000 (0x100abf2d: next 22, the outcome 1) or it is engaged (0x100ac524: next 23, -1) */
        u->ai_crouch = 1;
        if (fabsf(err) > 10.0f) { *turn = steer(err); if (fabsf(err) > 5.0f) *throttle = 1.0f / 640.0f; }
        if (ai_incoming(us, us->cs, i)) { u->ai_next = 22; u->ai_out = 1; done = 1; }
        else if (ai_threat(us, w, i)) { u->ai_next = 23; u->ai_out = -1; done = 1; }
        break;
    }
    case 17: {
        /* 0x100a9095: engaged (0x100ac524) - stand, face the target, failed; else peeking (step 1): standing, it
         * straffes from the point (0x100ac5dd: the aim ray to the target clear, or within 10 degrees a straffe
         * 0x16a ticks to a random side, rand(2)) until it sees it, then fires (counting its shots); after 8 shots or
         * its time (+0x200) it hides (step 2: 0x43e ticks): back to the point (0x100abd9d), crouched, and after
         * 0x5a8 ticks peeks again */
        if (u->cover < 0) { u->ai_out = -1; done = 1; break; }
        if (ai_threat(us, w, i)) {
            u->ai_crouch = 0; u->ai_straffe = 0;
            *turn = steer(err);
            u->ai_out = -1; done = 1; break;
        }
        int sub = u->ai_step & 0xf, ph = u->ai_step & 0xf0;
        if (sub == 1) {
            u->ai_crouch = 0;
            if (ph == 0x10) {
                const unit *tu = &us->u[t];
                float p3[3] = {tu->s.x, tu->s.y, tu->s.z}, hd, pt, fl, dd;
                bearing_to(u, p3, &hd, &pt, &fl, &dd);
                int seen = aim_clear(us, w, i, t, pt);
                *turn = steer(err);
                u->ai_straffe = 0;
                if (!seen && fabsf(err) < 10.0f) {
                    int *tm = &u->ai_tm[1];
                    int lim, dir;
                    if (*tm < 0) { dir = -1; lim = -*tm; }
                    else if (*tm < 1) { *tm = now + 0x16a; lim = *tm; dir = units_rand(2) ? 1 : -1; if (dir < 0) *tm = -*tm; }
                    else { dir = 1; lim = *tm; }
                    u->ai_straffe = dir;
                    if (lim < now) { seen = 1; *tm = 0; u->ai_straffe = 0; }
                }
                if (seen) u->ai_step = (u->ai_step & ~0xf0) | 0x20;
            } else if (ph == 0x20) {
                *turn = steer(err);
                if (u->ai_fired) { u->ai_shots++; u->ai_fired = 0; }
            }
            if (u->ai_shots > 7 || u->ai_t1 <= now) {
                u->ai_shots = 0;
                u->ai_t1 = now + 0x43e;
                u->ai_step = 0x12;
                u->ai_straffe = 0;
            }
        } else if (sub == 2) {
            if (ph == 0x10) {
                if (cover_walk(us, w, i, u->cover, t, turn, throttle) == 1) u->ai_step = 0x22;
            } else if (ph == 0x20) {
                u->ai_crouch = 1;
                *throttle = 0;
            }
            if (u->ai_t1 <= now) {
                u->ai_shots = 0;
                u->ai_t1 = now + 0x5a8;
                u->ai_step = 0x11;
                u->ai_crouch = 0;
            }
        }
        break;
    }
    default: done = 1;   /* states the gears' and vehicles' tables don't reach (5, 6, 9, 11 .. 13, 15, 18) */
    }
    if (u->ai_state == 27 && now >= u->ai_t1) done = 1;
    /* 0x100a58c0's end: the state's time, a state forced next (+0x1f8), or gears' random interruption: rand(0x61) < 3
     * outside states 0, 0x10, 0x11, 0x18 .. 0x1b forces one of 0x10123cf0's. The original runs the AI once a frame
     * (0x1000a811: +0x198 against the clock), so the roll is once a frame: here on the first tick after
     * units_frame() */
    if (u->ai_end <= now) done = 1;
    if (u->ai_next >= 0) done = 1;
    else if (us->frame && u->gm.vclass_raw == 1 && units_rand(0x61) < 3 && u->ai_state != 0 && u->ai_state != 0x10 &&
             u->ai_state != 0x11 && (u->ai_state < 0x18 || u->ai_state > 0x1b)) {
        u->ai_next = ai_interrupt[units_rand(0x40) >> 4];
        done = 1;
    }
    return done;
}

/* a leader that's gone: the first live member in the group's order, not the old leader (0x1000b9d3) */
static void check_leaders(units *us)
{
    for (int g = 0; g < 16; g++) {
        int l = us->lead[g];
        if (l == -2) {
            for (int j = 0; j < us->n; j++) if (us->u[j].player) l = j;
            if (l < 0) continue;
        }
        if (l >= 0 && l < us->n && !us->u[l].cb.dead) continue;
        int best = -1;
        for (int j = 0; j < us->n; j++) {
            const unit *v = &us->u[j];
            int vg = v->player ? -1 : v->group;
            if (vg != g || j == l || v->cb.dead || !v->placed) continue;
            if (best < 0 || v->slot < us->u[best].slot) best = j;
        }
        if (best >= 0 || l >= 0) us->lead[g] = best;
    }
}

/* ==== Modes and their rules (traced: 0x1000a811, 0x1000a988, 0x1000ab37, 0x1000e7c9, 0x10010412, 0x1000f7d8) ====
 *
 * Every vehicle the sim drives has a mode (+0x1a2), a target (+0x1a6) and a point (+0x1a4), as codes: 0x200 | a
 * vehicle, 0x100 | a nav (here 0x1ff: the vehicle's own waypoint, 0x100af430's, radius 3000), 0x400 | a structure;
 * 0x2100 the group's reach objective's nav, 0x2101 the vehicle's waypoint, 0x2200 the player, 0x2201 the group's
 * leader, 0x2202 itself, 0x4200 the player's group's vehicles, 0x4201 every hostile vehicle (0x1000e1a8). Each tick
 * (0x1000a811, guarded by the sim clock) a vehicle out of mode 0xc first fights back (0x1000adb8), then tries its
 * active rules in order (0x1000a988): a rule's condition (0x10107dd0) on its two arguments (0x1000d171: 0x40 the
 * target, 0x2a the point, else as given) gives a code or 0; with one (or a pending request of the leader's matching)
 * the rule's action runs (0x10107df0: 0 / 1 go on, 1 clearing the stack; 2 pushes the mode; 3 pops it - going on
 * only when nothing was pushed; 4 asks the leader) and the rule sets its mode with its argument (0x25: the code
 * found). No rule taken, the mode's function moves it (0x10107d90). The rules are the sets of src/ai_rules.h: slot
 * 0 is set 1 for everyone (0x1000a223), slot 2 the current objective's (0x1000f7d8, 0x10010412, by the type's
 * lowest bit through the class's selector, the leader's column or the others'); a mode's active list is slot 2's
 * then slot 0's rules for it, each (condition, a1) once (0x1000ab37). A mode not in any slot's sets can't be set
 * (0x1000e750). */

static int ai_group(const units *us, const unit *v) { return v->player ? us->pgroup : v->group; }

static int ai_player(const units *us)
{
    for (int j = 0; j < us->n; j++) if (us->u[j].player) return j;
    return -1;
}

/* 0x10042b80 / 0x10042b07: a hostile vehicle - of another side, not the neutral side 2 */
static int ai_hostile(const unit *a, const unit *b) { return b->side != 2 && a->side != b->side; }

static int ai_unit_of(const units *us, int c)
{
    int i = c & 0xff;
    return (c & 0xf000) == 0 && (c & 0xf00) == 0x200 && i < us->n ? i : -1;
}

/* the vehicle has no usable weapon left (0x100aae2f: weapons, none loaded and working) */
static int ai_unarmed(const unit *u)
{
    if (!u->cb.gr.nweapons) return 0;
    for (int k = 0; k < u->cb.gr.nweapons; k++) if (u->cb.state[k] >= 0 && u->cb.ammo[k] != 0) return 0;
    return 1;
}

static int ai_pos(const units *us, const world *w, const unit *u, int c, float p[3])
{
    const mission *m = world_mission(w);
    if (c & 0xf000) return 0;
    switch (c & 0xf00) {
    case 0x200: {
        int i = ai_unit_of(us, c);
        if (i < 0) return 0;
        p[0] = us->u[i].s.x; p[1] = us->u[i].s.y; p[2] = us->u[i].s.z;
        return 1;
    }
    case 0x100:
        if ((c & 0xff) == 0xff) { p[0] = u->wp_x; p[1] = u->wp_y; p[2] = u->wp_z; return u->wp_r > 0; }
        if ((c & 0xff) >= m->nnav) return 0;
        p[0] = m->nav[c & 0xff].x; p[1] = m->nav[c & 0xff].y; p[2] = m->nav[c & 0xff].z;
        return 1;
    case 0x400:
        p[0] = us->spos[c & 63][0]; p[1] = us->spos[c & 63][1]; p[2] = us->spos[c & 63][2];
        return 1;
    }
    return 0;
}

/* 0x1000d1f1: a nav its radius (the vehicle's waypoint 3000), a vehicle 100, a structure 10000; below 100, 100 */
static float ai_radius(const world *w, const unit *u, int c)
{
    const mission *m = world_mission(w);
    float r = 100.0f;
    if ((c & 0xf00) == 0x100) r = (c & 0xff) == 0xff ? u->wp_r : (c & 0xff) < m->nnav ? m->nav[c & 0xff].radius : 0;
    else if ((c & 0xf00) == 0x400) r = 10000.0f;
    return r < 100.0f ? 100.0f : r;
}

static int ai_seen(const units *us, int g, int c)
{
    int k = (c & 0xf00) == 0x200 ? 256 + (c & 0xff) : (c & 0xf00) == 0x400 ? 448 + (c & 63) : c & 0xff;
    return (us->gseen[g & 15][k >> 3] >> (k & 7)) & 1;
}

static void ai_mark(units *us, int g, int c)
{
    int k = (c & 0xf00) == 0x200 ? 256 + (c & 0xff) : (c & 0xf00) == 0x400 ? 448 + (c & 63) : c & 0xff;
    us->gseen[g & 15][k >> 3] |= (unsigned char)(1 << (k & 7));
}

/* 0x1000e495: a target done for an objective type: 1 / 2 / 4 / 0x1000 a vehicle or structure destroyed (a nav
 * never), 8 / 0x100 one the group has reached (0x100b05db's 0x20 and group bit); special codes always */
static unsigned char ai_sdone[64];
static int ai_done(const units *us, int g, int c, unsigned type)
{
    if (c & 0xf000) return 1;
    int i = ai_unit_of(us, c);
    if ((c & 0xf00) == 0x200 && i < 0) return 1;
    if (type == 1 || type == 2 || type == 4 || type == 0x1000) {
        if ((c & 0xf00) == 0x200) return us->u[i].cb.dead;
        if ((c & 0xf00) == 0x400) return ai_sdone[c & 63];
        return 0;
    }
    if (type == 8 || type == 0x100) return ai_seen(us, g, c);
    return 0;
}

/* 0x1000d1ca -> 0x100b05db: the vehicle's point +0xec; the sim's own vehicles see any target that stands (0x100afe82:
 * not destroyed, not itself; the sensor limits are the player's alone): its distance (+0xd0). A vehicle (or
 * structure) within its standing height (a structure's radius) + 20000 is marked reached by the group; navs are
 * marked by the player only (objectives.c) */
static int ai_sight(units *us, const world *w, int i, int c, float *d)
{
    unit *u = &us->u[i];
    float p[3];
    if (!ai_pos(us, w, u, c, p)) return 0;
    int t = ai_unit_of(us, c);
    if (t >= 0 && (t == i || us->u[t].cb.dead || !us->u[t].placed)) return 0;
    float dx = p[0] - u->s.x, dy = p[1] - u->s.y, dz = p[2] - u->s.z;
    *d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (t >= 0 && *d < us->u[t].gm.stand + 20000.0f) ai_mark(us, ai_group(us, u), c);
    if ((c & 0xf00) == 0x400 && *d < 10000.0f + 20000.0f) ai_mark(us, ai_group(us, u), c);
    return 1;
}

/* 0x1000e1a8: the codes a code stands for, one at a time (prev -1 to start; -1 at the end) */
static int ai_next(const units *us, const units_goal *gl, const unit *u, int code, int prev)
{
    if (!(code & 0x6000)) return (code & 0xfff) == (prev & 0xfff) ? -1 : code;
    if (prev != -1) {
        if (code & 0x2000) return -1;
        prev &= 0xff;
    }
    int r = -1, g = ai_group(us, u), pl = ai_player(us);
    switch (code) {
    case 0x2100: r = gl ? gl->reach : -1; break;              /* 0x10069aac: the group's reach (0x20) objective's */
    case 0x2101: r = -1; break;                               /* the vehicle's waypoint +0x1d2 (none kept: 0x1000) */
    case 0x2200: r = pl; break;
    case 0x2201: r = units_leader(us, g); break;
    case 0x2202: r = (int)(u - us->u); break;
    case 0x4200:
        for (int j = prev + 1; j < us->n; j++)
            if (units_leader(us, ai_group(us, &us->u[j])) == pl && pl >= 0) { r = j; break; }
        break;
    case 0x4201:
        for (int j = prev + 1; j < us->n; j++)
            if (ai_hostile(u, &us->u[j])) { r = j; break; }
        break;
    }
    if (r < 0) return -1;
    if (code == 0x2100) return r;
    return (r & 0xf00) ? r : r | (code & 0xf00);
}

/* condition 1 (0x1000c427): the nearest of the code's targets not destroyed (type 2) and seen, nearer than a2 x 100
 * (-3: any); 0 if none */
static int ai_c427(units *us, const world *w, const units_goal *gl, int i, int code, int lim)
{
    unit *u = &us->u[i];
    int limit = lim == -3 ? 100000000 : lim * 100, best = -1, bd = limit;
    for (int c = ai_next(us, gl, u, code, -1); c != -1; c = ai_next(us, gl, u, code, c)) {
        float d;
        if (ai_done(us, ai_group(us, u), c, 2) || !ai_sight(us, w, i, c, &d) || (int)d >= bd) continue;
        bd = (int)d; best = c;
    }
    return best != -1 && bd < limit ? best : 0;
}

/* condition 2 (0x1000c502): the farthest seen beyond a2 (-3: 0, -4: +0x1ce, -2: +0x1ca, 0: its radius, else x 100) */
static int ai_c502(units *us, const world *w, const units_goal *gl, int i, int code, int lim)
{
    unit *u = &us->u[i];
    int limit = lim == -3 ? 0 : lim == -4 ? u->r_1ce : lim == -2 ? u->r_1ca : lim == 0 ? (int)ai_radius(w, u, code)
              : lim * 100, best = -1, bd = limit;
    for (int c = ai_next(us, gl, u, code, -1); c != -1; c = ai_next(us, gl, u, code, c)) {
        float d;
        if (ai_done(us, ai_group(us, u), c, 2) || !ai_sight(us, w, i, c, &d) || bd >= (int)d) continue;
        bd = (int)d; best = c;
    }
    return limit < bd && best != -1 ? best : 0;
}

/* 0x1000e963: mode 7's four waypoints about its target, at the target's radius (a vehicle: both standing heights +
 * 3000, at least +0x1ce; a structure +0x1ce) */
static void ai_patrol(units *us, const world *w, unit *u)
{
    float p[3];
    int t = ai_unit_of(us, u->mtgt);
    if (!ai_pos(us, w, u, u->mtgt, p)) return;
    float r = ai_radius(w, u, u->mtgt);
    if (t >= 0) { r = us->u[t].gm.stand + u->gm.stand + 3000.0f; if (r < (float)u->r_1ce) r = (float)u->r_1ce; }
    else if ((u->mtgt & 0xf00) == 0x400) r = (float)u->r_1ce;
    const float o[4][2] = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}};
    for (int k = 0; k < 4; k++) { u->pat[k][0] = p[0] + o[k][0] * r; u->pat[k][1] = p[1]; u->pat[k][2] = p[2] + o[k][1] * r; }
}

static void ai_wp(unit *u, float x, float y, float z)
{
    u->wp_x = x; u->wp_y = y; u->wp_z = z; u->wp_r = 3000.0f;
}

/* condition 3 (0x1000c1ee): with a2 0, the code reached (within its radius, to the hundred below); in mode 7 a
 * waypoint reached moves the vehicle on to the next of its four (about a vehicle, laid again about where it is now,
 * stepping +0x218 = 1, 2, 3, 0 ... of them) and gives 0; else as condition 1 */
static int ai_c1ee(units *us, const world *w, const units_goal *gl, int i, int code, int lim)
{
    unit *u = &us->u[i];
    if (lim) return ai_c427(us, w, gl, i, code, lim);
    int f = ai_c427(us, w, gl, i, code, (int)(ai_radius(w, u, code) / 100.0f));
    if (f && (code & 0x100) && u->mode == 7) {
        if ((u->mtgt & 0x200) && !(u->mtgt & 0x500)) {
            ai_patrol(us, w, u);
            u->pat_k = (u->pat_k + 1) & 3;
            for (int k = 0; k < u->pat_k; k++) u->pat_i = (u->pat_i + 1) & 3;
        } else u->pat_i = (u->pat_i + 1) & 3;
        ai_wp(u, u->pat[u->pat_i][0], u->pat[u->pat_i][1], u->pat[u->pat_i][2]);
        u->marg = 0x1ff;
        return 0;
    }
    return f;
}

/* 0x1000c764: over the code's targets seen within a2 x 100 (-3 any): the nearest (*any), and the nearest no other
 * member of the group is already on in mode 2 / 3 (*free; a vehicle someone is on stays with him unless the
 * group's objective is a defend, 4) */
static void ai_c764(units *us, const world *w, const units_goal *gl, int i, int code, int lim, int *any, int *fr)
{
    unit *u = &us->u[i];
    int g = ai_group(us, u);
    float bd = 1e8f;
    *any = *fr = 0;
    for (int c = ai_next(us, gl, u, code, -1); c != -1; c = ai_next(us, gl, u, code, c)) {
        float d;
        if (!ai_sight(us, w, i, c, &d)) continue;
        int better = 0;
        if ((int)d < (int)bd) { bd = d; *any = c; better = 1; if (!(c & 0x200)) *fr = c; }
        if (c & 0x200) {
            int taken = 0;
            for (int j = 0; j < us->n; j++) {
                const unit *v = &us->u[j];
                if (j == i || v->cb.dead || ai_group(us, v) != g || (v->mtgt & 0xf000) || v->mtgt != c) continue;
                if ((v->mode == 2 || v->mode == 3) && better && (!gl || gl->type != 4)) { taken = 1; break; }
            }
            if (better && !taken) *fr = c;
        }
    }
    if (*fr && !ai_c427(us, w, gl, i, *fr, lim)) *fr = 0;
    if (*any && !ai_c427(us, w, gl, i, *any, lim)) *any = 0;
}

/* condition 7 (0x1000c9d8): unless +0x1aa & 3, the nearest of the code's targets seen within a2 (-2 +0x1ca, -1
 * +0x1c6, 0 its radius, else x 100) */
static int ai_c9d8(units *us, const world *w, const units_goal *gl, int i, int code, int lim)
{
    unit *u = &us->u[i];
    if (u->f1aa & 3) return 0;
    if (lim == -2) lim = u->r_1ca / 100;
    else if (lim == -1) lim = u->r_1c6 / 100;
    else if (lim == 0) lim = (int)(ai_radius(w, u, code) / 100.0f);
    int any, fr;
    ai_c764(us, w, gl, i, code, lim, &any, &fr);
    return any;
}

static void ai_set_mode(units *us, const world *w, const units_goal *gl, int i, int mode, int arg, int push);

/* 0x1000ab37: the mode's active rules - slot 2's set, then 1's, then 0's, each (condition, a1) once */
static void ai_rules_build(unit *u)
{
    u->nrule = 0;
    for (int sl = 2; sl >= 0; sl--) {
        int k = u->rset[sl];
        if (k <= 0 || k > 12) continue;
        const ai_rule_set *rs = &ai_sets[k];
        int skip = 0;
        for (int p = 0; p < rs->n; p++) {
            if (rs->m[p].mode != u->mode) { skip += rs->m[p].n; continue; }
            for (int r = 0; r < rs->m[p].n; r++) {
                const ai_rule *x = &rs->m[p].r[r];
                int dup = 0;
                for (int q = 0; q < u->nrule; q++) {
                    const ai_rule *y = u->rule[q];
                    if (y->cond == x->cond && y->a1 == x->a1) dup = 1;
                }
                if (!dup && u->nrule < 24) u->rule[u->nrule++] = x;
            }
            break;
        }
        (void)skip;
    }
}

/* 0x1000e750: the mode is one of a slot's sets' */
static int ai_mode_known(const unit *u, int mode)
{
    for (int sl = 0; sl < 3; sl++) {
        int k = u->rset[sl];
        if (k <= 0 || k > 12) continue;
        for (int p = 0; p < ai_sets[k].n; p++) if (ai_sets[k].m[p].mode == mode) return 1;
    }
    return 0;
}

/* 0x1000fc1a: slot `sl` gets set `idx` of the class's selector, the leader's column or the others' (the
 * selectors lie one after another, 0x10107c10 .. 0x10107d8f, so an index past 15 reads the next class's) */
static void ai_slot(const units *us, unit *u, int sl, int idx, int leader)
{
    static const short *const tabs[6] = {&ai_sel_c10[0][0], &ai_sel_c50[0][0], &ai_sel_c90[0][0], &ai_sel_cd0[0][0],
                                         &ai_sel_d10[0][0], &ai_sel_d50[0][0]};
    if (idx < 0) { u->rset[sl] = 0; return; }
    int cls = u->gm.vclass_raw, t;
    switch (cls) {
    case 1: case 9: t = 0; break;
    case 3: t = 1; break;
    case 6: t = 2; break;
    case 2: t = 3; break;
    case 4: case 10: t = 4; break;
    case 5: case 8: t = 5; break;
    default: u->rset[sl] = 0; return;
    }
    int col = (int)(u - us->u) != leader, flat = t * 16 + idx;
    u->rset[sl] = flat < 96 ? tabs[flat / 16][(flat % 16) * 2 + col] : 0;
}

/* 0x1000d70f: a mode ending: mode 3 ends the combat state machine (0x100a7203), 5 / 7 / 0xd drop their waypoints */
static void ai_mode_end(unit *u)
{
    if (u->mode == 3) { u->ai_prev = u->ai_state; u->ai_state = -1; u->ai_reverse = 0; }
    if (u->mode == 5 || u->mode == 7 || u->mode == 0xd) u->wp_r = 0;
}

/* 0x1000e7c9: set a mode (one the slots' sets know): the stack cleared (push 0) or the mode pushed (0x1000cfcc: into
 * the stack's one place); mode 5 on the leader asked of the leader is mode 0; the target resolved (0x1000bc02:
 * special codes to the nearest seen); then the mode's start (0x1000d9de) and its rules (0x1000ab37) */
static void ai_set_mode(units *us, const world *w, const units_goal *gl, int i, int mode, int arg, int push)
{
    unit *u = &us->u[i];
    if (mode == -1 || !ai_mode_known(u, mode)) return;
    if (!push) u->stk_n = 0;
    else { u->stk_mode = u->mode; u->stk_tgt = u->mtgt; u->stk_n = 1; }
    ai_mode_end(u);
    if (mode == 5 && arg == 0x2201 && units_leader(us, ai_group(us, u)) == i) { mode = 0; arg = 0; }
    if (arg & 0xf000) {
        int any, fr;
        ai_c764(us, w, gl, i, arg, -3, &any, &fr);
        arg = any;
    }
    u->marg = u->mtgt = arg;
    /* 0x1000d9de: a vehicle powered down (modes 10 / 11's flag 0x10) starts its engine again (+0x4f0 = 0) */
    if (u->mode == 10 || u->mode == 11) u->start_at = us->ticks + 0x43e + units_rand(0x16a);
    if (mode != 3 && ai_group(us, u) == us->pgroup) u->f1aa &= 3;
    float p[3];
    switch (mode) {
    case 2: u->next_decide = us->ticks + units_rand(u->react > 0 ? u->react : 2) * 0x16; break;
    case 3: u->ai_state = u->ai_prev = u->ai_next = -1; u->ai_lastd = 0; break;
    case 4: u->f1aa = 1; break;
    case 7:   /* 0x1000e963: its four waypoints, the first its point */
        u->pat_i = 0;
        ai_patrol(us, w, u);
        ai_wp(u, u->pat[0][0], u->pat[0][1], u->pat[0][2]);
        u->marg = 0x1ff;
        break;
    case 0xb: u->f1aa = 1; break;
    case 0xf: {   /* the cover point against its target, within 30000 (0x100ab7f6) */
        cover_load(us, w);
        int t = ai_unit_of(us, u->mtgt);
        u->cover = cover_pick(us, u, t >= 0 ? &us->u[t] : NULL, 30000.0f);
        break;
    }
    case 0xd: {   /* the flank point: beside the target, square to the line from the leader, as far as the leader is */
        int t = ai_unit_of(us, u->mtgt), l = units_leader(us, ai_group(us, u));
        if (t >= 0 && l >= 0) {
            const unit *v = &us->u[t], *ld = &us->u[l];
            float dx = ld->s.x - v->s.x, dy = ld->s.y - v->s.y, dz = ld->s.z - v->s.z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz), sd = units_rand(2) == 0 ? -1.0f : 1.0f;
            float cx = dz * sd, cz = -dx * sd, cl = sqrtf(cx * cx + cz * cz);
            if (cl > 0) { cx /= cl; cz /= cl; }
            ai_wp(u, v->s.x + cx * dist, v->s.y, v->s.z + cz * dist);
            u->marg = 0x1ff;
        }
        break;
    }
    }
    (void)p;
    u->mode = mode;
    ai_rules_build(u);
}

/* 0x1000e6f5: set a mode, unless something is pushed (then it is lost) */
static void ai_set_mode_q(units *us, const world *w, const units_goal *gl, int i, int mode, int arg)
{
    if (us->u[i].stk_n == 0) ai_set_mode(us, w, gl, i, mode, arg, 0);
}

/* 0x1000adb8: fighting back - unless +0x1aa & 1, a hostile vehicle on this one in mode 3 (0x1000d0ba) that it isn't
 * already on: mode 2 on it, pushed, and the same for the group's members (not the player) not on it in mode 2 / 3 */
static void ai_fight_back(units *us, const world *w, const units_goal *gl, int i)
{
    unit *u = &us->u[i];
    if (u->f1aa & 1) return;
    int att = -1;
    for (int j = 0; j < us->n; j++) {
        const unit *v = &us->u[j];
        if (v->cb.dead || !v->placed || v->mtgt != (0x200 | i) || !ai_hostile(u, v) || v->mode != 3) continue;
        att = 0x200 | j;
        break;
    }
    if (att < 0) return;
    if ((u->mode == 2 || u->mode == 3) && u->mtgt == att) return;
    ai_set_mode(us, w, gl, i, 2, att, 1);
    int g = ai_group(us, u);
    for (int j = 0; j < us->n; j++) {
        unit *v = &us->u[j];
        if (j == i || v->player || v->cb.dead || !v->placed || ai_group(us, v) != g) continue;
        if (v->mode != 2 && v->mode != 3 && v->mtgt != att) ai_set_mode(us, w, gl, j, 2, att, 1);
    }
}

/* 0x1000a988: the rules; 1 when one set the mode */
static int ai_rules(units *us, const world *w, const units_goal *gl, int i)
{
    unit *u = &us->u[i];
    ai_fight_back(us, w, gl, i);
    for (int q = 0; q < u->nrule && q < 24; q++) {
        const ai_rule *r = u->rule[q];
        int a1 = r->a1 == 0x40 ? u->mtgt : r->a1 == 0x2a ? u->marg : r->a1 == 0x25 ? 0 : (unsigned short)r->a1;
        int a2 = r->a2;
        int forced = 0, found = 0;
        if (r->cond == u->req_cond && a1 == u->req_code && u->req_cond) { forced = 1; u->req_cond = 0; }
        switch (r->cond) {
        case 1: found = ai_c427(us, w, gl, i, a1, a2); break;
        case 2: found = ai_c502(us, w, gl, i, a1, a2); break;
        case 3: found = ai_c1ee(us, w, gl, i, a1, a2); break;
        case 4: found = a1; break;                                         /* 0x1000bc69 */
        case 5: found = a1 & 0xffff0000; break;                            /* 0x1000c1d0 */
        case 6:                                                            /* 0x1000cace: destroyed */
            found = ai_done(us, ai_group(us, u), a1, 2) ? a1 : 0;
            /* the group's leader the player (0x100429cf): the vehicle's slot says reply 4 */
            if (found && units_leader(us, ai_group(us, u)) >= 0 && us->u[units_leader(us, ai_group(us, u))].player &&
                us->nreply < 8) {
                us->reply[us->nreply].unit = i; us->reply[us->nreply].code = 4; us->nreply++;
            }
            break;
        case 7: found = ai_c9d8(us, w, gl, i, a1, a2); break;
        default: found = a1; break;
        }
        if (!found && !forced) continue;
        int go = 1;
        switch (r->act) {
        case 1: u->stk_n = 0; break;                                       /* 0x1000cfb0 */
        case 2: u->stk_mode = u->mode; u->stk_tgt = u->mtgt; u->stk_n = 1; break;   /* 0x1000cfcc */
        case 3:                                                            /* 0x1000d04a */
            if (u->stk_n) {
                u->stk_n = 0;
                ai_set_mode(us, w, gl, i, u->stk_mode, u->stk_tgt, 0);
                go = 0;
            }
            break;
        case 4: {                                                          /* 0x1000eda7 -> 0x1000ee91 */
            int l = units_leader(us, ai_group(us, u));
            if (l >= 0 && !us->u[l].req_cond) { us->u[l].req_cond = r->cond; us->u[l].req_code = found; }
            go = 0;
            break;
        }
        }
        if (!go) continue;
        int arg = r->arg == 0x40 ? u->mtgt : r->arg == 0x25 ? found : r->arg == 0x2a ? u->marg : (unsigned short)r->arg;
        ai_set_mode(us, w, gl, i, r->mode, arg, 0);
        return 1;
    }
    return 0;
}

/* the group's members the sim drives (0x1000fe66: not destroyed) */
static int ai_members(const units *us, int g, int *out)
{
    int n = 0;
    for (int j = 0; j < us->n && n < UNITS_MAX; j++) {
        const unit *v = &us->u[j];
        if (!v->cb.dead && v->placed && ai_group(us, v) == g) out[n++] = j;
    }
    return n;
}

static int ai_type_bit(unsigned type)
{
    int b = 0;
    while (b < 16 && !(type & (1u << b))) b++;
    return b;
}

static unsigned ai_flags_of(const units_goal *gl)
{
    int f = gl ? gl->aiflag : 0;
    return 0x40u | (f == 1 ? 2u : f == 2 ? 1u : 0u);   /* 0x1000fbf9 */
}

/* 0x1000f7d8: the group's objective changed: each member the sim drives takes the objective's rule set in slot 2;
 * then a wait (0x400) puts it in mode 10, 0x800 / 0x10 in 11, others in formation (5) on the leader (the leader
 * itself 0) - for a group the player leads only the first time (0x1012d4f8) */
static int ai_pled_done[16];
static void ai_objective(units *us, const world *w, const units_goal *gl, int g)
{
    int mem[UNITS_MAX], n = ai_members(us, g, mem), L = units_leader(us, g);
    if (L < 0 || !n) return;
    unsigned type = gl && gl->obj >= 0 ? gl->type : 0x200;
    int mode = type == 0x400 ? 10 : (type == 0x800 || type == 0x10) ? 11 : 5;
    int bit = ai_type_bit(type);
    for (int k = 0; k < n; k++) {
        unit *v = &us->u[mem[k]];
        if (v->player) continue;
        ai_slot(us, v, 2, bit + 1, L);
        if (us->u[L].player && ai_pled_done[g]) { ai_rules_build(v); continue; }
        if (mem[k] == L && mode == 5) ai_set_mode_q(us, w, gl, mem[k], 0, 0);
        else ai_set_mode_q(us, w, gl, mem[k], mode, mode == 5 ? 0x200 | L : 0);
        v->f1aa = (v->f1aa & ~3u) | ai_flags_of(gl);
        v->wp_r = 0;
    }
    if (us->u[L].player && type != 0x10) ai_pled_done[g] = 1;
}

/* 0x1000ff96: the leader asked to engage a target: on a defend objective (4) every member not in mode 2 / 3, armed,
 * with +0x1ae; else the first such member that isn't the leader with +0x1aa & 3 clear (the +0x1a8 they are compared
 * by stays 0), else the leader: mode 2 on it, pushed */
static void ai_engage(units *us, const world *w, const units_goal *gl, int g, int code)
{
    if (code & 0xf000) return;
    int mem[UNITS_MAX], n = ai_members(us, g, mem), L = units_leader(us, g);
    if (L < 0 || !n) return;
    if (gl && gl->type == 4) {
        for (int k = 0; k < n; k++) {
            unit *v = &us->u[mem[k]];
            if (v->player || v->mode == 2 || v->mode == 3 || ai_unarmed(v) || !v->f1ae) continue;
            ai_set_mode(us, w, gl, mem[k], 2, code, 1);
        }
        return;
    }
    int pick = -1;
    for (int k = 0; k < n && pick < 0; k++) {
        unit *v = &us->u[mem[k]];
        if (v->player || mem[k] == L || (v->f1aa & 3) || v->mode == 2 || v->mode == 3 || ai_unarmed(v) || !v->f1ae)
            continue;
        pick = mem[k];
    }
    if (pick < 0) {
        unit *v = &us->u[L];
        if (v->player || (v->f1aa & 3) || v->mode == 2 || v->mode == 3 || ai_unarmed(v) || !v->f1ae) return;
        pick = L;
    }
    ai_set_mode(us, w, gl, pick, 2, code, 1);
    us->u[pick].f1aa = (us->u[pick].f1aa | 0x20) & ~0x40u;
}

/* the goal's targets as codes */
static int ai_tgt_code(units *us, const units_goal *gl, int g, int k)
{
    int kind = gl->tgt[k].kind, idx = gl->tgt[k].idx;
    if (kind == 1) { if (gl->tgt[k].done) ai_mark(us, g, 0x100 | (idx & 0xff)); return 0x100 | (idx & 0xff); }
    if (kind == 2) return idx >= 0 && idx < us->n ? 0x200 | idx : 0x1000;
    if (kind == 4) {
        us->spos[idx & 63][0] = gl->tgt[k].x; us->spos[idx & 63][1] = gl->tgt[k].y; us->spos[idx & 63][2] = gl->tgt[k].z;
        ai_sdone[idx & 63] = (unsigned char)gl->tgt[k].done;
        return 0x400 | (idx & 63);
    }
    return 0x1000;
}

/* 0x10010412, each tick for each group: the leader's pending request first (0x1000ff32: 0 the objective again, 7
 * engage its code); then, for a group the sim leads with a current objective that isn't a wait (0xe10), its first
 * target not done that still takes vehicles (count +0x79, 0 all; the ones on it counted by mode: 4 in 7, 1 / 2 /
 * 0x4000 in 2 / 3, 8 in 2, 0x20 / 0x100 / 0x200 / 0x400 in 8, 0x2000 in 4, 0x1000 any) goes to the first member in
 * mode 0 (or 5, armed) that isn't the leader, else the leader in mode 0: slot 2's set, the mode by the type (4: 7;
 * 1 / 2 / 8 / 0x4000: 2; 0x20 / 0x100 / 0x200 / 0x400: 8; 0x2000: 4; 0x1000: 0) on it, +0x1aa | 0x40 and the flag */
static void ai_group_tick(units *us, const world *w, const units_goal *gl, int g)
{
    int L = units_leader(us, g);
    if (L < 0) return;
    unit *ld = &us->u[L];
    if (ld->req_cond) {
        int c = ld->req_cond, code = ld->req_code;
        ld->req_cond = 0;
        if (c == 7) ai_engage(us, w, gl, g, code);
    }
    if (ld->player || !gl || gl->obj < 0) return;
    unsigned type = gl->type;
    if (!type || (type & 0xe10)) return;
    int mem[UNITS_MAX], n = ai_members(us, g, mem);
    if (!n) return;
    int code = 0;
    for (int k = 0; k < gl->ntgt && !code; k++) {
        int c = ai_tgt_code(us, gl, g, k), left = gl->count ? gl->count : n;
        if (ai_done(us, g, c, type)) continue;
        for (int q = 0; q < n && left; q++) {
            const unit *v = &us->u[mem[q]];
            if (v->mtgt != c) continue;
            int md = v->mode;
            if ((type == 4 && md == 7) || ((type == 1 || type == 2 || type == 0x4000) && (md == 2 || md == 3)) ||
                (type == 8 && md == 2) || ((type == 0x20 || type == 0x100 || type == 0x200 || type == 0x400) && md == 8) ||
                (type == 0x2000 && md == 4) || type == 0x1000)
                left--;
        }
        if (left) code = c;
    }
    if (!code) return;
    int pick = -1;
    for (int q = 0; q < n && pick < 0; q++) {
        const unit *v = &us->u[mem[q]];
        if (mem[q] == L || v->player) continue;
        if (v->mode == 0 || (v->mode == 5 && !ai_unarmed(v))) pick = mem[q];
    }
    if (pick < 0) {
        if (ld->mode != 0 || ai_unarmed(ld)) return;
        pick = L;
    }
    ai_slot(us, &us->u[pick], 2, ai_type_bit(type) + 1, L);
    int mode = type == 4 ? 7 : (type == 1 || type == 2 || type == 8 || type == 0x4000) ? 2 :
               (type == 0x20 || type == 0x100 || type == 0x200 || type == 0x400) ? 8 : type == 0x2000 ? 4 :
               type == 0x1000 ? 0 : -1;
    if (mode < 0) return;
    ai_set_mode_q(us, w, gl, pick, mode, code);
    us->u[pick].f1aa |= ai_flags_of(gl);
}

/* 0x1000a223 / 0x1000a69d: a vehicle's start - set 1 in slot 0, mode 0xc (it waits for its group's objective), the
 * ranges from its GPS record (0 read as 250, x 100), +0x1ae from whether a human leads its group */
static void ai_start(units *us, const world *w, int i)
{
    unit *u = &us->u[i];
    const mission *m = world_mission(w);
    const mission_gps *gp = u->gps >= 0 && u->gps < m->ngps ? &m->gps[u->gps] : NULL;
    int a = gp ? gp->r_1ce : 0, b = gp ? gp->r_1c6 : 0, c = gp ? gp->r_1ca : 0;
    u->r_1ce = (a ? a : 250) * 100; u->r_1c6 = (b ? b : 250) * 100; u->r_1ca = (c ? c : 250) * 100;
    int L = units_leader(us, ai_group(us, u));
    u->f1ae = !(L >= 0 && us->u[L].player);
    u->rset[0] = 0; u->rset[1] = u->rset[2] = 0;
    ai_slot(us, u, 0, 0, L);
    u->mode = 0xc; u->mtgt = u->marg = 0; u->stk_n = 0; u->f1aa = 0;
    ai_rules_build(u);
    u->ai_init = 1;
}

/* a squad mate's order (0x1000f2b0 -> 0x1000eec9 -> 0x1000e7c9): attack 2 on the target, flank 0xd on it, defend 7
 * on it (not pushed), scout 0xe on the player's nav, cover 0xf, formation 5 on the leader (0x2201); the others
 * pushed. A mode in none of its sets (or mode 0xc) isn't taken. Engage / disengage (item 3) flip +0x1ae */
static void ai_order(units *us, const world *w, const units_goal *gl, int i)
{
    unit *u = &us->u[i];
    int t = u->order_target, tc = t >= 0 && t < us->n ? 0x200 | t : 0;
    if (u->mode == 0xc) return;
    switch (u->order) {
    case UNIT_ORDER_ATTACK: if (tc) ai_set_mode(us, w, gl, i, 2, tc, 1); break;
    case UNIT_ORDER_FLANK: if (tc) ai_set_mode(us, w, gl, i, 0xd, tc, 1); break;
    case UNIT_ORDER_DEFEND: if (tc) ai_set_mode(us, w, gl, i, 7, tc, 0); break;
    case UNIT_ORDER_SCOUT:
        ai_wp(u, u->order_x, u->s.y, u->order_z);
        u->wp_r = u->order_r >= 100.0f ? u->order_r : 100.0f;
        ai_set_mode(us, w, gl, i, 0xe, 0x1ff, 1);
        break;
    case UNIT_ORDER_COVER: ai_set_mode(us, w, gl, i, 0xf, tc, 1); break;
    case UNIT_ORDER_FORM: ai_set_mode(us, w, gl, i, 5, 0x2201, 1); break;
    case UNIT_ORDER_DISENGAGE: u->f1ae = 0; u->f1aa &= ~3u; break;
    case UNIT_ORDER_ENGAGE: u->f1ae = 1; u->f1aa &= ~3u; break;
    }
}

/* 0x1000ebfc / 0x100428c2: the vehicle's formation slot now (the group's formation's offset from the leader's slot,
 * turned with the leader) as its waypoint */
static void ai_form_point(units *us, const world *w, int i)
{
    unit *u = &us->u[i];
    const mission *m = world_mission(w);
    int g = ai_group(us, u), L = units_leader(us, g);
    if (L < 0 || L == i) return;
    const unit *ld = &us->u[L];
    const mission_form *f = m->nform ? &m->form[0] : NULL;
    for (int k = 0; k < m->nform; k++)
        if (same_ci(m->form[k].name, m->group_form[g])) f = &m->form[k];
    float ox = 0, oz = 0;
    if (f && u->slot < f->nslots && ld->slot < f->nslots) {
        ox = f->x[u->slot] - f->x[ld->slot]; oz = f->z[u->slot] - f->z[ld->slot];
    }
    float s = sinf(ld->s.heading * PI_F / 180), c = cosf(ld->s.heading * PI_F / 180);
    ai_wp(u, ld->s.x + ox * c + oz * s, ld->s.y, ld->s.z - ox * s + oz * c);
    u->marg = 0x1ff;
}

/* ==== The obstacle probe (traced: 0x100aa2ac and 0x100aa96d, 0x100aa018, 0x100aa0f3, 0x100aa8f3, 0x100aab39,
 * 0x100aabe0, 0x100aace3, 0x100a9f22, 0x10004948) ====
 * Four flat rays from the vehicle at its height, ahead and fanned to one side (0x10123860's points 0..3, mirrored for
 * a left turn: (16 - k) & 15), each probe_len x 5000 long, probe_len = |velocity| / 7.7 (classes 1 / 9: / 3, class 4:
 * / 2, class 10: x 1.1; at least 0.3; taken when not already avoiding). A ray is stopped by a scene object (0x100229d9)
 * or another vehicle's sphere (0x100aa018: its standing height, 0x10004948); with neither, the ground at its end is
 * steep (normal y below 0.766) or 2000 off the vehicle's (0x100aa0f3: then again at the vehicle's +0x242 reach). A
 * struck object is passed when its struck polygon is flat (normal y >= 0.766, the walk handler of its kind,
 * 0x10022876) or its flags & 0xf0 are 0x50 (0x100aa8f3). The first stop starts the avoidance: its side from the
 * object's bearing (0x100aab39) or the steep point's (0x100aabe0): turn right (+1) for one on the left. Each stopped ray
 * adds side x 819.2 / 4 to the turn; with ray 0 clear while avoiding, a ray from the other side's flank (the standing
 * height - 1 out, 0x100aa96d's side start) adds side x 409.6 if it is stopped; ray 0 clear and the heading within 1
 * degree of the wanted one ends the avoidance. The turn is held to +-819.2 (of 1024); the throttle full when ray 0
 * stopped, else a quarter, a tenth for steep ground, 0 skating (control +8: 1/64, 1/256, 1/640). Class 8 adds 1000 to
 * its height offset for every stopped ray and takes 200 off it (to 0) when nothing is in the way. Probes run every
 * 0x5a ticks (10 while avoiding an object or skating; class 10 every 0x14; 0x100a9f22); between them the vehicle
 * holds the controls they set while it avoids an object. */
static float ai_ground_y(const world *w, float x, float z, float *ny)
{
    int f;
    float n[3] = {0, 1, 0};
    float h = world_ground_height_n(w, x, z, &f, n);
    if (ny) *ny = f ? n[1] : 1.0f;
    return f ? h : 0.0f;
}

/* 0x100aa96d: probe k's ray (side: from the flank) */
static void probe_ray(const unit *u, int dir, int k, float len, int side, float p0[3], float p1[3])
{
    int idx = dir < 0 ? (16 - k) & 15 : k;
    float ax = ai_dirs[idx][0], az = ai_dirs[idx][1];
    float lx = ax != 0 ? len / ax : 0, lz = az != 0 ? len / az : 0;
    float s = sinf(u->s.heading * PI_F / 180), c = cosf(u->s.heading * PI_F / 180);
    p0[0] = u->s.x; p0[1] = u->s.y; p0[2] = u->s.z;
    if (side) {
        float ox = dir < 0 ? -u->gm.stand + 1.0f : u->gm.stand - 1.0f;
        p0[0] = u->s.x + ox * c; p0[2] = u->s.z - ox * s;
    }
    p1[0] = u->s.x + lx * c + lz * s; p1[1] = u->s.y; p1[2] = u->s.z - lx * s + lz * c;
}

/* 0x10004948: the distance along the ray p0 -> p1 to the sphere (c, r), 3.4e38 when missed */
static float ray_sphere(const float p0[3], const float p1[3], const float c[3], float r)
{
    if (r < 1e-7f) return 3.4e38f;
    float d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float px = c[0] - p0[0], py = c[1] - p0[1], pz = c[2] - p0[2];
    float f = px * px + py * py + pz * pz - r * r;
    if (f < 0) return 10.0f;
    if (len <= 0) return 3.4e38f;
    float t = (px * d[0] + py * d[1] + pz * d[2]) / len;
    if (t < 0 || t * t - f < 0) return 3.4e38f;
    t -= r;
    if (t < 0) return 0.0f;
    return t >= len ? 3.4e38f : t;
}

/* 0x1000a14f: a vehicle's reach (+0x242): its standing height x 1.5 (class 3), 2 (5, 8), 1.2 (10), else 3 */
static float unit_reach(const unit *v)
{
    int c = v->gm.vclass_raw;
    float k = c == 3 ? 1.5f : c == 5 || c == 8 ? 2.0f : c == 10 ? 1.2f : 3.0f;
    return v->gm.stand * k;
}

/* the ray's stop: 1 an object or a vehicle (*obj: instance, or -2 - unit; *pass its passability), 0 none */
static int probe_hit(const units *us, world *w, int i, const float p0[3], const float p1[3], int reach, int *obj,
                     int *pass)
{
    float t = 2, ny = 1;
    int o = world_ray_n(w, p0, p1, &t, &ny);
    if (o >= 0) {
        *obj = o;
        /* 0x100aa8f3: passed when its kind has a walk handler (0x10022876: 0x1010e508's kinds 0, 5, 8) and the
         * struck polygon is flat (|normal y| >= 0.766), or its flags & 0xf0 are 0x50 */
        int kd = world_object_kind(w, o);
        int walk = kd == 0 || kd == 5 || kd == 8;
        *pass = (walk && fabsf(ny) >= 0.766f) || (world_object_flags(w, o) & 0xf0) == 0x50;
        return 1;
    }
    float d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int k = 0; k < us->n; k++) {
        const unit *v = &us->u[k];
        if (k == i || !v->placed) continue;
        float c[3] = {v->s.x, v->s.y, v->s.z};
        if (ray_sphere(p0, p1, c, reach ? unit_reach(v) : v->gm.stand) < len) {
            *obj = -2 - k;
            *pass = 0;   /* a vehicle's root has no walk handler */
            return 1;
        }
    }
    return 0;
}

/* 0x100aa0f3: the ground at the ray's end (then at the vehicle's reach along it) steep or 2000 off the vehicle's */
static int probe_steep(const unit *u, const world *w, const float p0[3], const float p1[3], float gh, float pt[3])
{
    float ny;
    float h = ai_ground_y(w, p1[0], p1[2], &ny);
    if (ny < 0.766f || fabsf(h - gh) > 2000.0f) { pt[0] = p1[0]; pt[1] = p1[1]; pt[2] = p1[2]; return 1; }
    float d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len <= 0) return 0;
    float r = unit_reach(u), q[3] = {p0[0] + d[0] / len * r, p0[1] + d[1] / len * r, p0[2] + d[2] / len * r};
    h = ai_ground_y(w, q[0], q[2], &ny);
    if (ny < 0.766f || fabsf(h - gh) > 2000.0f) { pt[0] = q[0]; pt[1] = q[1]; pt[2] = q[2]; return 1; }
    return 0;
}

/* 0x100aab39 / 0x100aabe0: +1 (turn right) for a point on the vehicle's left, else -1 */
static int probe_side(const unit *u, float x, float z)
{
    float b = wrap180(atan2f(x - u->s.x, z - u->s.z) * 180.0f / PI_F - u->s.heading);
    return b < 0 ? 1 : -1;
}

static void probe_obj_pos(const units *us, const world *w, int obj, float *x, float *z)
{
    if (obj <= -2) { *x = us->u[-2 - obj].s.x; *z = us->u[-2 - obj].s.z; return; }
    float y, s, c;
    if (world_object_pose(w, obj, x, &y, z, &s, &c) < 0) { *x = 0; *z = 0; }
}

/* returns 1 when the probe steers (*turn, *thr set), want: the heading the mode wants */
static int ai_probe(units *us, world *w, int i, float want, float *turn, float *thr)
{
    unit *u = &us->u[i];
    int cls = u->gm.vclass_raw, air = cls == 5 || cls == 8;
    if (u->s.top_speed == 0 || (u->av_dir == 0 && u->thr_ctl == 0) || u->ai_reverse) return 0;
    if (us->ticks < u->av_next) {
        if (u->av_obj) { *turn = u->av_turn; *thr = u->av_thr; }
        return u->av_obj != 0;
    }
    if (u->av_dir == 0) {
        float v = sqrtf(u->s.vx * u->s.vx + u->s.vy * u->s.vy + u->s.vz * u->s.vz) / 7.7f;
        if (cls == 1 || cls == 9) v /= 3.0f;
        else if (cls == 4) v /= 2.0f;
        else if (cls == 10) v *= 1.1f;
        u->av_len = v < 0.3f ? 0.3f : v;
    }
    float gh = ai_ground_y(w, u->s.x, u->s.z, NULL);
    float acc = 0, p0[3], p1[3], pt[3];
    int k, steep = 0, obj = 0, pass = 1, have = 0;
    for (k = 0; k < 4; k++) {
        probe_ray(u, u->av_dir, k, u->av_len * 5000.0f, 0, p0, p1);
        int hit = probe_hit(us, w, i, p0, p1, 0, &obj, &pass);
        if (hit) have = 1;
        if (!hit) {
            steep = !air && probe_steep(u, w, p0, p1, gh, pt);
            if (!steep) {
                if (k == 0 && fabsf(wrap180(want - u->s.heading)) <= 1.0f) { u->av_obj = 0; u->av_dir = 0; }
                if (k != 0 || u->av_dir == 0) break;
                probe_ray(u, -u->av_dir, 1, u->av_len * 5000.0f, 1, p0, p1);
                int h2 = probe_hit(us, w, i, p0, p1, 1, &obj, &pass);
                if (h2) have = 1;
                if (!h2) {
                    steep = !air && probe_steep(u, w, p0, p1, gh, pt);
                    if (!steep) break;
                }
                if ((h2 && !pass) || (!h2 && steep)) acc += (float)u->av_dir * 409.6f;
                break;
            }
            /* steep ground: an obstacle at its point */
            have = 1; pass = 0;
            u->av_pt[0] = pt[0]; u->av_pt[1] = pt[1]; u->av_pt[2] = pt[2];
        }
        if (!have || pass) break;
        if (cls == 8) u->alt_off += 1000.0f;
        if (u->av_dir == 0) {
            if (!steep) {
                float ox, oz;
                probe_obj_pos(us, w, obj, &ox, &oz);
                u->av_dir = probe_side(u, ox, oz);
                u->av_obj = 1;
            } else {
                u->av_dir = probe_side(u, u->av_pt[0], u->av_pt[2]);
            }
        }
        acc += (float)u->av_dir * 819.2f / 4.0f;
    }
    int on = acc != 0;
    if (!on) {
        if (cls == 8) {
            if (u->alt_off > 1e-7f) u->alt_off -= 200.0f;
            if (u->alt_off < 0) u->alt_off = 0;
        }
    } else {
        if (acc > 819.2f) acc = 819.2f;
        if (acc < -819.2f) acc = -819.2f;
        *turn = acc / 1024.0f;
        float th = k == 0 ? 1.0f : 0.25f;
        if (steep) th = 0.1f;
        if (u->s.skating) th = 0;
        if (cls == 6) { th = 0; *turn = 0; }
        *thr = th;
        u->av_turn = *turn; u->av_thr = *thr;
    }
    /* 0x100a9f22: the next probe */
    int skating = cls == 1 && u->s.skating;
    u->av_next = us->ticks + (cls == 10 ? 0x14 : skating ? 10 : u->av_obj ? 10 : 0x5a);
    return on;
}

/* ==== The walk to cover (traced: 0x1000d9de's mode 0xf start -> 0x100ab7f6, 0x100279a3, 0x100ab6cd, 0x1002715e,
 * 0x10027865; 0x1000cb8f's mode 0xf -> 0x100abd9d, 0x100abd60, 0x100abc59, 0x1002736d, 0x10027c01) ====
 * The mode's start picks a cover point against its target (the threat): the free points within 30000 of the vehicle,
 * nearest first; the first with the vehicle nearer to it than to the threat, the threat nearer the vehicle than to it
 * too and on the far side (0x1002715e: (vehicle - point) . (threat - point) < 0) - none such: a random one of them; no
 * point within 30000: the nearest free point of all. Each tick: a point taken by another (+0x10, +0x14) or gone sends
 * the vehicle to formation (mode 5 on its leader, 0x2201); else the spot behind the point's box from the threat
 * (0x1002736d: a flat ray from the point mirrored through the threat, at the point's height, into the box, backed off
 * by the vehicle's standing height, on the ground) becomes its waypoint; it steers there (the probe first, else
 * 0x1000d679) with the throttle by 0x1000d2bd against 1000, and stops within 1000. */
static void cover_load(units *us, const world *w)
{
    if (us->cov_loaded) return;
    us->cov_loaded = 1;
    static float pos[200][3];
    us->ncov = world_cover_points(w, pos, NULL, 200);
    int nb = world_cover_boxes(w, us->cov_box, 200);
    if (nb < us->ncov) us->ncov = nb;
    for (int k = 0; k < us->ncov; k++) {
        memcpy(us->cov_pos[k], pos[k], sizeof pos[k]);
        us->cov_taken[k] = 0; us->cov_by[k] = -1;
    }
}

static float cov_d2(const float p[3], float x, float y, float z)
{
    float dx = p[0] - x, dy = p[1] - y, dz = p[2] - z;
    return dx * dx + dy * dy + dz * dz;
}

/* 0x100ab7f6 */
static int cover_pick(units *us, const unit *u, const unit *t, float range)
{
    static int idx[200];
    static float dd[200];
    int n = 0;
    for (int k = 0; k < us->ncov; k++) {
        if (us->cov_taken[k]) continue;
        float d = cov_d2(us->cov_pos[k], u->s.x, u->s.y, u->s.z);
        if (d < range * range) { idx[n] = k; dd[n] = d; n++; }
    }
    if (n == 0 || !t) {   /* 0x10027865: the nearest free point */
        int best = -1;
        float bd = 3.4e38f;
        for (int k = 0; k < us->ncov; k++) {
            if (us->cov_taken[k]) continue;
            float d = cov_d2(us->cov_pos[k], u->s.x, u->s.y, u->s.z);
            if (d < bd) { bd = d; best = k; }
        }
        return best;
    }
    for (int a = 1; a < n; a++)   /* 0x1002795f: by distance */
        for (int b = a; b > 0 && dd[b] < dd[b - 1]; b--) {
            float tf = dd[b]; dd[b] = dd[b - 1]; dd[b - 1] = tf;
            int ti = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = ti;
        }
    for (int a = 0; a < n; a++) {   /* 0x100ab6cd */
        const float *p = us->cov_pos[idx[a]];
        float du = cov_d2(p, u->s.x, u->s.y, u->s.z), dt = cov_d2(p, t->s.x, t->s.y, t->s.z);
        float ux = u->s.x - t->s.x, uy = u->s.y - t->s.y, uz = u->s.z - t->s.z;
        float dot = (u->s.x - p[0]) * (t->s.x - p[0]) + (u->s.y - p[1]) * (t->s.y - p[1]) + (u->s.z - p[2]) * (t->s.z - p[2]);
        if (ux * ux + uy * uy + uz * uz > du && dt > du && dot < 0) return idx[a];
    }
    return idx[units_rand(n)];
}

/* 0x1002736d / 0x10027c01: the spot behind point k's box from the threat at tp */
static int cover_spot(const units *us, const world *w, int k, const float tp[3], float stand, float out[3])
{
    const float *p = us->cov_pos[k], *b = us->cov_box[k];
    float s[3] = {2 * p[0] - tp[0], p[1], 2 * p[2] - tp[2]}, e[3] = {tp[0], p[1], tp[2]};
    float d[3] = {e[0] - s[0], 0, e[2] - s[2]}, len = sqrtf(d[0] * d[0] + d[2] * d[2]);
    if (len <= 0) return 0;
    d[0] /= len; d[2] /= len;
    float tin = -3.4e38f, tout = 3.4e38f;
    for (int a = 0; a < 3; a++) {
        if (fabsf(d[a]) < 1e-7f) {
            if (s[a] < b[2 * a] || s[a] > b[2 * a + 1]) return 0;
            continue;
        }
        float t0 = (b[2 * a] - s[a]) / d[a], t1 = (b[2 * a + 1] - s[a]) / d[a];
        if (t0 > t1) { float tt = t0; t0 = t1; t1 = tt; }
        if (t0 > tin) tin = t0;
        if (t1 < tout) tout = t1;
    }
    if (tout > len) tout = len;
    if (tin > tout) return 0;
    float t = tin - stand;
    out[0] = s[0] + d[0] * t; out[2] = s[2] + d[2] * t;
    out[1] = ai_ground_y(w, out[0], out[2], NULL);
    return 1;
}

/* 0x100279a3's count: the free points within range of the vehicle */
static int cover_near(const units *us, const unit *u, float range)
{
    int n = 0;
    for (int k = 0; k < us->ncov; k++)
        if (!us->cov_taken[k] && cov_d2(us->cov_pos[k], u->s.x, u->s.y, u->s.z) < range * range) n++;
    return n;
}

/* 0x100abba2 -> 0x100ab93f: of the free points within range (0x100279a3, nearest first), kind 1 the nearest to the
 * threat (0x100aba26), kind 2 the farthest (0x100abae4); none within range: the nearest free point (0x10027865) */
static int cover_fold(units *us, const unit *u, const unit *t, float range, int kind)
{
    if (!t) return -1;
    int best = -1;
    float bd = 0;
    for (int k = 0; k < us->ncov; k++) {
        if (us->cov_taken[k]) continue;
        if (cov_d2(us->cov_pos[k], u->s.x, u->s.y, u->s.z) >= range * range) continue;
        float d = cov_d2(us->cov_pos[k], t->s.x, t->s.y, t->s.z);
        if (best < 0 || (kind == 1 ? d < bd : d > bd)) { best = k; bd = d; }
    }
    if (best >= 0) return best;
    return cover_pick(us, u, NULL, 0.0f);
}

/* 0x100abd9d: the walk to point k against threat t: 2 when the point is another's (0x100abd60), 1 there (within
 * 1000, the throttle cut), else 0; the spot behind its box (0x100abc59 -> 0x1002736d), the probe, else the turn
 * (0x1000d679) and the throttle against 1000 (0x1000d2bd) */
static int cover_walk(units *us, world *w, int i, int k, int t, float *turn, float *thr)
{
    unit *u = &us->u[i];
    if (k < 0 || k >= us->ncov) return 2;
    if (us->cov_taken[k] && us->cov_by[k] != i) return 2;
    float tp[3] = {t >= 0 ? us->u[t].s.x : u->s.x, t >= 0 ? us->u[t].s.y : u->s.y, t >= 0 ? us->u[t].s.z : u->s.z};
    float sp[3];
    if (t >= 0 && cover_spot(us, w, k, tp, u->gm.stand, sp)) ai_wp(u, sp[0], sp[1], sp[2]);
    else ai_wp(u, us->cov_pos[k][0], us->cov_pos[k][1], us->cov_pos[k][2]);
    float dx = u->wp_x - u->s.x, dz = u->wp_z - u->s.z, d = sqrtf(dx * dx + dz * dz);
    float hd = atan2f(dx, dz) * 180.0f / PI_F;
    if (!ai_probe(us, w, i, hd, turn, thr)) {
        *turn = steer(wrap180(hd - u->s.heading));
        u->turn_ctl = *turn;
        *thr = approach(u, d, 1000.0f);
    }
    if (d <= 1000.0f) { *thr = 0; return 1; }
    return 0;
}

/* 0x100ac524: the vehicle engaged - the player (0x100ac328: another side, within 15000, and 0x1000510e: the
 * player's view ray (heading + twist, its pitch) striking it first, or striking nothing while the player has it
 * targeted), else a vehicle that points at it (0x100ac1e0 over the +0x208 list: another side, its target this one,
 * within 15000 and its aim ray finding it, 0x10004d60) */
static int ai_threat(units *us, world *w, int i)
{
    const unit *u = &us->u[i];
    int p = ai_player(us);
    if (p >= 0 && us->u[p].side != u->side && !us->u[p].cb.dead) {
        const unit *pu = &us->u[p];
        float dx = pu->s.x - u->s.x, dy = pu->s.y - u->s.y, dz = pu->s.z - u->s.z;
        if (dx * dx + dy * dy + dz * dz <= 15000.0f * 15000.0f) {
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            int r = aim_ray(us, w, p, sim_gear_view_yaw(&pu->s), pu->aim_pitch, d + 1000.0f);
            if (r == i || (r == -1 && pu->target == i)) return 1;
        }
    }
    for (int k = 0; k < us->n; k++) {
        const unit *a = &us->u[k];
        if (k == i || a->player || a->cb.dead || !a->placed || a->side == u->side || a->target != i) continue;
        float dx = a->s.x - u->s.x, dy = a->s.y - u->s.y, dz = a->s.z - u->s.z;
        if (dx * dx + dy * dy + dz * dz > 15000.0f * 15000.0f) continue;
        float p3[3] = {u->s.x, u->s.y, u->s.z}, hd, pt, fl, dd;
        bearing_to(a, p3, &hd, &pt, &fl, &dd);
        if (aim_clear(us, w, k, i, pt)) return 1;
    }
    return 0;
}

/* 0x100abf2d: a shot of another side's in flight within 50000 coming its way (its velocity . (vehicle - shot) > 0) */
static int ai_incoming(const units *us, const combat_shots *s, int i)
{
    const unit *u = &us->u[i];
    if (!s) return 0;
    for (int k = 0; k < COMBAT_MAX_SHOTS; k++) {
        const combat_shot *p = &s->shot[k];
        if (!p->active || p->owner < 0 || p->owner >= us->n || us->u[p->owner].side == u->side) continue;
        float dx = u->s.x - p->x, dy = u->s.y - p->y, dz = u->s.z - p->z;
        if (dx * dx + dy * dy + dz * dz > 50000.0f * 50000.0f) continue;
        if (p->vx * dx + p->vy * dy + p->vz * dz > 0) return 1;
    }
    return 0;
}

/* ==== Aircraft heights (traced: 0x10010e31 / 0x10010d9c from the modes' functions 0x1000cb29 / 0x1000cb8f /
 * 0x1000cf4d, and 0x1005d75c's flight branch for classes 5 / 8, 0x10066eeb) ====
 * The mode's function (table 0x10107d90: 0 and 0xc 0x1000cb29, 3 0x1000cf4d, the rest 0x1000cb8f) sets the height
 * wanted over the ground (+0x23a) each tick: 4000 in mode 0 and the 0x1000cb8f modes but 10 / 11, 2000 in mode 3
 * (combat), the landed height (+0x588) in modes 0xc, 10 and 11. The flight branch: the ground's change under it
 * since the last tick, d, adds -d to the offset (+0x23e) when the ground drops more than 1000, or when it rises more
 * than 1000 and lies below the vehicle's base (+0x58 - +0x520) - 200; the height's target (+0x68) = ground + height +
 * offset, the height lagging toward it (0.6 s, 0x1005cf76's lag at +0x64). */
static void ai_altitude(units *us, world *w, unit *u)
{
    int cls = u->gm.vclass_raw;
    if (cls != 5 && cls != 8) return;
    int md = u->mode;
    if (md == 3) u->alt = 2000.0f;
    else if (md == 0xc || md == 10 || md == 11) u->alt = u->alt_land;
    else u->alt = 4000.0f;
    float gh = ai_ground_y(w, u->s.x, u->s.z, NULL);
    if (!u->alt_init) { u->alt_init = 1; u->alt_gprev = gh; u->alt_cur = u->s.y; }
    float d = gh - u->alt_gprev;
    if (d + 1000.0f < 0) u->alt_off -= d;
    else if (d - 1000.0f > 1e-7f && gh < (u->s.y - u->alt_land) - 200.0f) u->alt_off -= d;
    u->alt_gprev = gh;
    float tgt = gh + u->alt + u->alt_off, dd = tgt - u->alt_cur, T = 0.6f * 181.0f;
    if (fabsf(dd) < 1e-7f) u->alt_cur = tgt;
    else {
        float step = dd / T;
        if ((dd < 0 && step <= dd) || (dd > 1e-7f && dd <= step)) u->alt_cur = tgt;
        else u->alt_cur += step;
    }
    u->s.y = u->alt_cur;
    u->s.vy = 0;
    (void)us;
}

/* ==== The torso (traced: 0x10010aa3, the weave; 0x10005570 / 0x10005a87 -> 0x1000d61d -> 0x1000f705, the fire
 * controllers' twist) ====
 * The weave (gears in modes 5 .. 8): every 0x20 ticks while the throttle control is not 0 (else 0x40) the twist control
 * (+4) steps 5 degrees to its side, the side flipping once it passes 45. The fire controllers aim the torso: the twist
 * control = f(heading error / 11520) x 11520, f stepping its argument by 2k / 65536 toward the error's sign and
 * holding it to +-1/64 (0x1000f705; k their aim flag) - the heading error held to +-180, divided by 1.5 for gears and
 * infantry. */
static void ai_weave(units *us, unit *u)
{
    if (u->gm.vclass_raw != 1) return;
    int step = fabsf(u->thr_ctl) >= 1e-7f ? 0x20 : 0x40;
    if (us->ticks + step < u->weave_next) u->weave_next = us->ticks + step;
    if (u->weave_next <= us->ticks) {
        u->pan_ctl += u->weave_side ? 5.0f : -5.0f;
        if (fabsf(u->pan_ctl) > 45.0f) u->weave_side = !u->weave_side;
        u->weave_next = us->ticks + step;
    }
}

/* the tilt (0x10005a87 / 0x10005570 -> 0x1000d64b -> 0x1000f705): control +0 = -(x * 3840), x = the pitch to the aim
 * point (+0xe8) / 3840 stepped by the decision roll (k x 2 / 65536 toward its sign), held to +-1/64 (+-60 degrees) */
static float ai_tilt(float pitch, int k)
{
    float x = pitch / 3840.0f;
    int st = k * 2;
    int ss = st == 0 ? 0 : st > 0 ? 1 : -1, xs = fabsf(x) >= 1e-7f ? (x < 0 ? -1 : 1) : 0;
    if (ss != xs) st = -st;
    x += (float)st / 65536.0f;
    if (x > 1.0f / 64) x = 1.0f / 64;
    if (x < -1.0f / 64) x = -1.0f / 64;
    return -x * 3840.0f;
}

static float ai_twist(const unit *u, float err, int k)
{
    float x = err / 11520.0f;
    int st = k * 2;
    int ss = st == 0 ? 0 : st > 0 ? 1 : -1, xs = fabsf(x) >= 1e-7f ? (x < 0 ? -1 : 1) : 0;
    if (ss != xs) st = -st;
    x += (float)st / 65536.0f;
    if (x > 1.0f / 64) x = 1.0f / 64;
    if (x < -1.0f / 64) x = -1.0f / 64;
    float tw = x * 11520.0f;
    if (u->gm.vclass_raw == 1 || u->gm.vclass_raw == 9) tw /= 1.5f;
    return tw;
}

void units_frame(units *us)
{
    us->frame_pending = 1;
}

void units_ai_tick(units *us, world *w, const units_goal goal[16], combat_shots *s)
{
    us->ticks++;
    us->cs = s;
    cover_load(us, w);   /* the cover points (0x100275db, at load) */
    us->frame = us->frame_pending;
    us->frame_pending = 0;
    check_leaders(us);
    /* the vehicles' start (0x1000a223, at load) and each group's objective when it changes (0x1006932f ->
     * 0x1000f7c5 -> 0x1000f7d8; the start's, 0x1000a100) */
    for (int i = 0; i < us->n; i++)
        if (!us->u[i].player && !us->u[i].ai_init && us->u[i].placed) ai_start(us, w, i);
    for (int g = 0; g < 16; g++) {
        int ob = goal ? goal[g].obj : -1;
        if (ob != us->gobj[g]) { us->gobj[g] = ob; ai_objective(us, w, goal ? &goal[g] : NULL, g); }
    }
    for (int i = 0; i < us->n; i++) {
        unit *u = &us->u[i];
        if (u->player || u->remote || u->cb.dead || !u->placed) continue;
        combatant *c = &u->cb;
        /* gears: the design's speed points x 47.1744 (0x1004e6a0, +0x4d4); version 1 vehicles: v1_top_speed */
        if (u->s.top_speed <= 0 && c->gr.speed_points > 0) u->s.top_speed = (float)c->gr.speed_points * 47.1744f;
        int sk = u->skill >= 0 && u->skill < 9 ? u->skill : 5;
        int g = ai_group(us, u);
        const units_goal *gl = goal ? &goal[g] : NULL;
        /* 0x1000a811 (each tick, the sim clock guarding it): out of mode 0xc, the leader looks after its group
         * (0x10010412), then the rules (0x1000a988), else the mode's function (0x10107d90) */
        int ruled = 0;
        if (u->mode != 0xc) {
            if (u->order_n != u->order_done_n || u->order != u->order_last || u->order_target != u->order_last_tgt ||
                u->order_x != u->order_last_x) {
                u->order_done_n = u->order_n; u->order_last = u->order; u->order_last_tgt = u->order_target;
                u->order_last_x = u->order_x;
                if (u->order != UNIT_ORDER_NONE) ai_order(us, w, gl, i);
            }
            if (units_leader(us, g) == i) ai_group_tick(us, w, gl, g);
            ruled = ai_rules(us, w, gl, i);
        }
        /* a vehicle stands still until its engine runs (0x10060491 zeroes the speeds below state 2) */
        if (us->ticks < u->start_at) continue;
        /* the target: mode 2 / 3's vehicle */
        u->target = (u->mode == 2 || u->mode == 3) && c->gr.nweapons ? ai_unit_of(us, u->mtgt) : -1;
        if (u->target >= 0 && us->u[u->target].cb.dead) u->target = -1;
        if (u->mode != 3 && u->ai_state >= 0) {
            ai_leave(us, u); u->ai_prev = u->ai_next = -1; u->ai_crouch = u->ai_straffe = 0;
            u->s.ai_tilt = 0;   /* 0x1000d70f: mode 3's end zeroes the control's +0 and +0xc */
        }
        float tx = 0, ty = 0, tz = 0;
        if (u->target >= 0) { tx = us->u[u->target].s.x; ty = us->u[u->target].s.y; tz = us->u[u->target].s.z; }
        int mode2 = u->mode == 2;
        float turn = 0, throttle = 0;
        int trig = 0, saim = 0;
        float aim[3] = {0, 0, 0}, eye[3];
        unit_eye(u, eye);
        if (ruled) {
            /* a rule set the mode this tick: its function runs from the next (the controls held) */
            turn = u->turn_ctl; throttle = u->thr_ctl;
        } else
        if (u->target >= 0) {
            const unit *t = &us->u[u->target];
            float p[3] = {tx, ty, tz}, hd, pt, flat, dist;
            bearing_to(u, p, &hd, &pt, &flat, &dist);
            /* the attack state (0x100a7e40): turn toward it (0x1000d679), the throttle toward the middle of the
             * selected weapon's band ((+0x4c + +0x50) x 0.5, 5000 without one: 0x100a7d9b) */
            float err = wrap180(hd - u->s.heading);
            int roll = 0;   /* the fire controller's decision roll this tick (its aim flag for the twist) */
            turn = steer(err);
            int st = c->sel < c->gr.nweapons ? c->gr.weapon[c->sel].type : -1;
            float want = st >= 0 && st < GEAR_WEAPON_TYPES
                ? (gear_weapon_stat[st].min_range + gear_weapon_stat[st].max_range) * 0.5f : 5000.0f;
            throttle = approach(u, flat, want);
            /* 0x100a7e40: the obstacle probe first (0x100aa2ac), its steering in place of the attack's */
            ai_probe(us, w, i, hd, &turn, &throttle);
            /* the aim point: the target, or for the leading controller where it will be when the shot arrives
             * (0x100066ea: n = (int)(distance / (shot speed - its speed toward us)), within the shot's life) */
            float ap[3] = {tx, ty, tz};
            if (skill_row[sk].lead && st >= 0 && st < GEAR_WEAPON_TYPES) {
                float vx = t->s.vx, vz = t->s.vz, v = sqrtf(vx * vx + vz * vz);
                float rx = tx - u->s.x, rz = tz - u->s.z;
                if (rx * vx + rz * vz < 0) v = -v;
                float spd = gear_weapon_stat[st].speed, den = spd - v;
                float n = fabsf(den) < 1e-7f ? 2.0f : dist / den;
                if (fabsf(n) >= 1e-7f && n <= (float)gear_weapon_stat[st].life && n > 1.0f) {
                    int ni = (int)n;
                    ap[0] = tx + vx * (float)ni; ap[2] = tz + vz * (float)ni;
                }
            }
            float ahd, apt, afl, ad;
            bearing_to(u, ap, &ahd, &apt, &afl, &ad);
            /* shots fly along the vehicle's aim (RE_NOTES "Weapons and shots"): its heading, pitched at the point */
            /* along the torso: the heading plus its twist (the mounts ride the torso, 0x1005d75c's dum_trso turn) */
            float hr = sim_gear_view_yaw(&u->s) * PI_F / 180, pr = apt * PI_F / 180;
            aim[0] = eye[0] + sinf(hr) * cosf(pr) * ad;
            aim[1] = eye[1] + sinf(pr) * ad;
            aim[2] = eye[2] + cosf(hr) * cosf(pr) * ad;
            /* the fire controller (0x10005a87, or 0x10005570 for skill rows with +0x18): a decision when its time
             * comes (+0x1ba): k = rand(+0x1b2) (0x10089e69), the next k x 11 ticks on; it fires on k = 0 with a weapon ready
             * and in range, the heading error (halved for gears and infantry) within the window (0x1000536e's; 1
             * degree for 0x10005570, on the leading aim) and the aim ray finding the target (0x10004d60) */
            if (us->ticks >= u->next_decide) {
                int k = units_rand(u->react > 0 ? u->react : 2);
                roll = k;
                u->next_decide = us->ticks + k * 11;
                /* the weapon (0x100073b7, then 0x100060a7 or, with +0x1be, 0x10006473; else 0x10005ef1) */
                int tinf = t->gm.vclass_raw == 9, pick;
                if (ai_rechoose(c, tinf, dist)) {
                    float bh, bp, bf, bd;
                    float tp3[3] = {tx, ty, tz};
                    bearing_to(u, tp3, &bh, &bp, &bf, &bd);
                    int blocked = !aim_clear(us, w, i, u->target, bp);
                    if (!skill_row[sk].flag) c->fire_all = 0;
                    pick = skill_row[sk].flag ? ai_pick_all(c, dist, (int)((float)skill_row[sk].heat / 100.0f * 65.0f))
                         : ai_pick(c, tinf, t->gm.vclass_raw == 5 || t->gm.vclass_raw == 8, 0, blocked, dist);
                    if (!pick) pick = ai_ready(c, dist);
                } else pick = ai_ready(c, dist);
                st = c->sel < c->gr.nweapons ? c->gr.weapon[c->sel].type : -1;
                float e2 = wrap180(ahd - u->s.heading);
                if (u->gm.vclass_raw == 1 || u->gm.vclass_raw == 9) e2 /= 2.0f;
                float wnd = skill_row[sk].lead ? 1.0f : fire_window(u, t, dist, c->sel < c->gr.nweapons ? c->sel : 0);
                if (k == 0 && pick && fabsf(e2) < wnd && aim_clear(us, w, i, u->target, apt)) {
                    trig = !c->trigger_was;   /* single-shot weapons need a fresh pull */
                    /* the lock (0x10006672), firing a guided weapon: rand(100) below the row's +0x10, or aircraft
                     * (classes 5 / 8) always */
                    if (st >= 0 && st < GEAR_WEAPON_TYPES && (gear_weapon_stat[st].sflags & 1))
                        c->lock = (units_rand(100) < skill_row[sk].lock || u->gm.vclass_raw == 5 || u->gm.vclass_raw == 8)
                            ? u->target : -1;
                }
            }
            /* the torso aimed at the aim point (0x10005570 / 0x10005a87 -> 0x1000d61d) */
            u->pan_ctl = ai_twist(u, wrap180(ahd - u->s.heading), roll);
            u->s.ai_tilt = ai_tilt(apt, roll);
            /* a gear that didn't fire and whose aim ray is blocked closes to min(range, 4500) + 50 (0x100a7e40) */
            if (!trig && u->gm.vclass_raw == 1 && !aim_clear(us, w, i, u->target, apt)) {
                float rg = skill_row[sk].range ? (float)skill_row[sk].range : 15000.0f;
                throttle = approach(u, flat, (rg >= 4500.0f ? 4500.0f : rg) + 50.0f);
            }
            /* the combat AI's state (0x100a58c0): chosen when none (0x100a6956), started (0x100a6a5e), run, and
             * ended (0x100a7203 -> 0x100a7171: the last state kept, none current) */
            if (mode2) {
                /* the attack order (mode 2, 0x1000cb8f): closing to 100 */
                throttle = approach(u, flat, 100.0f);
            } else if (u->ai_state < 0) {
                u->ai_state = ai_choose(us, u, u->target, flat);
                ai_enter(us, u, u->target);
            }
            float at = turn, ath = throttle;
            if (!mode2 && ai_step(us, w, i, u->target, &turn, &throttle, at, ath, err, flat)) ai_leave(us, u);
        } else {
            /* the mode's function (0x1000cb29 / 0x1000cb8f / 0x1000cf4d) without a vehicle to fight: modes 0 and 0xc
             * stand (+8 = 0; aircraft hold 4000, ai_altitude); 4 drives away from its
             * target (+0xe4 + 180) at half the control (1/128) for a gear, full for others; 2 (on a nav or a
             * structure), 5 to 8, 0xd and 0xe toward the point (0x1000d1ca), by 0x1000d2bd within its radius and
             * 0x1000d679 (5: the formation slot, 0x1000ebfc, after); 10 and 11 are powered down (+0x4f0 | 3, not
             * running: still); 0xf walks to its cover point (below) */
            float p[3];
            int md = u->mode, pc = md == 2 || md == 4 ? u->mtgt : u->marg;
            if (md == 5) ai_form_point(us, w, i), pc = u->marg;
            if (md == 0xf) {   /* 0x100abd9d: the walk to the cover point */
                int t = ai_unit_of(us, u->mtgt), k = u->cover;
                if (k < 0 || k >= us->ncov || (us->cov_taken[k] && us->cov_by[k] != i)) {
                    ai_set_mode(us, w, gl, i, 5, 0x2201, 0);
                } else {
                    float tp[3] = {t >= 0 ? us->u[t].s.x : u->s.x, t >= 0 ? us->u[t].s.y : u->s.y,
                                   t >= 0 ? us->u[t].s.z : u->s.z}, sp[3];
                    if (t >= 0 && cover_spot(us, w, k, tp, u->gm.stand, sp)) ai_wp(u, sp[0], sp[1], sp[2]);
                    if (u->wp_r > 0) {
                        float dx = u->wp_x - u->s.x, dz = u->wp_z - u->s.z, d = sqrtf(dx * dx + dz * dz);
                        float hd = atan2f(dx, dz) * 180.0f / PI_F;
                        if (!ai_probe(us, w, i, hd, &turn, &throttle)) {
                            turn = steer(wrap180(hd - u->s.heading));
                            u->turn_ctl = turn;
                            throttle = approach(u, d, 1000.0f);
                        }
                        if (d <= 1000.0f) throttle = 0;
                    }
                }
            }
            if ((md == 2 || md == 4 || (md >= 5 && md <= 8) || md == 0xd || md == 0xe) && ai_pos(us, w, u, pc, p)) {
                float dx = p[0] - u->s.x, dz = p[2] - u->s.z, d = sqrtf(dx * dx + dz * dz);
                float hd = atan2f(dx, dz) * 180.0f / PI_F;
                /* the weave (modes 5 .. 8, 0x10010aa3), then the obstacle probe (0x100aa2ac) before the mode's own
                 * steering; mode 4: a gear skates away, others probe first */
                if (md >= 5 && md <= 8) ai_weave(us, u);
                if (md == 4) {
                    if (u->gm.vclass_raw == 1 || !ai_probe(us, w, i, wrap180(hd + 180.0f), &turn, &throttle)) {
                        turn = steer(wrap180(hd + 180.0f - u->s.heading));
                        throttle = u->gm.vclass_raw == 1 ? 0.5f : 1.0f;
                    }
                } else if (!ai_probe(us, w, i, hd, &turn, &throttle)) {
                    turn = steer(wrap180(hd - u->s.heading));
                    u->turn_ctl = turn;
                    throttle = approach(u, d, ai_radius(w, u, pc));
                }
                /* mode 2 on a structure (0x400 | k: 0x1000cb8f -> 0x10005300 -> 0x10005a87): the fire controller with
                 * its default window, 10 degrees, and the ray (0x10004d60) finding the structure's object first */
                if (md == 2 && (pc & 0xf00) == 0x400 && c->gr.nweapons && !ai_sdone[pc & 63]) {
                    const mission *mm = world_mission(w);
                    int sk2 = pc & 63, sinst = sk2 < mm->ntgt ? world_obj_instance(w, mm->tgt[sk2].obj) : -1;
                    float ahd, apt, afl, ad;
                    bearing_to(u, p, &ahd, &apt, &afl, &ad);
                    int roll2 = 0;
                    if (us->ticks >= u->next_decide) {
                        int k = units_rand(u->react > 0 ? u->react : 2);
                        roll2 = k;
                        u->next_decide = us->ticks + k * 11;
                        int pick = ai_rechoose(c, 0, ad) ? ai_pick(c, 0, 0, 1, 0, ad) : ai_ready(c, ad);
                        if (!pick) pick = ai_ready(c, ad);
                        float e2 = wrap180(ahd - u->s.heading);
                        if (u->gm.vclass_raw == 1 || u->gm.vclass_raw == 9) e2 /= 2.0f;
                        if (k == 0 && pick && fabsf(e2) < 10.0f && sinst >= 0) {
                            float e3[3], f3[3], tt = 2;
                            unit_eye(u, e3);
                            float hr2 = sim_gear_view_yaw(&u->s) * PI_F / 180, pr2 = apt * PI_F / 180;
                            f3[0] = e3[0] + sinf(hr2) * cosf(pr2) * ad * 1.05f;
                            f3[1] = e3[1] + sinf(pr2) * ad * 1.05f;
                            f3[2] = e3[2] + cosf(hr2) * cosf(pr2) * ad * 1.05f;
                            if (world_ray(w, e3, f3, &tt) == sinst) trig = !c->trigger_was;
                        }
                    }
                    u->pan_ctl = ai_twist(u, wrap180(ahd - u->s.heading), roll2);
                    u->s.ai_tilt = ai_tilt(apt, roll2);
                    float hr3 = sim_gear_view_yaw(&u->s) * PI_F / 180, pr3 = apt * PI_F / 180;
                    aim[0] = eye[0] + sinf(hr3) * cosf(pr3) * ad;
                    aim[1] = eye[1] + sinf(pr3) * ad;
                    aim[2] = eye[2] + cosf(hr3) * cosf(pr3) * ad;
                    saim = 1;
                }
            }
        }
        u->turn_ctl = turn;
        u->thr_ctl = throttle;
        if (u->cb.stun > 0) { u->cb.stun--; throttle = 0; turn = 0; }   /* +0xc3d: the power out (0x10060491) */
        /* the cover states' stance (the control's +0x54: crouch on the ground, not skating; 0 stands, 0x1005d75c) */
        if (u->ai_crouch && u->s.on_ground && !u->s.skating) u->s.crouched = 1;
        else if (!u->ai_crouch) u->s.crouched = 0;
        sim_gear_tick_pan(&u->s, throttle * u->cb.speed_factor, turn, u->pan_ctl, ground_at, w);
        ai_altitude(us, w, u);
        if (!c->gr.nweapons) continue;
        mount_ctx mc = {u};
        if (combat_weapons_tick(c, i, trig, eye, u->target >= 0 || saim ? aim : eye, unit_mount, &mc, s) > 0)
            u->ai_fired = 1;   /* 0x10005300's result: a peek counts its shots */
    }
}

void units_goals(units_goal goal[16], const objectives *o, const units *us, const world *w)
{
    const mission *m = world_mission(w);
    memset(goal, 0, sizeof(units_goal) * 16);
    for (int g = 0; g < 16; g++) {
        units_goal *gl = &goal[g];
        int k = o ? o->current[g] : -1;
        gl->obj = k >= 0 && k < m->mtbl[g].nobj ? k : -1;
        /* the group's first reach (0x20) objective's first target (0x10069aac) */
        for (int q = 0; o && q < m->mtbl[g].nobj && !gl->reach; q++)
            if (objectives_type(o, g, q) == 0x20 && o->st[g][q].ntgt > 0 && o->st[g][q].tgt[0].type == OBJT_NAV)
                gl->reach = 0x100 | (o->st[g][q].tgt[0].idx & 0xff);
        if (gl->obj < 0) continue;
        const mission_objective *ob = &m->mtbl[g].obj[k];
        gl->type = objectives_type(o, g, k);
        gl->count = ob->count;
        gl->aiflag = ob->aiflag;
        const obj_state *st = &o->st[g][k];
        for (int q = 0; q < st->ntgt && gl->ntgt < 16; q++) {
            int ty = st->tgt[q].type, idx = st->tgt[q].idx;
            if (ty == OBJT_NAV) {
                gl->tgt[gl->ntgt].kind = 1; gl->tgt[gl->ntgt].idx = idx;
                gl->tgt[gl->ntgt].done = idx >= 0 && idx < 0x80 && (o->nav_seen[idx] >> g & 1);
                if (!gl->go && gl->type == 0x100 && idx >= 0 && idx < m->nnav) {
                    gl->go = 1; gl->x = m->nav[idx].x; gl->y = m->nav[idx].y; gl->z = m->nav[idx].z;
                }
            } else if (ty == OBJT_VEHICLE) {
                int u = -1;
                for (int j = 0; j < us->n; j++)
                    if ((idx == -1 && us->u[j].player) || (idx != -1 && us->u[j].gps == idx)) u = j;
                if (u < 0) continue;
                gl->tgt[gl->ntgt].kind = 2; gl->tgt[gl->ntgt].idx = u;
            } else if (ty == OBJT_STRUCTURE) {
                int inst = idx >= 0 && idx < m->ntgt ? world_obj_instance(w, m->tgt[idx].obj) : -1;
                float x = 0, y = 0, z = 0, sn, cs;
                int hidden = inst >= 0 ? world_object_pose(w, inst, &x, &y, &z, &sn, &cs) : -1;
                if (hidden < 0) continue;
                gl->tgt[gl->ntgt].kind = 4; gl->tgt[gl->ntgt].idx = idx;
                gl->tgt[gl->ntgt].done = hidden == 1;
                gl->tgt[gl->ntgt].x = x; gl->tgt[gl->ntgt].y = y; gl->tgt[gl->ntgt].z = z;
            } else continue;
            gl->ntgt++;
        }
    }
}

int units_add_player(units *us, world *w, hg_archive *a, const char *gdf, int side, float internal_scale,
                     const sim_gear *g)
{
    if (us->n >= UNITS_MAX) return -1;
    unit *u = &us->u[us->n];
    memset(u, 0, sizeof *u);
    snprintf(u->gear, sizeof u->gear, "%s", gdf);
    if (gearmodel_load(a, gdf, &u->gm) || combat_init(&u->cb, a, gdf, internal_scale)) return -1;
    u->cb.vclass = u->gm.vclass ? u->gm.vclass : 1;
    u->cb.strike_zone = u->gm.nparts && u->gm.part[0].rec == 0 ? u->gm.part[0].zone : 0;
    {
        int ty[GEAR_MAX_WEAPONS], ki[GEAR_MAX_WEAPONS];
        for (int q = 0; q < u->cb.gr.nweapons; q++) { ty[q] = u->cb.gr.weapon[q].type; ki[q] = u->cb.gr.weapon[q].kind; }
        gearmodel_stand(&u->gm, a, ty, ki, u->cb.gr.nweapons, u->cb.gr.version);   /* the standing pose (0x1001386b) */
    }
    u->player = 1;
    u->vflags = 0x400;
    u->gps = -1;
    u->placed = 1;
    u->side = side;
    u->target = -1;
    u->s = *g;
    for (int k = 0; k < u->gm.nparts; k++) {
        u->obj[k] = world_add_object(w, u->gm.part[k].model, 0, 0, 0, 0);
        world_show_object(w, u->obj[k], 0);
    }
    return us->n++;
}

int units_add_mate(units *us, world *w, hg_archive *a, const char *gdf, int slot, int voice)
{
    if (us->n >= UNITS_MAX) return -1;
    const mission *m = world_mission(w);
    unit *u = &us->u[us->n];
    memset(u, 0, sizeof *u);
    snprintf(u->gear, sizeof u->gear, "%s", gdf);
    if (gearmodel_load(a, gdf, &u->gm) || combat_init(&u->cb, a, gdf, internal_scale)) return -1;
    u->cb.vclass = u->gm.vclass ? u->gm.vclass : 1;
    u->cb.strike_zone = u->gm.nparts && u->gm.part[0].rec == 0 ? u->gm.part[0].zone : 0;
    {
        int ty[GEAR_MAX_WEAPONS], ki[GEAR_MAX_WEAPONS];
        for (int q = 0; q < u->cb.gr.nweapons; q++) { ty[q] = u->cb.gr.weapon[q].type; ki[q] = u->cb.gr.weapon[q].kind; }
        gearmodel_stand(&u->gm, a, ty, ki, u->cb.gr.nweapons, u->cb.gr.version);   /* the standing pose (0x1001386b) */
    }
    u->gps = -1 - slot;   /* the objectives' vehicle index for the launch's wingman records (-2 / -3) */
    u->group = 0; u->slot = slot; u->leader = 0;
    u->vflags = 0x480;   /* the launch's USERSTAR records (HGSHELL 0x100ff1e6) */
    u->voice = voice;
    u->react = 2;   /* the launch's USERSTAR records leave +8 / +0x14 at 0 (HGSHELL 0x100ff1e6): 1 + 1, row 5 */
    u->skill = 5;
    u->side = m->group_side[0];
    u->target = -1;
    u->order_target = -1;
    u->ai_state = u->ai_prev = u->ai_next = -1;
    u->cover = u->ai_got = -1;
    u->start_at = 0x43e + rand() % 0x16a;
    /* placed as units_spawn places a group's vehicles: the start navpoint, the formation slot from the leader's (the
     * player, slot 0) */
    const mission_form *f = m->nform ? &m->form[0] : NULL;
    for (int k = 0; k < m->nform; k++)
        if (same_ci(m->form[k].name, m->group_form[0])) f = &m->form[k];
    mission_start st = mission_group_start(m, 0);
    float x = 0, z = 0, hd = 0;
    if (st.found) {
        u->placed = 1;
        x = st.x; z = st.z; hd = st.heading;
        if (f && slot < f->nslots) {
            float ox = f->x[slot] - f->x[0], oz = f->z[slot] - f->z[0];
            float s = sinf(hd * PI_F / 180), c = cosf(hd * PI_F / 180);
            x += ox * c + oz * s;
            z += -ox * s + oz * c;
            hd = f->heading[slot];
        }
    }
    float gy = ground_at(w, x, z);
    sim_gear_init(&u->s, x, gy + u->gm.stand, z, hd, 0, u->gm.stand);
    u->s.vclass = u->gm.vclass_raw ? u->gm.vclass_raw : 1;
    for (int k = 0; k < u->gm.nparts; k++) {
        u->obj[k] = world_add_object(w, u->gm.part[k].model, 0, 0, 0, 0);
        if (!u->placed) world_show_object(w, u->obj[k], 0);
    }
    us->n++;
    units_update(us, w);
    return us->n - 1;
}

int units_by_gps(const units *us, int gps)
{
    for (int i = 0; i < us->n; i++)
        if (us->u[i].gps == gps) return i;
    return -1;
}

int units_leader(const units *us, int group)
{
    if (group < 0 || group >= 16) return -1;
    int l = us->lead[group];
    if (l == -2) for (int j = 0; j < us->n; j++) if (us->u[j].player) return j;
    return l >= 0 && l < us->n ? l : -1;
}

int units_take_reply(units *us, int *unit, int *code)
{
    if (us->nreply <= 0) return 0;
    *unit = us->reply[0].unit;
    *code = us->reply[0].code;
    memmove(us->reply, us->reply + 1, (size_t)(us->nreply - 1) * sizeof us->reply[0]);
    us->nreply--;
    return 1;
}

/* 0x1003f7b0's prefixes by the root object's name: gears S.. (the striders' SN.. set) 0x2000, SA.. 0x3000; aircraft
 * ?13 (the Orca's V13SPINN) 0x5000, ?63 0x4000 */
static int anim_prefix(const unit *u)
{
    const char *n = u->gm.nparts ? u->gm.part[0].model : "";
    int cls = u->gm.vclass_raw;
    if (cls == 1 && (n[0] == 's' || n[0] == 'S')) return (n[1] == 'a' || n[1] == 'A') ? 0x3000 : 0x2000;
    if (cls == 5 && n[0] && n[1] == '1' && n[2] == '3') return 0x5000;
    if (cls == 5 && n[0] && n[1] == '6' && n[2] == '3') return 0x4000;
    return 0;
}

int units_animate(units *us, hg_archive *a, int ticks, const sim_control *pc)
{
    int ev = 0;
    for (int i = 0; i < us->n; i++) {
        unit *u = &us->u[i];
        int cls = u->gm.vclass_raw;
        const char *n = u->gm.nparts ? u->gm.part[0].model : "";
        /* 0x10013d53: gears, infantry and aircraft, but the V10.. (the Orca's older model) */
        if (cls != 1 && cls != 9 && cls != 5) continue;
        if ((n[0] == 'v' || n[0] == 'V') && n[1] == '1' && n[2] == '0') continue;
        if (!u->placed) continue;
        if (anim_offscreen(us, u)) continue;
        if (u->ap.cur == 0 && u->ap.next == 0 && !u->ap.an) anim_player_init(&u->ap);
        sim_gear *g = &u->s;
        /* a leg's loss and the 0x1e46 crit (0x1005ab68 / 0x1005b5c6), and getting up with the legs gone (0x10013d53:
         * +0xc15 = 1 again): the move (0x1005d75c) starts the fall - back for 1, forward for 2 (the 0x1e46 crit while
         * reversing, +0x59) - unless one runs */
        if (u->cb.fall) { g->body |= u->cb.fall; u->cb.fall = 0; }
        if (u->cb.legs_gone) g->legs_gone = 1;
        if ((u->cb.knock || u->ap.knock) && !(g->body & (ANIM_FALLF | ANIM_FALLB))) {
            g->body |= (u->cb.knock && g->reverse) ? ANIM_FALLF : ANIM_FALLB;
        }
        u->cb.knock = 0; u->ap.knock = 0;
        unsigned fl = g->body & ~(unsigned)(ANIM_SKATE | ANIM_CROUCH);
        if (g->skating) fl |= ANIM_SKATE;
        if (g->crouched) fl |= ANIM_CROUCH;
        if (u->player && (fl & (ANIM_FALLF | ANIM_FALLB)) && !(u->body_seen & (ANIM_FALLF | ANIM_FALLB))) ev |= 2;
        u->body_seen = (int)fl;
        anim_in in;
        memset(&in, 0, sizeof in);
        in.cls = cls;
        in.set = u->gm.anim_set;
        in.prefix = anim_prefix(u);
        /* a wreck's control stops (0x1000a69d -> 0x1000d70f): its throttle at 1/64 */
        in.thr = u->cb.dead ? 1.0f / 64 : g->thr.cur;
        in.reverse = g->reverse;
        in.stun = u->cb.stun > 0;
        if (u->player && pc) { in.straffe_l = pc->straffe_l; in.straffe_r = pc->straffe_r; }
        else if (!u->player) { in.straffe_l = u->ai_straffe < 0; in.straffe_r = u->ai_straffe > 0; }   /* 0x100ac0bb */
        /* in the air (the vehicle's +0x158, 0x1005d75c) the legs take the skating stance */
        if (!g->on_ground && cls != 5) fl |= ANIM_SKATE;
        if (!u->cb.dead) anim_pick(&u->ap, &in, &fl);
        anim_advance(&u->ap, a, ticks, &fl, g->legs_gone);
        g->body = fl & ~(unsigned)ANIM_SKATE;
        if (!g->crouched) g->body &= ~(unsigned)ANIM_CROUCH;
        /* 0x10013d53's 0x10108378: a wreck (state 4 on the ground, 0x560 | 0x100 by 0x10058f0d) keeps its head */
        gearmodel_animate(&u->gm, &u->ap, fl, !u->cb.dead);
        if (u->ap.footstep) {
            u->ap.footstep = 0;
            if (u->player) ev |= 1;   /* 0x1003fbb7: the player's only (its dust, 0x10086007, not done) */
        }
    }
    return ev;
}

float units_aim_dist(units_aimd *a, int now, float hit)
{
    if (!a->init) { a->init = 1; a->last = now; a->target = 50000; a->cur = 50000; a->T = 36; a->c0 = 0; }
    if (hit >= 0) a->c0 = hit <= 2000.0f ? 2000 : (int)hit;
    a->target = a->c0;
    int d = a->target - a->cur;
    if (d != 0) {
        int step = d * (now - a->last) / a->T;
        if ((d < 0 && step <= d) || (d > 0 && step >= d)) a->cur = a->target;
        else a->cur += step;
    }
    a->last = now;
    return (float)a->cur;
}

void units_set_view(units *us, const camera *cam, int w, int h, float far_dist)
{
    us->view = *cam;
    us->view_w = w; us->view_h = h; us->view_far = far_dist;
    us->view_set = 1;
}

static int anim_must(int id)
{
    switch (id & 0xff) {
    case 4: case 5: case 6: case 7: case 8: case 0xd: case 0x11: case 0x15: case 0x17: case 0x18: case 0x19: return 1;
    }
    return 0;
}

/* 0x10013d53's off-screen test (units_set_view) */
static int anim_offscreen(const units *us, const unit *u)
{
    if (!us->view_set || u->player || anim_must(u->ap.cur) || anim_must(u->ap.next)) return 0;
    const camera *cam = &us->view;
    float f = (float)us->view_w * 0.5f / tanf(cam->fov * 0.5f * PI_F / 180);
    float cy = cosf(cam->yaw * PI_F / 180), syw = sinf(cam->yaw * PI_F / 180);
    float cp = cosf(cam->pitch * PI_F / 180), sp = sinf(cam->pitch * PI_F / 180);
    float px = u->s.x - cam->x, py = u->s.y - cam->y, pz = u->s.z - cam->z;
    float x1 = px * cy - pz * syw, z1 = px * syw + pz * cy;
    float y2 = py * cp - z1 * sp, z2 = py * sp + z1 * cp;
    if (z2 < 1e-3f) z2 = 1e-3f;
    float sx = (float)us->view_w * 0.5f + x1 * f / z2, sy = (float)us->view_h * 0.5f - y2 * f / z2;
    float r = f * u->gm.stand / z2;
    return sx - r > (float)us->view_w + 25 || sx + r < -25.0f || sy - r > (float)us->view_h + 25 || sy + r < -25.0f ||
           (us->view_far > 0 && z2 - r > us->view_far);
}
