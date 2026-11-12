#include "combat.h"
#include "hgrand.h"

#include "gear_tables.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GRAVITY 0.0296f   /* 0x10110088 */

int combat_init(combatant *c, hg_archive *a, const char *gdf, float internal_scale)
{
    memset(c, 0, sizeof *c);
    c->vclass = 1;
    c->strike_zone = 2;
    if (gear_load(a, gdf, &c->gr)) return -1;
    for (int z = 0; z < GEAR_ZONES; z++) {
        c->armor[z] = c->gr.armor[z];
        c->internal[z] = c->gr.internal[z] * internal_scale;
        int t = (int)((c->internal[z] * 0.5f + c->armor[z] * 0.5f) / 5.0f);
        c->ztotal[z] = t > 0xffff ? 0xffff : t;
    }
    for (int k = 0; k < c->gr.nweapons; k++) {
        int t = c->gr.weapon[k].type;
        c->ammo[k] = c->gr.weapon[k].ammo;
        c->state[k] = t >= 0 && t < GEAR_WEAPON_TYPES && c->ammo[k] > 0 ? 1 : -1;
    }
    combat_crits_init(c);
    c->lock = -1;
    c->sel = 0;
    while (c->sel < c->gr.nweapons && c->state[c->sel] < 0) c->sel++;
    if (c->sel >= c->gr.nweapons) c->sel = 0;
    return 0;
}

void combat_crits_init(combatant *c)
{
    c->speed_factor = c->sensor_factor = 1.0f;
    c->hyd_spare = c->gr.version == 2;   /* +0xbbb: any arm or leg record (0x1003d9c9 / 0x1003dd38 give 1) */
    /* 0x1004e320: zone z reads slot list z, filled from group gear_crit_group[z] (z = 1..16) */
    if (c->gr.version == 2 && c->gr.crit_type > 0 && c->gr.crit_type < GEAR_CRIT_TYPES)
        for (int z = 1; z <= 16; z++) {
            int g = gear_crit_group[z];
            c->crit_n[z] = gear_crit[c->gr.crit_type][g].n;
            for (int i = 0; i < c->crit_n[z] && i < 20; i++) c->crit[z][i] = gear_crit[c->gr.crit_type][g].id[i];
        }
}

void combat_cycle(combatant *c)
{
    for (int k = 1; k <= c->gr.nweapons; k++) {
        int n = (c->sel + k) % (c->gr.nweapons ? c->gr.nweapons : 1);
        if (c->state[n] >= 0) { c->sel = n; return; }
    }
}

/* the weapon's +0x76 (0x101192f0 + type x 0xd6): 3 for the hand grenades (types 0x1c, 0x26, 0x27, kind 0x3d), whose
 * shells bounce (0x10018975 runs 0x100191d7 for them, 0x10019f1d for the 0s) */
static int weapon_motion(int type)
{
    return type == 0x1c || type == 0x26 || type == 0x27 ? 3 : 0;
}

