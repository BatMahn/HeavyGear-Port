#include "combat.h"

#include "gear_tables.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GRAVITY 0.0296f   /* 0x10110088 */

int combat_init(combatant *c, hg_archive *a, const char *gdf, float internal_scale)
{
    memset(c, 0, sizeof *c);
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

static void spawn(combat_shots *s, int owner, int type, int power, const float from[3], const float to[3], int lock)
{
    const gear_weapon_stats *st = &gear_weapon_stat[type];
    float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    float l = sqrtf(dx * dx + dy * dy + dz * dz);
    if (l < 1e-3f) return;
    for (int i = 0; i < COMBAT_MAX_SHOTS; i++) {
        combat_shot *p = &s->shot[i];
        if (p->active) continue;
        p->active = 1;
        p->owner = owner;
        p->type = type;
        p->power = power;
        p->x = from[0]; p->y = from[1]; p->z = from[2];
        p->vx = dx / l * st->speed; p->vy = dy / l * st->speed; p->vz = dz / l * st->speed;
        p->ax = p->az = 0;
        p->ay = -GRAVITY * (float)st->gravity;
        p->life = st->life;
        if (st->beam) {
            /* a beam (0x100baf31 / 0x10018a9c): the ray along the aim is tested at once, struck within the weapon's
             * reach (+0x50); here one tick's flight over that reach, straight (INFERRED: one hit a beam) */
            float reach = st->max_range > 0 ? st->max_range : st->speed * (float)st->life;
            p->vx = dx / l * reach; p->vy = dy / l * reach; p->vz = dz / l * reach;
            p->ay = 0;
            p->life = 1;
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
    int fired = 0, pulled = trigger && !c->trigger_was;
    c->trigger_was = trigger;
    for (int k = 0; k < c->gr.nweapons; k++) {
        int t = c->gr.weapon[k].type;
        if (c->state[k] < 0 || t < 0 || t >= GEAR_WEAPON_TYPES) continue;
        const gear_weapon_stats *st = &gear_weapon_stat[t];
        int selected = k == c->sel;
        switch (c->state[k]) {
        case 1:
            if (!(selected && pulled)) break;
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
            if (--c->ammo[k] <= 0) { c->state[k] = -1; break; }
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
        c->dead = 1;   /* gears and infantry (class 1 / 9); other vehicles lose the part (INFERRED: all die) */
        break;
    case 2: case 7:
        destroy_zone(c, 3); destroy_zone(c, 4); destroy_zone(c, 1);
        c->dead = 1;
        break;
    case 3: case 4:
        break;
    case 5: case 6:
        c->crippled = 1;          /* gears and infantry fall and crawl (0x1005ab68: +0x500 = 0) */
        c->speed_factor = 0;
        break;
    case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16:
        for (int k = 0; k < c->gr.nweapons; k++) if (c->gr.weapon[k].kind == zone) c->state[k] = -1;
        break;
    default:
        c->dead = 1;
    }
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
    case 0x1b8a: destroy_zone(c, zone); break;
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
    case 0x1e46: say(c, 0xc, 2); break;                                 /* torso (its twist stops) */
    case 0x1e78: say(c, 0xd, 2); break;
    case 0x1eaa: if (c->gr.power_spare) return; say(c, 0x2a, -1); break;   /* +0xbbf: the torso's field 2 is 9 */
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
    if (c->dead || zone < 1 || zone > GEAR_ZONES) return 0;
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
            int q = rand() % 100;
            int i = (q < 5 ? -5 : q < 0x19 ? -1 : q < 0x4b ? 0 : q < 0x5f ? 1 : 5) + amount / 0x32;
            if (i >= c->crit_n[zone]) i = c->crit_n[zone];
            if (i >= 0) crit(c, zone, i);
        }
    }
    return 0;
}

int combat_shots_tick(combat_shots *s, const combat_world *w)
{
    int hits = 0;
    for (int i = 0; i < COMBAT_MAX_SHOTS; i++) {
        combat_shot *p = &s->shot[i];
        if (!p->active) continue;
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
        if (strike) {
            combatant *c = w->victim(w->ctx, p->target);
            float at[3] = {p->x, p->y, p->z};
            int pen = -1;
            if (c) {
                int alive = !c->dead;
                /* INFERRED: the zone, the torso (the shot's hit with 0x8000 isn't followed to its zone) */
                if (combat_damage(c, p->type, p->power, 2) && s->ev.killed) s->ev.killed(s->ev.ctx, p->target, p->owner);
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
