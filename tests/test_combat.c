/*
 * test_combat - the damage rules (0x1005c1f8 / 0x1005ab68) and the weapon slot timing (0x100ba550), on made-up
 * vehicles with the AI Hunter's zones as the running original holds them.
 */
#include "../src/combat.h"
#include "../src/gear_tables.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void hunter(combatant *c)
{
    static const float ar[GEAR_ZONES] = {9, 85, 45, 45, 40, 40, 60, 0, 0, 180, 0, 50};
    static const float in[GEAR_ZONES] = {120, 250, 350, 350, 500, 500, 450, 0, 0, 200, 0, 100};
    memset(c, 0, sizeof *c);
    c->vclass = 1;
    c->strike_zone = 2;
    memcpy(c->armor, ar, sizeof ar);
    memcpy(c->internal, in, sizeof in);
    c->gr.nweapons = 1;
    c->gr.weapon[0].type = 2;   /* LAC */
    c->gr.weapon[0].kind = 10;
    c->gr.weapon[0].ammo = 240;
    c->ammo[0] = 240;
    c->state[0] = 1;
}

static int none(void *ctx, int owner, const float p0[3], const float p1[3], float *t, int *zone)
{
    (void)ctx; (void)owner; (void)p0; (void)p1; (void)t; (void)zone;
    return -1;
}
static float flat(void *ctx, float x, float z) { (void)ctx; (void)x; (void)z; return -1e9f; }
static combatant *nobody(void *ctx, int id) { (void)ctx; (void)id; return NULL; }

static float tgt_at[3];
static int tpos(void *ctx, int id, float out[3]) { (void)ctx; (void)id; memcpy(out, tgt_at, sizeof tgt_at); return 1; }
static combatant guided_victim;
static combatant *gvic(void *ctx, int id) { (void)ctx; (void)id; return &guided_victim; }
static int always(void *ctx, int owner, const float p0[3], const float p1[3], float *t, int *zone)
{
    (void)ctx; (void)owner; (void)p0; (void)p1;
    *t = 0.01f; *zone = 2;
    return 1;
}

/* beams (0x10018a9c): a light laser cannon (type 33, kind 2) strikes every step of its life (181 ticks); a light rail
 * gun (type 36, kind 4) once */
static int beam_hits(int type)
{
    static combat_shots bs;
    memset(&bs, 0, sizeof bs);
    combatant sh;
    memset(&sh, 0, sizeof sh);
    sh.gr.nweapons = 1; sh.gr.weapon[0].type = type; sh.ammo[0] = 100; sh.state[0] = 1;
    memset(&guided_victim, 0, sizeof guided_victim);
    guided_victim.vclass = 1;
    guided_victim.internal[1] = 1e9f;
    float from[3] = {0, 0, 0}, to[3] = {0, 0, 1000};
    combat_world bw = {NULL, always, flat, gvic, NULL, NULL, NULL, NULL};
    int hits = 0, fired = 0;
    for (int t = 0; t < 400; t++) {
        if (!fired) fired = combat_weapons_tick(&sh, 0, 1, from, to, NULL, NULL, &bs);
        else hits += combat_shots_tick(&bs, &bw);
    }
    return hits;
}

/* the shells' pools: two of each kind, models of radius 50 */
static int cap2(void *ctx, int kind) { (void)ctx; (void)kind; return 2; }
static float rad50(void *ctx, int kind) { (void)ctx; (void)kind; return 50.0f; }
static float ground0(void *ctx, float x, float z) { (void)ctx; (void)x; (void)z; return 0.0f; }
static int sprays;
static void on_spray(void *ctx, int owner, int type, const float at[3], const float v[3])
{
    (void)ctx; (void)owner; (void)type; (void)at; (void)v;
    sprays++;
}
static int end_victim = 99;
static void on_impact(void *ctx, int owner, int type, int victim, int pen, const float at[3])
{
    (void)ctx; (void)owner; (void)type; (void)pen; (void)at;
    end_victim = victim;
}