static void spawn(combat_shots *s, int owner, int type, int power, const float from[3], const float to[3], int lock)
{
    const gear_weapon_stats *st = &gear_weapon_stat[type];
    float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    float l = sqrtf(dx * dx + dy * dy + dz * dz);
    if (l < 1e-3f) return;
    if (!st->beam && s->pool_cap) {
        /* 0x100bb425: a free object of the kind's pool (the shot table's entries of that kind not in flight) */
        int cap = s->pool_cap(s->pool_ctx, st->kind), busy = 0;
        for (int i = 0; i < COMBAT_MAX_SHOTS; i++)
            if (s->shot[i].active && !s->shot[i].beam && gear_weapon_stat[s->shot[i].type].kind == st->kind) busy++;
        if (busy >= cap) return;
    }
    float off = 0;
    if (!st->beam && s->pool_radius && !(st->flags & 0x40)) off = 2.0f * s->pool_radius(s->pool_ctx, st->kind);
    for (int i = 0; i < COMBAT_MAX_SHOTS; i++) {
        combat_shot *p = &s->shot[i];
        if (p->active) continue;
        p->active = 1;
        p->owner = owner;
        p->type = type;
        p->power = power;
        /* 0x100bd1e7: the object is moved twice its model's radius along the aim unless the weapon's +8 has 0x40 */
        p->x = from[0] + dx / l * off; p->y = from[1] + dy / l * off; p->z = from[2] + dz / l * off;
        if (st->kind == 0x3e || st->kind == 0x3f) {
            /* 0x100bd1e7 (its 6th argument 1 for kinds 0x3e / 0x3f, 0x100baf31): r = rand(200) - 100; the side
             * s = (-dz, 0, dx) normalized (0x1006f915), u = (dy sz, sz dx + dz sx, -sx dy); the start moves by
             * r (u + s) (y by r u.y), d the aim's unit vector */
            float ux = dx / l, uy = dy / l, uz = dz / l;
            float sx = -uz, sz = ux, sl = sqrtf(sx * sx + sz * sz);
            if (sl > 1e-6f) { sx /= sl; sz /= sl; }
            float r = (float)(rand() % 200 - 100);
            float vx = uy * sz, vy = sz * ux + uz * sx, vz = -(sx * uy);
            p->x += r * vx + r * sx;
            p->y += r * vy;
            p->z += r * vz + r * sz;
        }
        p->serial = ++s->serial;
        p->flags = 0;
        p->motion = weapon_motion(type);
        p->vx = dx / l * st->speed; p->vy = dy / l * st->speed; p->vz = dz / l * st->speed;
        p->ax = p->az = 0;
        p->ay = -GRAVITY * (float)st->gravity;
        p->life = st->life;
        p->beam = st->beam;
        if (st->beam) {
            /* a beam (0x100baf31): it takes a slot of the beam pool, its life the weapon's +0x5c less the weapon
             * slot's +0x10 (0 here: that field isn't traced), its ray the vehicle's mount along its aim; the tests
             * are combat_beams_frame's */
            p->dir[0] = dx / l; p->dir[1] = dy / l; p->dir[2] = dz / l;
            p->vx = p->vy = p->vz = p->ay = 0;
        }
        p->target = (st->sflags & 1) ? lock : -1;   /* 0x100bb425: a guided weapon takes the slot's target */
        p->age = 0;
        p->arm = (st->sflags & 0x21) == 0x21 ? -1.0f : 0.0f;
        if (s->ev.fire) s->ev.fire(s->ev.ctx, owner, type, from);
        return;
    }
}

int combat_weapons_tick(combatant *c, int owner, int trigger, const float from[3], const float to[3],
                        combat_mount_fn mount, void *mctx, combat_shots *s)
{
    if (c->dead) return 0;
    if (c->stun > 0) trigger = 0;   /* 0x100bb425: no firing while the power is out (+0xc3d) */
    int fired = 0, pulled = trigger && !c->trigger_was;
    c->trigger_was = trigger;
    for (int k = 0; k < c->gr.nweapons; k++) {
        int t = c->gr.weapon[k].type;
        if (c->state[k] < 0 || t < 0 || t >= GEAR_WEAPON_TYPES) continue;
        const gear_weapon_stats *st = &gear_weapon_stat[t];
        int selected = k == c->sel;
        int in_group = c->fire_all && c->sel >= 0 && c->sel < c->gr.nweapons && c->wgroup[k] == c->wgroup[c->sel] &&
                       (st->flags & 4);
        switch (c->state[k]) {
        case 1:
            if (!((selected || in_group) && pulled)) break;
            c->state[k] = 2; c->left[k] = st->burst; c->timer[k] = 0;
            /* the pull's tick counts toward the first shot (0x100ba550 starts the count that tick) */
            /* fall through */
        case 2:
            if (++c->timer[k] < st->interval) break;
            c->timer[k] = 0;
            {
                float f[3] = {from[0], from[1], from[2]};
                if (mount) mount(mctx, c->gr.weapon[k].kind, f);
                spawn(s, owner, t, st->power + c->gr.weapon[k].power_bonus, f, to, c->lock);
                fired++;
            }
            if (!c->unlimited_ammo && --c->ammo[k] <= 0) { c->state[k] = -1; break; }
            if (--c->left[k] <= 0) { c->state[k] = 3; c->timer[k] = 0; }
            break;
        case 3:
            if (++c->timer[k] < st->reload) break;
            if (selected && trigger && st->autofire) { c->state[k] = 2; c->left[k] = st->burst; c->timer[k] = 0; }
            else c->state[k] = 1;
            break;
        }
    }
    return fired;
}

