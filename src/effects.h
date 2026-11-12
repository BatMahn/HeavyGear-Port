/*
 * effects.h - the visible effects: explosions, hit flashes, muzzle flashes, a gear's death (HGSIM 0x1001b3a1, the
 * effect step; 0x1001c40d, their ticking).
 *
 * The sim keeps a pool of effect objects (0x107b97a0, 0x100 x 0x2c bytes) filled from the HGXP records of gearsht1.BWD
 * (the loader 0x100c1152): per effect type a model (a flat quad with an animated gearxpl1 material, e.g. wxexplod ->
 * EXPLOSION -> WXEXPL01..14) and how many of it. Starting an effect of a type takes a free one of that type:
 *   - none free, or no pool for the type: nothing happens - not even the effect's sound (gear_effect[].sound);
 *   - unless the type's flags +0x28 have bit 1, or it is a muzzle flash (0x17..0x21, not 0x4c): at most two of the
 *     type may already be running within 500 of the spot (none at the low LOD quality, 0x101244f8 == 2);
 *   - it lasts the type's life (gear_effect[].life, ticks); flags bit 0: it follows the struck vehicle (300 below its
 *     origin; with none, 181 ticks).
 * Debris (type 0x0b, wgchunk0, 0x1001ce0a): chunks thrown from a point with random velocities (0x1001e76f) that fall,
 * bounce and come to rest on the ground (0x1001ef21), each for 1810 ticks. A destroyed gear (0x100533d2) burns for
 * 0x712 ticks: each tick 21% of a burst at a random offset, 40% of those four chunks of debris.
 */
#ifndef HG_EFFECTS_H
#define HG_EFFECTS_H

#define FX_TYPES 0x4d
#define FX_SLOTS 256

typedef struct {
    void *ctx;
    /* make an effect object of a model (hidden); its handle, or -1 */
    int  (*add)(void *ctx, const char *model);
    /* show / hide it, where, and how many ticks it has run (for its animation) */
    /* age: ticks since the start; seq: how its texture sequence runs (gear_effect[].seq: 1 loops, 2 one pass) */
    void (*set)(void *ctx, int h, int on, float x, float y, float z, float age, int seq);
    /* where a followed vehicle is (0: gone) */
    int  (*vehicle_pos)(void *ctx, int unit, float p[3]);
    /* debris: the ground's height, an object's size (its bounding sphere's radius) and its turn (degrees) */
    float (*ground)(void *ctx, float x, float z);
    float (*radius)(void *ctx, int h);
    void (*turn)(void *ctx, int h, const float m[9]);
    /* an explosion's harm (0x1001c75c / 0x1001c96a): to what is within `radius` (plus its own size) of the point,
     * `power` (already times the ticks); vehicles only when `vehicles` */
    void (*splash)(void *ctx, int owner, float x, float y, float z, float radius, float power, int vehicles);
    /* optional: a vehicle's yaw (degrees), for muzzle flashes fixed to it (NULL: they keep their turn) */
    int  (*vehicle_yaw)(void *ctx, int unit, float *deg);
} effects_ops;

typedef struct {
    int   type, h, on, follow, owner;
    float x, y, z;
    int   left, age;
    int   body;            /* debris in flight (0x107f74a0's bodies) */
    int   mount;           /* a muzzle flash fixed to vehicle `mount` (0x100bd13f), -1 none */
    float mo[3], myaw;     /* its offset from that vehicle's origin, and the vehicle's yaw, at the start */
    float v[3], w[3], m[9], r;   /* w: the turn a tick about x, y, z (degrees); m: the orientation */
} fx_slot;

typedef struct { float x, y, z; int left; } fx_burn;

typedef struct {
    effects_ops ops;
    const char (*model)[17];   /* per type, "" none */
    const int  *count;         /* per type, the pool size */
    fx_slot     s[FX_SLOTS];
    int         n;
    int         low_lod;       /* 0x101244f8 == 2 */
    fx_burn     burn[32];
    int         owner;         /* who the effects started next belong to (-2 none, the original's 0xfffffffe) */
    int         splash_all;    /* the option at 0x1014a564 + 2: every splash effect harms vehicles (else 0x2c / 0x2d) */
    unsigned    seed;          /* the random numbers (the sim's tables 0x1018e5a0 / 0x1018e7a0 from rand()) */
} effects;

void effects_init(effects *fx, const effects_ops *ops, const char (*model)[17], const int *count);
/* start an effect of `type` at (x, y, z); follow: the struck vehicle (unit index) or -1. 1 started (its sound may
 * play), 0 not */
int  effects_start(effects *fx, int type, float x, float y, float z, int follow);
/* throw n chunks of debris from (x, y, z) (0x1001ce0a: n 4, 8 or 16 by what was struck); how many went */
int  effects_debris(effects *fx, float x, float y, float z, int n);
/* a destroyed gear's burning (0x100533d2): 0x712 ticks of bursts and debris about (x, y, z) */
void effects_wreck(effects *fx, float x, float y, float z);
/* a muzzle flash (types 0x17..0x21, not 0x4c) at the firing vehicle's weapon: the original hangs the effect object on
 * the weapon's mount (0x100bd13f, through 0x10130718 the firer), so it moves and turns with the vehicle while it lasts.
 * Other types start as effects_start. */
int  effects_start_on(effects *fx, int type, float x, float y, float z, int vehicle);
/* the running effects' harm for a frame of `dt` ticks (0x1001c40d, before their lives count down): types with the
 * table's +0x18, radius by type (0x16: its age x 4, 0x2c: x 16, else the model's size), power dt x +0x20 */
void effects_splash(effects *fx, int dt);
/* the option at 0x1014a564 + 2 (the options screen's): every splash effect harms vehicles */
void effects_set_splash_all(effects *fx, int on);
/* run the effects `dt` ticks on */
void effects_tick(effects *fx, int dt);
/* how many are running */
int  effects_active(const effects *fx);

#endif