static int reports[64];
static void rep(void *ctx, int subject, int verdict) { (void)ctx; (void)verdict; if (subject >= 0 && subject < 64) reports[subject]++; }

int main(void)
{
    combatant c;
    hunter(&c);
    /* LAC (power 85) on the torso (armor 85): no penetration, no chip damage */
    combat_damage(&c, 2, 85, 2);
    CHECK(c.armor[1] == 85 && c.internal[1] == 250, "LAC on torso: %g / %g", c.armor[1], c.internal[1]);
    /* MAC (105): 20 x 1.2 = 24 off the armor (mode 1) */
    combat_damage(&c, 3, 105, 2);
    CHECK(c.armor[1] == 61, "MAC on torso: armor %g", c.armor[1]);
    /* LAC on a leg (armor 40): 45 x 1.2 = 54; armor -14 spills into the internal */
    combat_damage(&c, 2, 85, 5);
    CHECK(c.armor[4] == 0 && c.internal[4] == 486, "LAC on leg: %g / %g", c.armor[4], c.internal[4]);
    /* then straight into the internal: 85 x 1.2 = 102 a hit */
    combat_damage(&c, 2, 85, 5);
    CHECK(c.internal[4] == 384, "LAC on bare leg: %g", c.internal[4]);
    /* the head (9 armor, 120 internal): 76 x 1.2 = 91 -> armor -82 -> internal 38; then 102 -> dead */
    int killed = combat_damage(&c, 2, 85, 1);
    CHECK(!killed && c.internal[0] == 38, "head 1: %g", c.internal[0]);
    killed = combat_damage(&c, 2, 85, 1);
    CHECK(killed && c.dead, "head 2 should kill");
    /* a ground vehicle (class 4) only loses its head part (0x1005ab68) */
    hunter(&c);
    c.vclass = 4;
    for (int i = 0; i < 5; i++) combat_damage(&c, 2, 85, 1);
    CHECK(c.zone_gone[0] && !c.dead, "vehicle head: gone %d dead %d", c.zone_gone[0], c.dead);
    /* an arm destroyed takes its zone only */
    hunter(&c);
    for (int i = 0; i < 20 && !c.zone_gone[2]; i++) combat_damage(&c, 4, 150, 3);
    CHECK(c.zone_gone[2] && !c.dead, "arm off, alive");
    /* the torso destroyed: arms, head and the vehicle */
    for (int i = 0; i < 50 && !c.dead; i++) combat_damage(&c, 4, 150, 2);
    CHECK(c.dead && c.zone_gone[0] && c.zone_gone[3], "torso kills");

    /* timing: LAC, interval 60, reload 60, burst 1, auto: shots at 60, 180, 300 ... ticks while held */
    hunter(&c);
    static combat_shots s;
    memset(&s, 0, sizeof s);
    float from[3] = {0, 0, 0}, to[3] = {0, 0, 1000};
    int ticks[8], n = 0;
    for (int t = 1; t <= 400 && n < 8; t++)
        if (combat_weapons_tick(&c, 0, 1, from, to, NULL, NULL, &s)) ticks[n++] = t;
    CHECK(n == 3 && ticks[0] == 60 && ticks[1] == 180 && ticks[2] == 300, "LAC shots at %d %d %d (%d)",
          n > 0 ? ticks[0] : -1, n > 1 ? ticks[1] : -1, n > 2 ? ticks[2] : -1, n);
    CHECK(c.ammo[0] == 237, "ammo %d", c.ammo[0]);
    {   /* a fire group (0x100bc53d): with fire_all a pull fires every ready weapon of the selected one's group */
        static combatant g;
        static combat_shots gs;
        hunter(&g);
        memset(&gs, 0, sizeof gs);
        g.gr.nweapons = 3;
        for (int k = 1; k < 3; k++) { g.gr.weapon[k] = g.gr.weapon[0]; g.ammo[k] = 240; g.state[k] = 1; }
        g.wgroup[0] = g.wgroup[1] = 1; g.wgroup[2] = 0;
        g.fire_all = 1;
        int f = 0;
        for (int t = 1; t <= 60; t++) f += combat_weapons_tick(&g, 0, 1, from, to, NULL, NULL, &gs);
        CHECK(f == 2 && g.ammo[0] == 239 && g.ammo[1] == 239 && g.ammo[2] == 240, "group fire %d", f);
    }
    /* a shot flies at 140 a tick for 560 ticks (range 78400), level */
    combat_world w = {NULL, none, flat, nobody, NULL, NULL, NULL, NULL};
    int live = 0;
    for (int i = 0; i < COMBAT_MAX_SHOTS; i++) live += s.shot[i].active;
    CHECK(live == 3, "live shots %d", live);
    const combat_shot *p = &s.shot[0];
    CHECK(fabsf(p->vz - 140.0f) < 1e-3f, "speed %g", p->vz);
    for (int t = 0; t < 1000; t++) combat_shots_tick(&s, &w);
    live = 0;
    for (int i = 0; i < COMBAT_MAX_SHOTS; i++) live += s.shot[i].active;
    CHECK(live == 0, "shots expire: %d left", live);
    CHECK(gear_weapon_stat[28].gravity == 4 && gear_weapon_stat[18].burst == 4, "table");

    {
        int l = beam_hits(33), r = beam_hits(36);
        CHECK(l == 180 && r == 1, "beam strikes: laser %d (want 180), rail gun %d (want 1)", l, r);
    }


    {
        /* 0x100bb425: a shell needs a free object of its kind's pool; 0x100bd1e7 starts it 2 x its model's radius
         * ahead */
        static combat_shots ps;
        memset(&ps, 0, sizeof ps);
        ps.pool_cap = cap2;
        ps.pool_radius = rad50;
        combatant m;
        memset(&m, 0, sizeof m);
        m.gr.nweapons = 1; m.gr.weapon[0].type = 0; m.ammo[0] = 100; m.state[0] = 1;   /* LMG: interval 5 */
        float from[3] = {0, 0, 0}, to[3] = {0, 0, 1000};
        int fired = 0;
        for (int t = 0; t < 60; t++) fired += combat_weapons_tick(&m, 0, 1, from, to, NULL, NULL, &ps);
        int live = 0;
        for (int i = 0; i < COMBAT_MAX_SHOTS; i++) live += ps.shot[i].active;
        CHECK(live == 2, "pool: %d shells in flight (want 2)", live);
        CHECK(fabsf(ps.shot[0].z - 100.0f) < 1e-3f && ps.shot[0].serial == 1, "offset %g serial %u", ps.shot[0].z, ps.shot[0].serial);
        CHECK(m.ammo[0] == 100 - fired, "rounds spent %d of %d", 100 - m.ammo[0], fired);
    }
    {
        /* 0x100191d7: a hand grenade (type 0x1c, +0x76 = 3) bounces off the ground (halved, y turned), throws a spray,
         * and at its life's end (633 ticks) strikes as on the ground */
        static combat_shots gs;
        memset(&gs, 0, sizeof gs);
        gs.ev.spray = on_spray;
        gs.ev.impact = on_impact;
        combatant m;
        memset(&m, 0, sizeof m);
        m.gr.nweapons = 1; m.gr.weapon[0].type = 0x1c; m.ammo[0] = 5; m.state[0] = 1;
        float from[3] = {0, 100, 0}, to[3] = {0, 100, 1000};
        while (!combat_weapons_tick(&m, 0, 1, from, to, NULL, NULL, &gs)) {}
        combat_world w = {NULL, none, ground0, nobody, NULL, NULL, NULL, NULL};
        const combat_shot *g = NULL;
        for (int i = 0; i < COMBAT_MAX_SHOTS; i++) if (gs.shot[i].active) g = &gs.shot[i];
        CHECK(g && g->motion == 3, "grenade motion %d", g ? g->motion : -1);
        int t = 0;
        while (g && g->active && t < 2000) { combat_shots_tick(&gs, &w); t++; }
        CHECK(sprays > 0 && end_victim == -1 && t == gear_weapon_stat[0x1c].life, "grenade: %d sprays, end %d after %d ticks",
              sprays, end_victim, t);
    }

    /* crits (0x1005b5c6): pounding a gear's leg brings leg actuator reports and a slower gear */
    {
        srand(1);
        int slower = 0;
        for (int n = 0; n < 50; n++) {
            hunter(&c);
            c.gr.version = 2; c.gr.crit_type = 1;
            combat_crits_init(&c);
            c.report = rep;
            c.internal[4] = 1e6f;   /* a leg that won't come off */
            for (int k = 0; k < 40; k++) combat_damage(&c, 2, 85, 5);
            if (c.speed_factor < 1) slower++;
        }
        CHECK(reports[9] > 0 && slower > 0, "leg crits: %d reports, %d slower", reports[9], slower);
    }

    /* guidance (0x1001aec9): a light guided missile (type 9) fired straight ahead at a target 20000 to the side and
     * ahead turns onto it and strikes within 100 */
    {
        static combat_shots gs;
        memset(&gs, 0, sizeof gs);
        combatant sh;
        memset(&sh, 0, sizeof sh);
        sh.gr.nweapons = 1; sh.gr.weapon[0].type = 9; sh.ammo[0] = 4; sh.state[0] = 1; sh.lock = 1;
        hunter(&guided_victim);
        float from[3] = {0, 0, 0}, to[3] = {0, 0, 1000};
        tgt_at[0] = 20000; tgt_at[1] = 0; tgt_at[2] = 30000;
        for (int t = 0; t < 200; t++) combat_weapons_tick(&sh, 0, t < 2, from, to, NULL, NULL, &gs);
        combat_world gw = {NULL, none, flat, gvic, NULL, NULL, tpos, NULL};
        /* it is kept level (+0x7e 0x20: its height left to gravity), so the test asks for its closest pass on the
         * ground plan: a straight shot would pass 20000 off */
        float best = 1e9f;
        for (int t = 0; t < 700; t++) {
            combat_shots_tick(&gs, &gw);
            const combat_shot *q = &gs.shot[0];
            float dx = q->x - tgt_at[0], dz = q->z - tgt_at[2], d = sqrtf(dx * dx + dz * dz);
            if (q->active && d < best) best = d;
        }
        CHECK(best < 2500, "guided missile's closest pass %g", best);
    }

    /* arming (0x100af2a0): a lofted guided shot (type 9, +0x86 0x61) aimed a little up steers only once it comes back
     * down to the target's height: vy / (0.0296 x 5 / 2) ticks */
    {
        static combat_shots gs;
        memset(&gs, 0, sizeof gs);
        combatant sh;
        memset(&sh, 0, sizeof sh);
        sh.gr.nweapons = 1; sh.gr.weapon[0].type = 9; sh.ammo[0] = 4; sh.state[0] = 1; sh.lock = 1;
        hunter(&guided_victim);
        float from[3] = {0, 0, 0}, to[3] = {0, 100, 1000};
        tgt_at[0] = 20000; tgt_at[1] = 0; tgt_at[2] = 30000;
        for (int t = 0; t < 200 && !gs.shot[0].active; t++) combat_weapons_tick(&sh, 0, t < 2, from, to, NULL, NULL, &gs);
        combat_world gw = {NULL, none, flat, gvic, NULL, NULL, tpos, NULL};
        float vy = gs.shot[0].vy, want = vy / (0.0296f * 5.0f / 2.0f);
        int first_steer = -1;
        for (int t = 0; t < 400 && gs.shot[0].active; t++) {
            combat_shots_tick(&gs, &gw);
            if (first_steer < 0 && gs.shot[0].ax != 0) first_steer = gs.shot[0].age;
        }
        CHECK(fabsf(gs.shot[0].arm - want) < 1.0f, "armed after %g ticks (want %g)", gs.shot[0].arm, want);
        CHECK(first_steer > (int)want - 1 && first_steer < (int)want + 3, "first steers at %d", first_steer);
    }

    {
        /* the seeker (0x100bc861): acquiring, 0x16a ticks to lock, out of the cone the lock drops at once and the
         * acquiring after 0x169 more ticks; out of the band nothing */
        int gt = -1;
        for (int t = 0; t < GEAR_WEAPON_TYPES && gt < 0; t++)
            if ((gear_weapon_stat[t].sflags & 0x21) == 1 && gear_weapon_stat[t].max_range > 0) gt = t;
        CHECK(gt >= 0, "a guided weapon %d", gt);
        const gear_weapon_stats *st = &gear_weapon_stat[gt];
        float d = (st->min_range + st->max_range) / 2;
        combat_seeker sk = {0, 0, 0, 0};
        combat_seeker_tick(&sk, gt, 1, d, 5, 0, 0, 1);
        CHECK(sk.acquiring && sk.in_cone && !sk.locked, "acquiring %d", sk.acquiring);
        int n = 0;
        while (!sk.locked && n < 1000) { combat_seeker_tick(&sk, gt, 1, d, 5, 0, 0, 1); n++; }
        CHECK(n == 0x16a + 1, "locked after %d ticks", n);
        combat_seeker_tick(&sk, gt, 1, d, 20, 0, 0, 1);
        CHECK(!sk.locked && sk.acquiring && !sk.in_cone, "out of the cone %d", sk.locked);
        for (int t = 0; t < 0x169; t++) combat_seeker_tick(&sk, gt, 1, d, 20, 0, 0, 1);
        CHECK(!sk.acquiring, "acquiring dropped %d", sk.acquiring);
        combat_seeker_tick(&sk, gt, 1, st->max_range + 1, 0, 0, 0, 1);
        CHECK(!sk.acquiring && !sk.in_cone, "out of the band %d", sk.in_cone);
        combat_seeker_tick(&sk, gt, 1, d, 0, 0, 1, 1);
        combat_seeker_tick(&sk, gt, 1, d, 0, 0, 1, 1);
        CHECK(sk.locked, "fast lock %d", sk.locked);
    }

    /* a power loss (crit 0x1eaa, +0xc3d = 0x21f) holds the trigger (0x100bb425) */
    {
        combatant ps;
        hunter(&ps);
        ps.stun = 0x21f;
        static combat_shots ss;
        memset(&ss, 0, sizeof ss);
        float f0[3] = {0, 0, 0}, f1[3] = {0, 0, 1000};
        int shots = 0;
        for (int t = 0; t < 200; t++) shots += combat_weapons_tick(&ps, 0, 1, f0, f1, NULL, NULL, &ss);
        CHECK(shots == 0, "stunned fired %d", shots);
    }

    /* the point lights' kinds (0x1001b3a1's local_68) */
    CHECK(combat_effect_light(2, 0, 1) && combat_effect_light(0x24, 0, 0) && combat_effect_light(0x3d, COMBAT_HIT_VEHICLE, 0),
          "light: always kinds");
    CHECK(combat_effect_light(0x3b, COMBAT_HIT_GROUND, 0) && !combat_effect_light(0x3b, 0, 0) &&
          !combat_effect_light(0x3b, COMBAT_HIT_GROUND | COMBAT_HIT_VEHICLE, 0) && !combat_effect_light(5, COMBAT_HIT_GROUND, 0),
          "light: ground-only kinds");
    if (fails) { printf("test_combat: %d failures\n", fails); return 1; }
    printf("test_combat: ok\n");
    return 0;
}