static void destroy_zone(combatant *c, int zone)
{
    if (zone < 1 || zone > GEAR_ZONES || c->zone_gone[zone - 1]) return;
    c->zone_gone[zone - 1] = 1;
    c->armor[zone - 1] = c->internal[zone - 1] = 0;
    switch (zone) {
    case 1:
        if (c->vclass == 1 || c->vclass == 9) c->dead = 1;   /* other vehicles only lose the part (0x1005ab68) */
        break;
    case 2: case 7:
        destroy_zone(c, 3); destroy_zone(c, 4); destroy_zone(c, 1);
        c->dead = 1;
        break;
    case 3: case 4:
        break;
    case 5: case 6:
        c->crippled = 1;          /* 0x1005ab68: +0x500 = 0, +0x560 | 0x20 */
        c->speed_factor = 0;
        c->legs_gone = 1;
        if (c->vclass == 1 || c->vclass == 9) c->fall |= hg_rand(2) == 0 ? 4u : 0x400u;   /* +0xc19 2 / 1 */
        if (c->vclass == 1) c->fall |= zone == 5 ? 0x4000u : 0x2000u;
        break;
    case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16:
        /* version 1 records carry kinds 0x21..0x2c: their zone (0x1003cd60's table) */
        for (int k = 0; k < c->gr.nweapons; k++) {
            int wk = c->gr.weapon[k].kind;
            static const int v1[12] = {14, 16, 13, 15, 0, 11, 9, 0, 12, 10, 0, 8};   /* 0x21..0x2c */
            if (wk > 16) wk = wk >= 0x21 && wk <= 0x2c ? v1[wk - 0x21] : 0;
            if (wk == zone) c->state[k] = -1;
        }
        break;
    default:
        c->dead = 1;
    }
}

void combat_destroy_zone(combatant *c, int zone)
{
    destroy_zone(c, zone);
}

static void say(combatant *c, int subject, int verdict)
{
    if (c->report) c->report(c->report_ctx, subject, verdict);
}

/* 0x1005bfca: the computer's name for a destroyed weapon, by weapon type */
static int weapon_subject(int type)
{
    switch (type) {
    case 0: case 1: return 0x12;
    case 2: case 3: case 4: return 0x13;
    case 5: case 7: case 0x2b: return 0x17;
    case 6: return 0x1e;
    case 8: return 0x1a;
    case 9: case 10: case 11: return 0x1c;
    case 0xc: case 0xd: case 0xe: return 0x19;
    case 0x1b: case 0x29: case 0x2a: return 0x1f;
    case 0x1c: case 0x26: case 0x27: return 0x18;
    case 0x1d: case 0x1e: case 0x1f: case 0x20: return 0x1b;
    case 0x21: case 0x22: case 0x23: case 0x28: return 0x14;
    case 0x24: case 0x2c: return 0x16;
    case 0x25: case 0x2d: return 0x15;
    }
    return type >= 0xf && type <= 0x1a ? 0x1d : -1;
}

