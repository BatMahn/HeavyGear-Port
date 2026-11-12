#include "effects.h"

#include "gear_tables.h"

#include <math.h>
#include <string.h>

void effects_init(effects *fx, const effects_ops *ops, const char (*model)[17], const int *count)
{
    memset(fx, 0, sizeof *fx);
    fx->ops = *ops;
    fx->model = model;
    fx->count = count;
    fx->seed = 1;
    fx->owner = -2;
}

void effects_splash(effects *fx, int dt)
{
    if (!fx->ops.splash) return;
    for (int i = 0; i < fx->n; i++) {
        const fx_slot *s = &fx->s[i];
        if (!s->on || s->type < 0 || s->type >= FX_TYPES || !gear_effect[s->type].splash) continue;
        int life = gear_effect[s->type].life;
        float r = s->type == 0x16 ? (float)(life - s->left) * 4.0f
                : s->type == 0x2c ? (float)(life - s->left) * 16.0f
                : fx->ops.radius ? fx->ops.radius(fx->ops.ctx, s->h) : 0.0f;
        fx->ops.splash(fx->ops.ctx, s->owner, s->x, s->y, s->z, r, (float)dt * gear_effect[s->type].power,
                       fx->splash_all || s->type == 0x2c || s->type == 0x2d);
    }
}

/* rand() as the original's C library (MSVC: seed * 214013 + 2531011, bits 16..30) */
static int crand(effects *fx)
{
    fx->seed = fx->seed * 214013u + 2531011u;
    return (int)(fx->seed >> 16 & 0x7fff);
}

/* the sim's normal table (0x10089da0): 30 rand()s summed, scaled to a mean of 0 and a deviation of 1024 */
static int nrand(effects *fx)
{
    int sum = 0;
    for (int k = 0; k < 30; k++) sum += crand(fx);
    return (int)(((double)sum * 2.0 * sqrt(90.0) / 983010.0 - sqrt(90.0)) * 1024.0);
}

static fx_slot *take(effects *fx, int type)
{
    for (int i = 0; i < fx->n; i++)
        if (fx->s[i].type == type && !fx->s[i].on) return &fx->s[i];
    int have = 0;
    for (int i = 0; i < fx->n; i++) have += fx->s[i].type == type;
    if (!fx->model || !fx->model[type][0] || have >= fx->count[type] || fx->n >= FX_SLOTS) return NULL;
    int h = fx->ops.add(fx->ops.ctx, fx->model[type]);
    if (h < 0) return NULL;
    fx_slot *s = &fx->s[fx->n++];
    memset(s, 0, sizeof *s);
    s->type = type;
    s->h = h;
    return s;
}

int effects_debris(effects *fx, float x, float y, float z, int n)
{
    int k = 0;
    if (n <= 0) n = 0x100;
    while (k < n) {
        fx_slot *s = take(fx, 0x0b);
        if (!s) break;
        s->on = 1; s->body = 1; s->follow = -1; s->owner = fx->owner;
        s->x = x; s->y = y; s->z = z;
        s->left = 1810;   /* 0x1011bbfc */
        s->age = 0;
        /* 0x1001e76f: velocities N / 1024 x 4 (y: (N + 0x400) / 1024 x 4), turns N x 0.5 / 1024 a tick */
        s->v[0] = (float)nrand(fx) / 1024.0f * 4.0f;
        s->v[2] = (float)nrand(fx) / 1024.0f * 4.0f;
        s->v[1] = (float)(nrand(fx) + 0x400) / 1024.0f * 4.0f;
        for (int a = 0; a < 3; a++) s->w[a] = (float)nrand(fx) * 0.5f / 1024.0f;
        for (int a = 0; a < 9; a++) s->m[a] = a % 4 == 0 ? 1.0f : 0.0f;
        s->r = fx->ops.radius ? fx->ops.radius(fx->ops.ctx, s->h) : 0;
        fx->ops.set(fx->ops.ctx, s->h, 1, x, y, z, 0, gear_effect[0x0b].seq);
        if (fx->ops.turn) fx->ops.turn(fx->ops.ctx, s->h, s->m);
        k++;
    }
    return k;
}

void effects_wreck(effects *fx, float x, float y, float z)
{
    for (int i = 0; i < 32; i++)
        if (fx->burn[i].left <= 0) { fx->burn[i].x = x; fx->burn[i].y = y; fx->burn[i].z = z; fx->burn[i].left = 0x712; return; }
}

static void mat3_mul(const float *a, const float *b, float *o)
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) o[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
}

