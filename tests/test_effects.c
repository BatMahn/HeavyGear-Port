/* the effect pool: pool sizes, the nearby limit, lives, following, types with no pool */
#include "../src/effects.h"
#include "../src/gear_tables.h"

#include <stdio.h>
#include <string.h>

static int made, shown[64], vx;
static float ly;
static int f_add(void *c, const char *m) { (void)c; (void)m; return made < 64 ? made++ : -1; }
static void f_set(void *c, int h, int on, float x, float y, float z, float age, int seq) { (void)c; (void)x; (void)z; (void)age; (void)seq; shown[h] = on; ly = y; }
static int f_pos(void *c, int u, float p[3]) { (void)c; if (u != 3) return 0; p[0] = (float)vx; p[1] = 1000; p[2] = 0; return 1; }
static float f_ground(void *c, float x, float z) { (void)c; (void)x; (void)z; return 100.0f; }
static float f_radius(void *c, int h) { (void)c; (void)h; return 20.0f; }
static int turned, sp_n, sp_veh, sp_owner;
static float sp_r, sp_p;
static void f_splash(void *c, int o, float x, float y, float z, float r, float p, int v)
{ (void)c; (void)x; (void)y; (void)z; sp_n++; sp_r = r; sp_p = p; sp_veh = v; sp_owner = o; }
static float ortho_err;
static void f_turn(void *c, int h, const float m[9])
{
    (void)c; (void)h; turned++;
    for (int i = 0; i < 3; i++)          /* the orientation stays a rotation: rows orthonormal */
        for (int j = 0; j < 3; j++) {
            float d = m[i * 3] * m[j * 3] + m[i * 3 + 1] * m[j * 3 + 1] + m[i * 3 + 2] * m[j * 3 + 2] - (i == j);
            if (d < 0) d = -d;
            if (d > ortho_err) ortho_err = d;
        }
}

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    static char model[FX_TYPES][17];
    static int count[FX_TYPES];
    strcpy(model[0x2a], "wxexplod"); count[0x2a] = 20;
    strcpy(model[0x17], "wxmgnmzl"); count[0x17] = 2;
    strcpy(model[0x2f], "wxflamob"); count[0x2f] = 10;
    effects_ops ops = {NULL, f_add, f_set, f_pos, f_ground, f_radius, f_turn, f_splash, NULL};
    effects fx;
    effects_init(&fx, &ops, (const char (*)[17])model, count);
    CHECK(!effects_start(&fx, 5, 0, 0, 0, -1));                 /* no pool: nothing (no sound either) */
    CHECK(effects_start(&fx, 0x2a, 0, 0, 0, -1));
    CHECK(effects_start(&fx, 0x2a, 100, 0, 0, -1));
    CHECK(effects_start(&fx, 0x2a, 0, 100, 0, -1));
    CHECK(!effects_start(&fx, 0x2a, 0, 0, 100, -1));            /* three within 500 already */
    CHECK(effects_start(&fx, 0x2a, 5000, 0, 0, -1));
    CHECK(effects_active(&fx) == 4);
    CHECK(effects_start(&fx, 0x17, 0, 0, 0, -1));               /* muzzle flashes: no nearby limit, pool of 2 */
    CHECK(effects_start(&fx, 0x17, 0, 0, 0, -1));
    CHECK(!effects_start(&fx, 0x17, 0, 0, 0, -1));
    effects_tick(&fx, gear_effect[0x17].life - 1);
    CHECK(effects_active(&fx) == 6);
    effects_tick(&fx, 1);
    CHECK(effects_active(&fx) == 4);                            /* the flashes' 36 ticks are up */
    CHECK(effects_start(&fx, 0x17, 0, 0, 0, -1));               /* reuses a free one */
    CHECK(made == 6);
    effects_tick(&fx, gear_effect[0x2a].life);
    CHECK(effects_active(&fx) == 0);
    for (int i = 0; i < made; i++) CHECK(!shown[i]);
    /* following a vehicle (flags bit 0): 300 below it, for the type's life */
    CHECK(gear_effect[0x2f].flags & 1);
    CHECK(effects_start(&fx, 0x2f, 0, 0, 0, 3));
    vx = 50;
    effects_tick(&fx, 10);
    CHECK(ly == 700.0f);
    effects_tick(&fx, gear_effect[0x2f].life);
    CHECK(effects_active(&fx) == 0);
    CHECK(effects_start(&fx, 0x2f, 0, 0, 0, -1));               /* none to follow: 181 ticks */
    effects_tick(&fx, 0xb4);
    CHECK(effects_active(&fx) == 1);
    effects_tick(&fx, 1);
    CHECK(effects_active(&fx) == 0);
    /* debris: chunks of the pool (15) fly, fall to the ground (100) and rest; a wreck's burning throws them */
    strcpy(model[0x0b], "wgchunk0"); count[0x0b] = 15;
    CHECK(effects_debris(&fx, 0, 500, 0, 8) == 8);
    CHECK(effects_debris(&fx, 0, 500, 0, 16) == 7);
    effects_tick(&fx, 3000);   /* the tables' highest throws take ~1100 ticks to come down */
    int resting = 0, low = 1;
    for (int i = 0; i < fx.n; i++) if (fx.s[i].type == 0x0b && fx.s[i].on) {
        resting += !fx.s[i].body;
        if (fx.s[i].y - 10.0f > 100.5f + 30.0f) low = 0;
    }
    CHECK(resting == effects_active(&fx));
    CHECK(low);
    CHECK(turned > 0);
    CHECK(ortho_err < 1e-3f);
    effects_tick(&fx, 1810);
    CHECK(effects_active(&fx) == 0);
    effects_wreck(&fx, 0, 100, 0);
    effects_tick(&fx, 0x100);
    CHECK(effects_active(&fx) > 0 && effects_active(&fx) <= 15);
    effects_tick(&fx, 0x712 + 1810);
    CHECK(effects_active(&fx) == 0);
    /* splash: the ground explosion's radius grows 16 a tick and harms vehicles; the gear death's is its model's */
    strcpy(model[0x2c], "wxgrexpl"); count[0x2c] = 20;
    strcpy(model[0x24], "wxgdeath"); count[0x24] = 10;
    fx.owner = 7;
    CHECK(effects_start(&fx, 0x2c, 0, 0, 0, -1));
    fx.owner = -2;
    effects_tick(&fx, 10);
    sp_n = 0;
    effects_splash(&fx, 6);
    CHECK(sp_n == 1 && sp_r == 160.0f && sp_veh && sp_owner == 7);
    CHECK(sp_p == 6.0f * gear_effect[0x2c].power);
    effects_tick(&fx, 1000);
    CHECK(effects_start(&fx, 0x24, 0, 0, 0, -1));
    sp_n = 0;
    effects_splash(&fx, 1);
    CHECK(sp_n == 1 && sp_r == 20.0f && !sp_veh);
    if (!fails) printf("test_effects: ok\n");
    return fails != 0;
}