/* 0x1005b5c6: crit slot `i` of `zone` */
static void crit(combatant *c, int zone, int i)
{
    if (zone < 1 || zone > 16 || i < 0 || i >= 20 || c->dead) return;
    int id = c->crit[zone][i];
    switch (id) {
    case 0:
        if (i < c->crit_n[zone]) crit(c, zone, i + 1);
        break;
    case 0x1b8a: c->crit_zones++; destroy_zone(c, zone); break;   /* the player's: HGCAR +0x138 (0x1005b5c6) */
    case 0x1bee:   /* low-light (head option bit 1, +0xbaf) and satellite uplink (bit 7, +0xbb3) */
        if (c->gr.head_flags & 2) { say(c, 1, 3); c->gr.head_flags &= ~2; }
        if (c->gr.head_flags & 0x80) { say(c, 0x10, 3); c->gr.head_flags &= ~0x80; }
        break;
    case 0x1c20:   /* IR (bit 0, +0xbab) and the autopilot (bit 4, +0xc35) */
        if (c->gr.head_flags & 1) { say(c, 0, 3); c->gr.head_flags &= ~1; }
        if (c->gr.head_flags & 0x10) { say(c, 2, 3); c->gr.head_flags &= ~0x10; }
        break;
    case 0x1c52: say(c, 5, 3); break;                                   /* camera */
    case 0x1c84: say(c, 6, 2); c->speed_factor *= 0.9f; break;          /* engine */
    case 0x1cb6: say(c, 6, 2); c->speed_factor *= 0.5f; break;
    case 0x1ce8: say(c, 7, -1); c->speed_factor = 0; break;
    case 0x1d1a:   /* sensors: a backup (head bit 6, +0xbc3) shrugs it off, the slot stays */
        if (c->gr.head_flags & 0x40) return;
        say(c, 4, 2); c->sensor_factor *= 0.5f; c->gr.sensors >>= 1; break;
    case 0x1d4c: say(c, 4, 3); c->sensor_factor = 0; c->gr.sensors = 0; break;
    case 0x1d7e: say(c, 8, 2); break;                                   /* arm actuator */
    case 0x1db0: say(c, 9, 2); c->speed_factor *= 0.9f; break;          /* leg actuator */
    case 0x1de2:   /* hydraulics: the first hit on a gear with limbs is spared (+0xbbb), the slot stays */
        if (c->hyd_spare) { c->hyd_spare = 0; return; }
        say(c, 0x28, 2); c->speed_factor *= 0.9f; break;
    case 0x1e14: say(c, 0x29, -1); break;
    case 0x1e46: say(c, 0xc, 2); if (c->vclass == 1 || c->vclass == 9) c->knock = 1; break;   /* +0xc15: knocked down */
    case 0x1e78: say(c, 0xd, 2); break;
    case 0x1eaa: if (c->gr.power_spare) return; say(c, 0x2a, -1); c->stun = 0x21f; break;   /* +0xbbf: the torso's field 2
                                                                                     * is 9; +0xc3d = 0x21f */
    case 8000: if (c->gr.head_flags & 0x800) return; say(c, 0xf, 2); break;   /* cockpit; head bit 11 (+0xbb7) */
    case 9000:                                                          /* a weapon in this zone (0x1005bfca) */
        for (int k = 0; k < c->gr.nweapons; k++)
            if (c->gr.weapon[k].kind == zone && c->state[k] >= 0) {
                int sj = weapon_subject(c->gr.weapon[k].type);
                if (sj >= 0) say(c, sj, 3);
                c->state[k] = -1;
                break;
            }
        break;
    }
    c->crit[zone][i] = 0;
}

int combat_damage(combatant *c, int type, int power, int zone)
{
    if (type < 0 || type >= GEAR_WEAPON_TYPES) return 0;
    const gear_weapon_stats *st = &gear_weapon_stat[type];
    return combat_damage_raw(c, power, st->mult, st->chip, st->mode, zone);
}