/* 0x1001ef21: a chunk's flight, one tick */
static void fly(effects *fx, fx_slot *s)
{
    const float g = -0.0296f;   /* 0x10110088 */
    float bottom = s->y - s->r / 2.0f, vy = s->v[1];
    int rising = vy > 1e-7f, done = 0;
    float ny = (g * 0.5f + vy) + bottom;
    vy += g;
    if (vy < 1e-7f && fx->ops.ground) {
        float gh = fx->ops.ground(fx->ops.ctx, s->x, s->z);
        if (ny <= gh) {
            if (rising) {   /* struck while still rising: taken off (0x1001f66e) */
                s->body = 0;
                s->left = 0;
                return;
            }
            if (vy > -0.5524862f) done = 1;   /* too slow to bounce: it rests */
            vy = -(vy / 4.0f);
            ny = gh;
            if (crand(fx) % 100 < 0x32) {
                vy /= 2.0f;
                s->v[0] = -(s->v[0] / 2.0f);
                s->v[2] = -(s->v[2] / 2.0f);
            }
            for (int a = 0; a < 3; a++) s->w[a] = -(s->w[a] / 2.0f);
        }
    }
    s->v[1] = vy;
    s->x += s->v[0];
    s->y += ny - bottom;
    s->z += s->v[2];
    /* the chunk turns by its rates each tick (0x100955b7 with order 0): the turn's quaternion is Z * (X * Y)
     * (0x10052fcb; 0x10052ebe the Hamilton product, 0x10052e3e axis-angle in degrees), multiplied in front of the
     * chunk's own: in matrices m = Rz(w z) Rx(w x) Ry(w y) m */
    {
        const float d = 3.14159265f / 180.0f;
        float cx = cosf(s->w[0] * d), sx = sinf(s->w[0] * d), cy = cosf(s->w[1] * d), sy = sinf(s->w[1] * d);
        float cz = cosf(s->w[2] * d), sz = sinf(s->w[2] * d);
        const float rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx}, ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
        const float rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
        float t[9], u[9], n[9];
        mat3_mul(rx, ry, t);
        mat3_mul(rz, t, u);
        mat3_mul(u, s->m, n);
        for (int a = 0; a < 9; a++) s->m[a] = n[a];
    }
    if (fx->ops.turn) fx->ops.turn(fx->ops.ctx, s->h, s->m);
    if (done) s->body = 0;
}

int effects_start(effects *fx, int type, float x, float y, float z, int follow)
{
    if (type < 0 || type >= FX_TYPES || !fx->model || !fx->model[type][0]) return 0;
    if (type == 0x0b) return effects_debris(fx, x, y, z, 8) > 0;   /* 0x1001b3a1's case 0xb: debris, no sound */
    int flags = gear_effect[type].flags, near = 0, have = 0;
    fx_slot *free_ = NULL;
    for (int i = 0; i < fx->n; i++) {
        fx_slot *s = &fx->s[i];
        if (s->type != type) continue;
        have++;
        if (!s->on) { if (!free_) free_ = s; continue; }
        /* 0x1001b3a1: running ones of the type close by (not muzzle flashes 0x17..0x21, but 0x4c) */
        if ((type < 0x17 || type > 0x21 || type == 0x4c) && !(flags & 2)) {
            float dx = s->x - x, dy = s->y - y, dz = s->z - z;
            if (sqrtf(dx * dx + dy * dy + dz * dz) < 500.0f) {
                near++;
                if (fx->low_lod || near > 2) return 0;
            }
        }
    }
    if (!free_) {
        /* the pool is made at load; here its objects are made as first needed, up to the record's count */
        if (have >= fx->count[type] || fx->n >= FX_SLOTS) return 0;
        int h = fx->ops.add(fx->ops.ctx, fx->model[type]);
        if (h < 0) return 0;
        free_ = &fx->s[fx->n++];
        memset(free_, 0, sizeof *free_);
        free_->type = type;
        free_->h = h;
    }
    fx_slot *s = free_;
    s->on = 1;
    s->x = x; s->y = y; s->z = z;
    s->left = gear_effect[type].life;
    s->age = 0;
    s->body = 0;
    s->follow = -1;
    s->owner = fx->owner;
    if (flags & 1) {
        float p[3];
        if (follow >= 0 && fx->ops.vehicle_pos && fx->ops.vehicle_pos(fx->ops.ctx, follow, p)) s->follow = follow;
        else s->left = 0xb5;
    }
    fx->ops.set(fx->ops.ctx, s->h, 1, s->x, s->y, s->z, 0, gear_effect[type].seq);
    return 1;
}

void effects_tick(effects *fx, int dt)
{
    /* a destroyed gear burning (0x100533d2): each tick 21%, at an offset of half a normal draw, 60% kind 0x3e (no
     * pool: nothing), else 0x10b: four chunks of debris */
    for (int b = 0; b < 32; b++) {
        fx_burn *u = &fx->burn[b];
        for (int t = 0; t < dt && u->left > 0; t++, u->left--) {
            if (crand(fx) % 100 >= 0x15) continue;
            float x = u->x + (float)nrand(fx) / 2.0f, z = u->z + (float)nrand(fx) / 2.0f;
            int was = fx->owner;
            fx->owner = -2;
            if (crand(fx) % 100 < 0x3c) effects_start(fx, 0x3e, x, u->y, z, -1);
            else effects_debris(fx, x, u->y, z, 4);
            fx->owner = was;
        }
    }
    for (int i = 0; i < fx->n; i++) {
        fx_slot *s = &fx->s[i];
        if (!s->on) continue;
        for (int t = 0; t < dt && s->body; t++) fly(fx, s);
        if (s->follow >= 0) {   /* 0x1001c40d: at the vehicle, 300 below its origin */
            float p[3];
            if (fx->ops.vehicle_pos(fx->ops.ctx, s->follow, p)) { s->x = p[0]; s->y = p[1] - 300.0f; s->z = p[2]; }
        }
        s->age += dt;
        s->left -= dt;
        if (s->left < 1) {
            s->on = 0;
            fx->ops.set(fx->ops.ctx, s->h, 0, s->x, s->y, s->z, 0, 0);
        } else {
            fx->ops.set(fx->ops.ctx, s->h, 1, s->x, s->y, s->z, (float)s->age, gear_effect[s->type].seq);
        }
    }
}

int effects_active(const effects *fx)
{
    int k = 0;
    for (int i = 0; i < fx->n; i++) k += fx->s[i].on;
    return k;
}
