#include "units.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "gear_tables.h"

#define PI_F 3.14159265f

static int same_ci(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return !*a && !*b;
}

static float ground_at(void *ctx, float x, float z)
{
    int f;
    float h = world_ground_height((const world *)ctx, x, z, &f);
    return f ? h : 0.0f;
}

void units_spawn(units *us, world *w, hg_archive *a)
{
    memset(us, 0, sizeof *us);
    const mission *m = world_mission(w);
    int count[16] = {0}, lead_slot[16];
    for (int g = 0; g < 16; g++) lead_slot[g] = 0;
    for (int i = 0; i < m->ngps && us->n < UNITS_MAX; i++) {
        const mission_gps *gp = &m->gps[i];
        int g = gp->group & 15;
        int slot = count[g]++;
        if (gp->leader) { lead_slot[g] = slot; }
        if (gp->role == 0) continue;   /* the player */
        unit *u = &us->u[us->n];
        memset(u, 0, sizeof *u);
        u->gps = i;
        snprintf(u->gear, sizeof u->gear, "%s", gp->gear);
        u->group = g; u->slot = slot; u->leader = gp->leader;
        u->side = m->group_side[g];
        u->target = -1;
        u->order_target = -1;
        u->start_at = 0x43e + rand() % 0x16a;
        if (gearmodel_load(a, gp->gear, &u->gm)) continue;
        /* the difficulty's internal multiplier (0x1004e4cc) goes to the vehicles whose player record's +8 is the
         * player's: read as the player's group (INFERRED: mission 1's other friendly groups read x 1 in the original) */
        combat_init(&u->cb, a, gp->gear, g == 0 ? 6.0f : 1.0f);
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
        sim_gear_init(&u->s, x, gy + h, z, hd, 0, h);
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
         * table; its fall and smoke aren't done: INFERRED still pose), less its destroyed parts */
        if (u->cb.dead && !u->shown_dead) {
            for (int k = 0; k < u->gm.nparts; k++)
                if (u->gm.part[k].zone >= 1 && u->gm.part[k].zone <= GEAR_ZONES && u->cb.zone_gone[u->gm.part[k].zone - 1])
                    world_show_object(w, u->obj[k], 0);
            u->shown_dead = 1;
        }
        if (u->cb.dead) continue;
        float s = sinf(u->s.heading * PI_F / 180), c = cosf(u->s.heading * PI_F / 180);
        for (int k = 0; k < u->gm.nparts; k++) {
            if (u->gm.part[k].zone >= 1 && u->gm.part[k].zone <= GEAR_ZONES && u->cb.zone_gone[u->gm.part[k].zone - 1])
                world_show_object(w, u->obj[k], 0);
            const gearmodel_part *p = &u->gm.part[k];
            world_move_object(w, u->obj[k], u->s.x + p->x * c + p->z * s, u->s.y + p->y, u->s.z - p->x * s + p->z * c,
                              u->s.heading);
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
        if (i == owner || u->cb.dead || !u->placed) continue;
        /* reject segments nowhere near (the parts lie within a few hundred units of the origin) */
        float cx = u->s.x, cy = u->s.y, cz = u->s.z;
        float lo[3] = {cx - 1500, cy - 1500, cz - 1500}, hi[3] = {cx + 1500, cy + 1500, cz + 1500};
        float tt;
        if (!seg_box(p0, p1, lo, hi, &tt)) continue;
        float s = sinf(u->s.heading * PI_F / 180), c = cosf(u->s.heading * PI_F / 180);
        for (int k = 0; k < u->gm.nparts; k++) {
            const gearmodel_part *p = &u->gm.part[k];
            if (p->zone < 1 || p->zone > GEAR_ZONES || u->cb.zone_gone[p->zone - 1]) continue;
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

/* INFERRED top speeds for the version 1 vehicles (units per 5 ms tick at full throttle / 64; their builder,
 * 0x1004f86d, isn't traced for speed): ground vehicles 1.5 x the Hunter's, aircraft 3 x, infantry a fifth */
static float top_speed(int vclass)
{
    switch (vclass) {
    case 1: return 330.221f;
    case 4: return 495.0f;
    case 5: case 8: return 990.0f;
    case 9: return 66.0f;
    }
    return 330.221f;
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

/* the ground between two vehicles' eyes (100 above their origins), sampled every 500 */
static int in_sight(world *w, const unit *a, const unit *b)
{
    float ax = a->s.x, ay = a->s.y + 100.0f, az = a->s.z, bx = b->s.x, by = b->s.y + 100.0f, bz = b->s.z;
    float d = sqrtf((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
    int n = (int)(d / 500.0f);
    for (int k = 1; k < n; k++) {
        float t = (float)k / (float)n, x = ax + (bx - ax) * t, z = az + (bz - az) * t, y = ay + (by - ay) * t;
        int f;
        float g = world_ground_height(w, x, z, &f);
        if (f && g > y) return 0;
    }
    return 1;
}

void units_ai_tick(units *us, world *w, const units_goal goal[16], combat_shots *s)
{
    us->ticks++;
    int player_side = -1;
    for (int j = 0; j < us->n; j++) if (us->u[j].player) player_side = us->u[j].side;
    for (int i = 0; i < us->n; i++) {
        unit *u = &us->u[i];
        if (u->player || u->cb.dead || !u->placed) continue;
        /* a vehicle stands still until its engine runs (0x10060491 zeroes the speeds below state 2) */
        if (us->ticks < u->start_at) continue;
        combatant *c = &u->cb;
        /* gears: the design's speed points x 47.1744 (0x1004e6a0, +0x4d4); others INFERRED by class */
        if (u->s.top_speed <= 0)
            u->s.top_speed = c->gr.speed_points > 0 ? (float)c->gr.speed_points * 47.1744f : top_speed(u->gm.vclass);
        /* the longest reach among its loaded weapons bounds the search */
        float reach = 0;
        for (int k = 0; k < c->gr.nweapons; k++) {
            int t = c->gr.weapon[k].type;
            if (c->state[k] < 0 || t < 0 || t >= GEAR_WEAPON_TYPES) continue;
            float r = weapon_reach(t);
            if (r > reach) reach = r;
        }
        /* the nearest hostile it engages (a squad mate told to disengage holds its fire). The AI's engage test
         * (0x100ac524, its state 0x17): in single play the player, a hostile within 15000 (0x1010370c) in sight
         * (0x1000510e), or one of the vehicles that shot it that targets it, within 15000 and in sight
         * (0x100ac1e0 / 0x10004d60). The player's own group engages any hostile within 15000 in sight (INFERRED: its
         * AI files' attack nodes aren't traced). Sight: the ground between them (INFERRED for the scene ray test) */
        float best = reach < 15000.0f ? reach * reach : 15000.0f * 15000.0f, tx = 0, ty = 0, tz = 0;
        int friendly = player_side >= 0 && u->side == player_side;
        u->target = -1;
        for (int j = 0; reach > 0 && u->order != UNIT_ORDER_DISENGAGE && j < us->n; j++) {
            const unit *v = &us->u[j];
            if (j == i || v->cb.dead || !v->placed || v->side == u->side) continue;
            if (!friendly && !v->player && v->target != i) continue;
            float dx = v->s.x - u->s.x, dy = v->s.y - u->s.y, dz = v->s.z - u->s.z, d = dx * dx + dy * dy + dz * dz;
            if (d < best && in_sight(w, u, v)) { best = d; u->target = j; tx = v->s.x; ty = v->s.y; tz = v->s.z; }
        }
        /* ordered at the player's target: it is the target, and the mate closes in while it's out of reach */
        int chase = 0;
        if (u->order == UNIT_ORDER_SCOUT) {
            if (u->target < 0) { chase = 1; tx = u->order_x; tz = u->order_z; }   /* to the nav, fighting on the way */
        }
        else if (u->order >= UNIT_ORDER_ATTACK && u->order <= UNIT_ORDER_DEFEND && u->order_target >= 0 &&
            u->order_target < us->n && !us->u[u->order_target].cb.dead) {
            const unit *v = &us->u[u->order_target];
            float dx = v->s.x - u->s.x, dy = v->s.y - u->s.y, dz = v->s.z - u->s.z, d = dx * dx + dy * dy + dz * dz;
            if (d < reach * reach) { u->target = u->order_target; best = d; tx = v->s.x; ty = v->s.y; tz = v->s.z; }
            else { u->target = -1; chase = 1; tx = v->s.x; tz = v->s.z; }
        }
        /* the player's position, for the mates that follow it */
        const unit *lead = NULL;
        for (int j = 0; j < us->n; j++) if (us->u[j].player) lead = &us->u[j];
        float turn = 0, throttle = 0;
        int trig = 0;
        if (u->target >= 0 && c->state[c->sel] == 1) {
            /* a weapon that reaches, flat-shooting ones first (a lobbed grenade aimed straight falls short) */
            float d = sqrtf(best);
            int pick = -1, pick_flat = 0;
            for (int k = 0; k < c->gr.nweapons; k++) {
                int t = c->gr.weapon[k].type;
                if (c->state[k] < 0 || t < 0 || t >= GEAR_WEAPON_TYPES) continue;
                /* in range as 0x1000502f has it: min range (+0x4c) <= d < the reach */
                if (weapon_reach(t) <= d || d < gear_weapon_stat[t].min_range) continue;
                int flat = gear_weapon_stat[t].gravity == 0;
                if (pick < 0 || (flat && !pick_flat)) { pick = k; pick_flat = flat; }
            }
            if (pick >= 0) c->sel = pick;
        }
        if (u->target >= 0) {
            float want = atan2f(tx - u->s.x, tz - u->s.z) * 180.0f / PI_F;
            float diff = fmodf(want - u->s.heading + 540.0f, 360.0f) - 180.0f;
            turn = diff > 2 ? 1.0f : diff < -2 ? -1.0f : diff / 2.0f;
            /* pulled every other tick: single-shot weapons need a fresh pull per shot */
            trig = fabsf(diff) < 5.0f && !c->trigger_was;
        } else if (chase || ((u->order == UNIT_ORDER_FORM || u->order == UNIT_ORDER_DISENGAGE) && lead)) {
            float gx = chase ? tx : lead->s.x, gz = chase ? tz : lead->s.z;
            float dx = gx - u->s.x, dz = gz - u->s.z, d = sqrtf(dx * dx + dz * dz);
            if (d > (chase && u->order != UNIT_ORDER_SCOUT ? reach * 0.8f : 1500.0f)) {
                float want = atan2f(dx, dz) * 180.0f / PI_F;
                float diff = fmodf(want - u->s.heading + 540.0f, 360.0f) - 180.0f;
                turn = diff > 2 ? 1.0f : diff < -2 ? -1.0f : diff / 2.0f;
                throttle = fabsf(diff) < 45.0f ? 1.0f : 0.3f;
            }
        } else if (u->order == UNIT_ORDER_COVER) {
            /* holds where it is */
        } else if (u->group >= 0 && u->group < 16 && goal[u->group].go) {
            /* the group's current objective is a go-to (0x100): head for its nav. The leader drives onto it; the
             * others stop short at their formation distance (INFERRED steering; the objective choice is traced) */
            const units_goal *gl = &goal[u->group];
            float dx = gl->x - u->s.x, dz = gl->z - u->s.z, d = sqrtf(dx * dx + dz * dz);
            float stop = u->leader ? 0.0f : 1500.0f;
            if (d > stop + 50.0f) {
                float want = atan2f(dx, dz) * 180.0f / PI_F;
                float diff = fmodf(want - u->s.heading + 540.0f, 360.0f) - 180.0f;
                turn = diff > 2 ? 1.0f : diff < -2 ? -1.0f : diff / 2.0f;
                throttle = fabsf(diff) < 45.0f ? 1.0f : 0.3f;
            }
        }
        sim_gear_tick(&u->s, throttle * u->cb.speed_factor, turn, ground_at, w);
        if (u->target < 0 || !c->gr.nweapons) continue;
        /* INFERRED: the aim wanders by up to 2% of the range (the original's AI skill is not traced; without some
         * spread every AI shot strikes the same part) */
        static unsigned seed = 12345u;
        float spread = 0.02f * sqrtf(best), jit[3];
        for (int q = 0; q < 3; q++) {
            seed = seed * 1103515245u + 12345u;
            jit[q] = ((float)((seed >> 16) & 0x7fff) / 32767.0f * 2.0f - 1.0f) * spread;
        }
        float eye[3] = {u->s.x, u->s.y + 100.0f, u->s.z}, aim[3] = {tx + jit[0], ty + 50.0f + jit[1], tz + jit[2]};
        c->lock = u->target;   /* INFERRED: an AI locks what it shoots at */
        mount_ctx mc = {u};
        combat_weapons_tick(c, i, trig, eye, aim, unit_mount, &mc, s);
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
    u->player = 1;
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
    if (gearmodel_load(a, gdf, &u->gm) || combat_init(&u->cb, a, gdf, 6.0f)) return -1;
    u->gps = -1;
    u->group = 0; u->slot = slot; u->leader = 0;
    u->voice = voice;
    u->side = m->group_side[0];
    u->target = -1;
    u->order_target = -1;
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
    int first = -1;
    for (int i = 0; i < us->n; i++) {
        const unit *u = &us->u[i];
        int g = u->player ? 0 : u->group;
        if (g != group || !u->placed) continue;
        if (u->leader || u->player) return i;   /* the group's leader index stands, alive or not (0x107c8ee0) */
        if (first < 0 && !u->cb.dead) first = i;
    }
    return first;
}