int combat_damage_raw(combatant *c, int power, float mult, int chip, int mode, int zone)
{
    if (c->dead || c->invulnerable) return 0;
    /* 0x1005c1f8: zones 1..0x11 (a part of zone 0 stops the shot, unharmed); the legs (5 / 6) only on gears and
     * infantry */
    if (zone < 1 || zone > GEAR_ZONES || ((zone == 5 || zone == 6) && c->vclass != 1 && c->vclass != 9)) {
        c->pierced = 0;
        return 0;
    }
    struct { float mult; int chip; } stv = {mult, chip}, *st = &stv;
    float *ar = &c->armor[zone - 1], *in = &c->internal[zone - 1];
    int pen = (mode == 3 || mode == 4) ? (int)((float)power - *ar / 2.0f) : (int)((float)power - *ar);
    const float eps = 1e-7f;   /* 0x101012b4 */
    c->pierced = pen >= 1;
    int inner = 0, amount = pen;   /* bVar2: the internal points were reached; local_c */
    if (pen < 1) {
        if (*ar >= eps) {
            *ar -= (float)st->chip;
            if (*ar < eps) { *in += *ar; *ar = 0; inner = 1; }
        } else {
            *ar = 0;
            *in -= (float)st->chip;
            inner = 1;
        }
    } else {
        int d = (int)((float)pen * st->mult);
        amount = d;
        if (*ar >= eps) {
            switch (mode) {
            case 1: *ar -= (float)d; break;
            case 2: *ar -= (float)d / 2.0f; *in -= (float)d / 2.0f; inner = 1; break;
            case 3: case 4: *in -= (float)d; inner = 1; break;
            }
            if (*ar < eps) { *in += *ar; *ar = 0; }
        } else {
            *ar = 0;
            *in -= (float)d;
            inner = 1;
        }
    }
    if (*in <= 0.0f) {
        int was = c->dead;
        destroy_zone(c, zone);
        return !was && c->dead;
    }
    /* two crit rolls when the internal points were struck (0x1005c1f8): slot -5 / -1 / 0 / 1 / 5 by 5 / 20 / 50 / 20 /
     * 5 percent, plus the damage / 50, held to the zone's count */
    if (inner && zone <= 16 && c->crit_n[zone] > 0) {
        for (int r = 0; r < 2 && !c->dead; r++) {
            int q = hg_rand(100);   /* 0x10089e69 */
            int i = (q < 5 ? -5 : q < 0x19 ? -1 : q < 0x4b ? 0 : q < 0x5f ? 1 : 5) + amount / 0x32;
            if (i >= c->crit_n[zone]) i = c->crit_n[zone];
            if (i >= 0) crit(c, zone, i);
        }
    }
    return 0;
}

/* A bouncing shell's step (0x100191d7): a vehicle struck takes the hit and ends it; an object flagged 0x800 (the
 * ground's tiles: here also the ground itself) halves its x and z speed and turns back and halves its y speed, flags
 * the shell 0x400 and throws a spray (0x1008514d); 0x400 flags it 0x200; any other object (0x200 or none) turns its
 * x and z speed back. It moves on while above the ground. Its life run out, it strikes where it is (0x1001b224 with
 * its effect) */
static int bounce_tick(combat_shots *s, const combat_world *w, combat_shot *p, const float p0[3], const float p1[3])
{
    float t;
    int zone, v = w->hit(w->ctx, p->owner, p0, p1, &t, &zone);
    combatant *c = v >= 0 ? w->victim(w->ctx, v) : NULL;
    if (c) {
        float at[3] = {p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t, p0[2] + (p1[2] - p0[2]) * t};
        int alive = !c->dead;
        if (combat_damage(c, p->type, p->power, zone) && s->ev.killed) s->ev.killed(s->ev.ctx, v, p->owner);
        if (w->hit_vehicle) w->hit_vehicle(w->ctx, v, p->owner);
        if (s->ev.impact) s->ev.impact(s->ev.ctx, p->owner, p->type, v, alive ? c->pierced : -1, at);
        p->active = 0;
        return 1;
    }
    float gy = w->ground_y(w->ctx, p1[0], p1[2]);
    unsigned of = 0;
    int struck = 0;
    if (v >= 0) { struck = 1; of = s->obj_flags ? s->obj_flags(s->pool_ctx, v) : 0; }
    else if (p1[1] < gy) { struck = 1; of = 0x800; }
    if (struck) {
        if (of & 0x800) {
            p->flags |= 0x400;
            p->vx /= 2.0f; p->vz /= 2.0f; p->vy = -(p->vy / 2.0f);
            if (s->ev.spray) { float vv[3] = {p->vx, p->vy, p->vz}; s->ev.spray(s->ev.ctx, p->owner, p->type, p0, vv); }
        } else if (of & 0x400) p->flags |= 0x200;
        else { p->vx = -p->vx; p->vz = -p->vz; }
    }
    if (gy < p1[1]) { p->x = p1[0]; p->y = p1[1]; p->z = p1[2]; }
    if (--p->life < 1) {
        p->active = 0;
        float at[3] = {p->x, p->y, p->z};
        /* the effect by what it last struck: -1 the ground (0x400), -3 nothing */
        if (s->ev.impact) s->ev.impact(s->ev.ctx, p->owner, p->type, (p->flags & 0x400) ? -1 : -3, -1, at);
        return 1;
    }
    return 0;
}

int combat_shots_tick(combat_shots *s, const combat_world *w)
{
    int hits = 0;
    for (int i = 0; i < COMBAT_MAX_SHOTS; i++) {
        combat_shot *p = &s->shot[i];
        if (!p->active) continue;
        if (p->beam) continue;
        /* guidance (0x1001aec9): the acceleration is the unit vector to the target less the unit vector of the
         * velocity (vertical only for shots not kept level, +0x7e bit 0x20); within 3000 the speed drops 5% a tick;
         * within 100 the shot strikes the target (0x8000) */
        int strike = 0;
        p->age++;
        if (p->target >= 0 && w->target_pos) {
            float tp[3];
            int there = w->target_pos(w->ctx, p->target, tp);
            if (there && p->arm < 0) {
                /* 0x100af2a0 (at the firing): the time the launch velocity's parabola takes to reach the target's
                 * height, the later root first; none (a negative square) never arms */
                float a = -GRAVITY * (float)gear_weapon_stat[p->type].gravity * 0.5f, h = tp[1] - p->y;
                float disc = p->vy * p->vy - 4.0f * a * h;
                if (disc < 0 || a == 0) p->arm = 1e30f;
                else {
                    float t = (-p->vy - sqrtf(disc)) / (2.0f * a);
                    if (t < 0) t = (sqrtf(disc) - p->vy) / (2.0f * a);
                    p->arm = t;
                }
            }
            if (!there) { p->target = -1; p->ax = p->az = 0; p->ay = -GRAVITY * (float)gear_weapon_stat[p->type].gravity; }
            else if (!(p->arm < (float)p->age)) {
                /* not armed yet (0x100191d7 steers only once +0x82 < the age): a plain shot */
            } else {
                float dx = tp[0] - p->x, dy = tp[1] - p->y, dz = tp[2] - p->z, d = sqrtf(dx * dx + dy * dy + dz * dz);
                float sp = sqrtf(p->vx * p->vx + p->vy * p->vy + p->vz * p->vz);
                if (d <= 100.0f) strike = 1;
                if (d > 1e-3f && sp > 1e-3f) {
                    if (d < 3000.0f) { p->vx *= 0.95f; p->vy *= 0.95f; p->vz *= 0.95f; }
                    p->ax = dx / d - p->vx / sp;
                    if (!(gear_weapon_stat[p->type].sflags & 0x20)) p->ay = dy / d - p->vy / sp;
                    p->az = dz / d - p->vz / sp;
                }
            }
        }
        float p0[3] = {p->x, p->y, p->z};
        float p1[3] = {p->x + p->vx + p->ax * 0.5f, p->y + p->vy + p->ay * 0.5f, p->z + p->vz + p->az * 0.5f};
        p->vx += p->ax; p->vy += p->ay; p->vz += p->az;
        if (p->motion == 3 && !strike) {
            hits += bounce_tick(s, w, p, p0, p1);
            continue;
        }
        if (strike) {
            combatant *c = w->victim(w->ctx, p->target);
            float at[3] = {p->x, p->y, p->z};
            int pen = -1;
            if (c) {
                int alive = !c->dead;
                /* 0x100191d7: a strike with 0x8000 takes the vehicle's root object, its zone (+0x26) */
                if (combat_damage(c, p->type, p->power, c->strike_zone) && s->ev.killed) s->ev.killed(s->ev.ctx, p->target, p->owner);
                pen = alive ? c->pierced : -1;
                if (w->hit_vehicle) w->hit_vehicle(w->ctx, p->target, p->owner);
            }
            if (s->ev.impact) s->ev.impact(s->ev.ctx, p->owner, p->type, p->target, pen, at);
            p->active = 0;
            hits++;
            continue;
        }
        float t;
        int zone, v = w->hit(w->ctx, p->owner, p0, p1, &t, &zone);
        if (v >= 0) {
            float at[3] = {p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t, p0[2] + (p1[2] - p0[2]) * t};
            combatant *c = w->victim(w->ctx, v);
            int pen = -1;
            if (c) {
                int alive = !c->dead;
                if (combat_damage(c, p->type, p->power, zone) && s->ev.killed) s->ev.killed(s->ev.ctx, v, p->owner);
                pen = alive ? c->pierced : -1;
                if (w->hit_vehicle) w->hit_vehicle(w->ctx, v, p->owner);
            } else if (w->hit_other) w->hit_other(w->ctx, v, p->owner, p->power);
            if (s->ev.impact) s->ev.impact(s->ev.ctx, p->owner, p->type, v, pen, at);
            p->active = 0;
            hits++;
            continue;
        }
        p->x = p1[0]; p->y = p1[1]; p->z = p1[2];
        if (p->y < w->ground_y(w->ctx, p->x, p->z)) {
            p->active = 0;
            if (s->ev.impact) { float at[3] = {p->x, p->y, p->z}; s->ev.impact(s->ev.ctx, p->owner, p->type, -1, -1, at); }
        } else if (--p->life <= 0) p->active = 0;
    }
    if (!s->beams_by_frame) hits += combat_beams_frame(s, w, 1);
    return hits;
}

int combat_beams_frame(combat_shots *s, const combat_world *w, int ticks)
{
    int hits = 0;
    for (int i = 0; i < COMBAT_MAX_SHOTS; i++) {
        combat_shot *p = &s->shot[i];
        if (!p->active || !p->beam) continue;
        p->age += ticks;
        p->life -= ticks;
        if (p->life < 1) { p->active = 0; continue; }
        const gear_weapon_stats *st = &gear_weapon_stat[p->type];
        if (st->kind == 4 || st->kind == 6) p->active = 0;   /* one test */
        if (w->beam_ray) {
            float f[3], d[3];
            if (!w->beam_ray(w->ctx, p->owner, p->type, f, d)) { p->active = 0; continue; }
            float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            p->x = f[0]; p->y = f[1]; p->z = f[2];
            if (l > 1e-6f) { p->dir[0] = d[0] / l; p->dir[1] = d[1] / l; p->dir[2] = d[2] / l; }
        }
        float reach = st->max_range > 0 ? st->max_range : st->speed * (float)st->life;
        float p0[3] = {p->x, p->y, p->z};
        float p1[3] = {p->x + p->dir[0] * reach, p->y + p->dir[1] * reach, p->z + p->dir[2] * reach};
        float t;
        int zone, v = w->hit(w->ctx, p->owner, p0, p1, &t, &zone);
        /* the scene test (0x100229d9) meets the ground's tiles too: the ground cuts the ray short */
        float tg = 2;
        for (int k = 1; k <= 64; k++) {
            float f = (float)k / 64.0f;
            if (p0[1] + (p1[1] - p0[1]) * f < w->ground_y(w->ctx, p0[0] + (p1[0] - p0[0]) * f, p0[2] + (p1[2] - p0[2]) * f)) {
                tg = (float)(k - 1) / 64.0f;
                break;
            }
        }
        float te = v >= 0 && t < tg ? t : tg < 2 ? tg : 1;
        for (int k = 0; k < 3; k++) p->end[k] = p0[k] + (p1[k] - p0[k]) * te;
        if (v < 0 || tg < t) continue;
        float at[3] = {p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t, p0[2] + (p1[2] - p0[2]) * t};
        combatant *c = w->victim(w->ctx, v);
        if (c) {
            int alive = !c->dead;
            if (combat_damage(c, p->type, p->power, zone) && s->ev.killed) s->ev.killed(s->ev.ctx, v, p->owner);
            if (w->hit_vehicle) w->hit_vehicle(w->ctx, v, p->owner);
            if (s->ev.impact) s->ev.impact(s->ev.ctx, p->owner, p->type, v, alive ? c->pierced : -1, at);
            hits++;
        } else if (w->hit_other) w->hit_other(w->ctx, v, p->owner, p->power);
    }
    return hits;
}

/* 0x1001b3a1's choice of effect for a shot's kind and what it struck */
int combat_effect(int kind, unsigned flags, int cockpit)
{
    int veh = (flags & COMBAT_HIT_VEHICLE) && !((flags & COMBAT_HIT_PLAYER) && cockpit);
    int k = kind & 0xff;
    if (k < 0 || k >= 0x4d) return -1;
    switch (k) {
    case 1: case 2: case 6: return flags & COMBAT_HIT_VEHICLE ? 0x4b : k;
    case 4: case 0x39: case 0x3c: return flags & COMBAT_HIT_VEHICLE ? 0x4a : k;
    case 5: return veh ? 0x2a : flags & COMBAT_HIT_GROUND ? 9 : flags & COMBAT_HIT_CORE ? 0x12 : 5;
    case 0xb: case 0x25: case 0x26: return -1;   /* 0x1001ce0a: its own effect, no sound here */
    case 0x3b: case 0x3f: return veh ? 0x32 : flags & COMBAT_HIT_GROUND ? 0x32 : k;
    case 0x3d: case 0x40: case 0x41: case 0x42: case 0x45: return veh ? 0x2a : flags & COMBAT_HIT_GROUND ? 0x2c : k;
    case 0x3e: return veh ? 0x2f : flags & COMBAT_HIT_GROUND ? 0x34 : k;
    case 0x43: case 0x46: case 0x47: case 0x48: case 0x44: return veh ? 0x2a : flags & COMBAT_HIT_GROUND ? 0x2a : k;
    }
    return k;
}

/* 0x1001b3a1's light flag (local_68, then 0x1004b275 when the effect found a slot): kinds 1 / 2 / 6, 0x24, 0x3d /
 * 0x40 / 0x41 / 0x42 / 0x44 / 0x45 always; 0x3b, 0x3e, 0x3f, 0x43 / 0x46 / 0x47 / 0x48 only off a vehicle and on a
 * strike flagged 0x400 */
int combat_effect_light(int kind, unsigned flags, int cockpit)
{
    int veh = (flags & COMBAT_HIT_VEHICLE) && !((flags & COMBAT_HIT_PLAYER) && cockpit);
    switch (kind & 0xff) {
    case 1: case 2: case 6: case 0x24: case 0x3d: case 0x40: case 0x41: case 0x42: case 0x44: case 0x45:
        return 1;
    case 0x3b: case 0x3e: case 0x3f: case 0x43: case 0x46: case 0x47: case 0x48:
        return !veh && (flags & COMBAT_HIT_GROUND) != 0;
    }
    return 0;
}

/* 0x1001b3a1: the vehicle branches of kinds 5, 0x3b and 0x3d..0x48 lower the effect by 300 (0x100eb4a8) */
float combat_effect_drop(int kind, unsigned flags, int cockpit)
{
    int veh = (flags & COMBAT_HIT_VEHICLE) && !((flags & COMBAT_HIT_PLAYER) && cockpit);
    int k = kind & 0xff;
    return veh && (k == 5 || k == 0x3b || (k >= 0x3d && k <= 0x48)) ? 300.0f : 0.0f;
}

void combat_seeker_tick(combat_seeker *s, int type, int has_target, float dist, float yaw_err, float pitch_err,
                        int fast, int dt)
{
    s->locked = 0;
    if (type < 0 || type >= GEAR_WEAPON_TYPES || !has_target || !(gear_weapon_stat[type].sflags & 1)) {
        s->in_cone = 0;
        s->acquiring = 0;
        return;
    }
    const gear_weapon_stats *st = &gear_weapon_stat[type];
    int band = !(dist <= st->min_range) && !(dist > st->max_range);
    int level = (st->sflags & 0x21) == 0x21;
    if (!band || ((fabsf(yaw_err) >= 16.0f || fabsf(pitch_err) >= 16.0f) && !level)) {
        s->in_cone = 0;
        if (s->acquiring && (s->timer += dt) > 0x169) s->acquiring = 0;
        return;
    }
    s->in_cone = 1;
    if (!s->acquiring) {
        s->acquiring = 1;
        s->timer = 0x16a;
    } else if (s->timer < 1 || fast) {
        s->timer = 0;
        s->locked = 1;
    } else {
        s->timer -= dt;
    }
}
