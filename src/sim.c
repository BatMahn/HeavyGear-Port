#include "sim.h"

#include <math.h>
#include <string.h>

#define DEG (3.14159265358979f / 180.0f)

/* 0x10039c56: a lag of `sec` seconds (x 181 ticks) */
static void lag_set(sim_lag *l, float target, float cur, float sec)
{
    l->target = target;
    l->cur = cur;
    l->T = sec * SIM_TICKS_PER_SECOND;
}

/* 0x10039cad: cur += (target - cur) x ticks / T, stopping at the target */
static float lag_run(sim_lag *l, int dt)
{
    float d = l->target - l->cur;
    if (fabsf(d) < 1e-7f) {
        l->cur = l->target;
    } else {
        float step = (float)dt * d / l->T;
        if ((d < 0 && step <= d) || (d > 1e-7f && d <= step)) l->cur = l->target;
        else l->cur += step;
    }
    return l->cur;
}

static float wrap180(float a)
{
    while (a > 180) a -= 360;
    while (a < -180) a += 360;
    return a;
}

/* 0x1006f4ee: atan2 in degrees */
static float atan2d(float y, float x)
{
    return atan2f(y, x) / DEG;
}

void sim_gear_init(sim_gear *g, float x, float y, float z, float heading, float top_speed, float stand)
{
    memset(g, 0, sizeof *g);
    g->x = x; g->y = y; g->z = z;
    g->heading = heading;
    g->top_speed = top_speed;
    g->stand = stand;
    g->vclass = 1;
    g->speed_factor = 1;
    g->skate_ticks = 0xb5;          /* +0x60b (0x5a with the builder's +0xbd3 bit 0x20) */
    g->jumps = -1;                  /* no jets (+0x510 = -2) */
    g->on_ground = 1;
    /* 0x1005cf76's lags */
    lag_set(&g->twist, 0, 0, 0.2f);
    lag_set(&g->spd, 0, 0, 0.2f);
    lag_set(&g->turn, 0, 0, 0.3f);
    lag_set(&g->tilt, 0, 0, 0.2f);
    lag_set(&g->thr, 1.0f / 64, 1.0f / 64, 0.2f);
    lag_set(&g->roll, 0, 0, 0.7f);
    lag_set(&g->pitch, 0, 0, 0.7f);
    g->last_heading = heading;
}

void sim_gear_jets(sim_gear *g, int has_jets, int torso_id)
{
    static const float f[16] = {0.25f, 0.25f, 0.283f, 0.316f, 0.316f, 0.35f, 0.35f, 0, 0.25f, 0.25f, 0.283f, 0.316f,
                                0.316f, 0.35f, 0.35f, 0};
    if (!has_jets) { g->jumps = -1; g->jump_lift = 0; return; }
    g->jumps = 10;
    g->jump_time = 0x2d4;
    g->jump_lift = 0.148f * (torso_id >= 0 && torso_id < 16 ? f[torso_id] : 0);
}

/* the move (0x1005d75c) */
static int sim_move(sim_gear *g, const sim_control *c, int dt, int running, sim_ground_n_fn ground, void *ctx)
{
    int ev = 0;
    /* the lags, in the original's order (+4 .. +0x24) */
    lag_run(&g->twist, dt);
    lag_run(&g->tilt, dt);
    lag_run(&g->turn, dt);
    lag_run(&g->roll, dt);
    lag_run(&g->pitch, dt);
    lag_run(&g->thr, dt);
    lag_run(&g->spd, dt);
    g->speed = g->spd.cur;

    float vx = g->vx, vy = g->vy, vz = g->vz;
    float ax = 0, az = 0, ay = -SIM_GRAVITY;
    float hx = sinf(g->heading * DEG), hz = cosf(g->heading * DEG);   /* +0xcc / +0xc8 */
    int jumping = g->jumping;
    int jump_key = c && c->jump;
    if (!g->on_ground || jump_key || jumping) {
        /* the air's drag: a quadratic in the horizontal speed, toward the air top speed (top / 64 x 2.5) */
        float s = sqrtf(vx * vx + vz * vz);
        float L = g->top_speed * (1.0f / 64) * 2.5f, lift = g->jump_lift;
        if (s > 0 && L > 0 && lift > 0) {
            float a2 = L * L, b = a2 - L * 0.75f;
            float c1 = (lift - (lift / L) / 3.0f) / b;
            float c2 = (lift - c1 * a2) / L;
            float f = -((c1 * s + c2) * s);
            ax = f * vx / s;
            az = f * vz / s;
        }
    } else {
        float s = g->speed;
        if (g->skating) s *= 2;
        if (s < 1.0f / 2048 && s > -1.0f / 2048) s = 0;
        float tx = s * hx, tz = s * hz;
        if (dt < 45) { ax = (tx - vx) / 45.0f; az = (tz - vz) / 45.0f; }
        else if (dt > 0) { ax = (tx - vx) / (float)dt; az = (tz - vz) / (float)dt; }
    }
    if ((jump_key || jumping) && g->jumps > 0) {
        /* the jets' lift, damped as the gear climbs (0x1005d75c's jump branch) */
        float damp = vy > 0 ? (SIM_GRAVITY - g->jump_lift) * 1448.0f * vy / 100000.0f : 0;
        ay = damp + g->jump_lift - SIM_GRAVITY;
    }
    float t = (float)dt;
    float dx = (t * ax * 0.5f + vx) * t, dz = (t * az * 0.5f + vz) * t;
    vx += t * ax; vz += t * az;
    /* below 1/16 a velocity is 0 (0x1010130c). The original steps once a frame of several ticks; stepped every tick
     * (dt 1) the dead zone would hold every speed under 45 / 16 = 2.8 units a tick at 0, so the per-tick callers
     * (sim_gear_tick: the AI's vehicles) go without it */
    if (!g->per_tick) {
        if (fabsf(vx) < 0.0625f) { vx = 0; dx = 0; }
        if (fabsf(vz) < 0.0625f) { vz = 0; dz = 0; }
    }
    float dy = 0;
    if (running || vy != 0 || ay != -SIM_GRAVITY || !g->on_ground) {
        dy = (t * ay * 0.5f + vy) * t;
        vy += t * ay;
    }
    g->x += dx; g->z += dz;
    float ny = g->y + dy;
    float n[3] = {0, 1, 0};
    float gh = ground ? ground(ctx, g->x, g->z, n) : g->y - g->stand;
    float above = ny - g->stand - gh;   /* the soles' height over the ground */
    if (above <= g->stand) {
        if (above < 0) {
            float hit = -vy * 6.0f;   /* 0x10101324 */
            if (!g->on_ground && hit > 140.0f) { ev |= SIM_EV_LAND_HARD; g->land_speed = hit; }
            g->on_ground = 1;
            vy = 0;
            ny = gh + g->stand;
        }
    } else {
        g->on_ground = 0;
    }
    g->y = ny;
    g->vx = vx; g->vy = vy; g->vz = vz;
    /* the heading: the turn rate / 181 a tick */
    g->heading = wrap180(g->heading + t / SIM_TICKS_PER_SECOND * g->turn.cur);
    g->turn_rate = g->turn.cur;
    /* gears (classes 1, 4, 10): pitch and roll from the ground's polygon, gears held to +-10 */
    if (g->vclass == 1 || g->vclass == 4 || g->vclass == 10) {
        float p = 90.0f - atan2d(n[1], n[2]);
        float r = atan2d(n[1], n[0]) - 90.0f;
        if (g->vclass == 1) {
            if (p < -10) p = -10;
            if (p > 10) p = 10;
            if (r < -10) r = -10;
            if (r > 10) r = 10;
        }
        g->pitch.target = p;
        g->roll.target = r;
    }
    /* skating (0x1005d75c, +0x607 / +0x60f) */
    if (!g->skate_wait) {
        int want = c && c->skate && g->throttle * (1.0f / 64) <= 1.0f / 128 && g->vclass == 1;
        if (!want) {
            if (g->skating && !(g->throttle > 0)) { g->skating = 0; ev |= SIM_EV_SKATE_OFF; }
        } else if (!g->skating) {
            g->skate_wait = g->skate_ticks;
            ev |= SIM_EV_SKATE_ENGAGE;
        } else {
            g->skating = 0;
            ev |= SIM_EV_SKATE_OFF;
        }
    } else {
        g->skate_wait -= dt;
        if (g->skate_wait < 1) {
            g->skate_wait = 0;
            g->skating = 1;
            ev |= SIM_EV_SKATE_ON;
        }
    }
    return ev;
}

/* the control step (0x10060491) */
static int sim_control_step(sim_gear *g, const sim_control *c, int dt, int running)
{
    int ev = 0;
    float thr = c ? c->throttle : 0, turnc = c ? c->turn : 0;
    if (thr < 0) thr = 0;
    if (thr > 1) thr = 1;
    if (turnc < -1) turnc = -1;
    if (turnc > 1) turnc = 1;
    g->throttle = thr;
    if (c && c->reverse) g->reverse ^= 1;
    if (!running) {
        g->thr.target = 1.0f / 64;
        g->spd.target = 0;
        g->turn.target = 0;
        g->twist.target = 0;
        return 0;
    }
    /* stand up from a fall (0x1005d75c: standup +0x55 while down, 0x562 & 8, the legs whole): 0x200, the fall's
     * animation then plays its second half (0x10013d53) */
    if (c && c->standup && (g->body & 8) && !g->legs_gone) { g->body |= 0x200; g->body &= ~8u; }
    /* crouch / stand up (+0x54 / +0x55, the stance flag +0x562 bit 0x10; on the ground, not skating) */
    if (c && c->crouch && g->on_ground && !g->skating && !g->crouched) { g->crouched = 1; ev |= SIM_EV_CROUCH; }
    if (c && c->standup && g->crouched) { g->crouched = 0; ev |= SIM_EV_STAND; }
    /* jump jets (+0x510 .. +0x518) */
    if (g->jumps > 0 && ((c && c->jump) || g->jumping)) {
        if (!g->jumping) ev |= SIM_EV_JUMP_START;
        g->jumping = 1;
        g->jump_time -= dt;
        if (g->jump_time < 1) {
            ev |= SIM_EV_JUMP_END;
            g->jump_time = 0x2d4;
            g->jumps--;
            g->jumping = 0;
        }
    }
    /* the throttle: control +8 x the factor + 1/64; the player's stopped gear 1/64 + 1/512 */
    g->thr.target = thr * (1.0f / 64) * g->speed_factor + 1.0f / 64;
    if (g->body & 0x60e) g->thr.target = 1.0f / 64;   /* falling, down or getting up: no throttle (0x10060491) */
    if (g->player && g->thr.target == 1.0f / 64) g->thr.target = 1.0f / 64 + 1.0f / 512;
    /* the turn rate */
    float s6 = g->speed * 6.0f, base = 0;
    int airborne_turn = g->skating || g->jumping;
    if (airborne_turn) s6 *= 2;
    if (s6 > 80) s6 = 80;
    int classwide = g->vclass == 8 || g->vclass == 2;
    if (!airborne_turn) {
        if (s6 != 0 || (g->player && g->speed_factor > 0)) base = turnc * (classwide ? 180.0f : 90.0f);
    } else {
        base = turnc * (classwide ? 180.0f : 90.0f * 0.5f);
    }
    g->turn.target = base * cosf(s6 * DEG);
    /* feet to torso (0x1013194c, the player's): the legs turn toward the torso, the twist taking it back */
    if (g->player && c && c->feet_to_torso) g->feet = 1;
    if (g->player && g->feet) {
        float dh = wrap180(g->heading - g->last_heading);
        if (g->twist.cur <= -1 || g->twist.cur >= 1) {
            g->turn.target = g->twist.cur <= 0 ? (g->skating ? -25.0f : -55.0f) : (g->skating ? 25.0f : 55.0f);
            if (dh != 0) { g->twist.target = g->twist.cur - dh; g->twist.cur = g->twist.target; }
        } else {
            g->turn.target = g->turn.cur = 0;
            g->twist.target = g->twist.cur = 0;
            g->feet = 0;
        }
    }
    g->last_heading = g->heading;
    /* the speed: (throttle - 1/64) x top speed, class 8 x 4; reverse halved and negated */
    float sp = g->thr.cur - 1.0f / 64;
    if (sp < 1.0f / 2048) sp = 0;
    sp *= g->top_speed;
    if (g->vclass == 8) sp *= 4;
    if (g->reverse) sp = -sp / 2;
    g->spd.target = sp;
    /* the twist: control +4 held to +0x534 (180) */
    if (!g->feet) {
        float tw = c ? c->torso_pan : 0;
        if (tw > 180) tw = 180;
        if (tw < -180) tw = -180;
        g->twist.target = tw;
    }
    g->tilt.target = c ? c->torso_tilt : 0;
    return ev;
}

int sim_gear_step(sim_gear *g, const sim_control *c, int dt, int running, sim_ground_n_fn ground, void *ctx)
{
    if (g->player) g->twist.T = 0.4f * SIM_TICKS_PER_SECOND;
    int ev = sim_move(g, c, dt, running, ground, ctx);
    ev |= sim_control_step(g, c, dt, running);
    return ev;
}

typedef struct { sim_ground_fn fn; void *ctx; } ground_wrap;

static float ground_flat_n(void *ctx, float x, float z, float n[3])
{
    const ground_wrap *gw = ctx;
    n[0] = 0; n[1] = 1; n[2] = 0;
    return gw->fn(gw->ctx, x, z);
}

void sim_gear_tick(sim_gear *g, float throttle, float turn, sim_ground_fn ground, void *ctx)
{
    sim_gear_tick_pan(g, throttle, turn, 0, ground, ctx);
}

void sim_gear_tick_pan(sim_gear *g, float throttle, float turn, float pan, sim_ground_fn ground, void *ctx)
{
    sim_control c;
    memset(&c, 0, sizeof c);
    c.torso_pan = pan;
    c.torso_tilt = g->ai_tilt;
    int rev = throttle < 0;
    c.throttle = rev ? -throttle : throttle;
    c.turn = turn;
    if (rev != g->reverse) c.reverse = 1;
    /* an AI gear or infantry down from a fall with its legs whole stands up (0x10010cd2 -> 0x10010cb0: +0x55) */
    if ((g->body & 8) && !g->legs_gone) c.standup = 1;
    ground_wrap gw = {ground, ctx};
    g->per_tick = 1;
    sim_gear_step(g, &c, 1, 1, ground ? ground_flat_n : NULL, &gw);
}

float sim_gear_speed(const sim_gear *g)
{
    return sqrtf(g->vx * g->vx + g->vz * g->vz) * (1000.0f / SIM_TICK_MS);
}

int sim_gear_kph(const sim_gear *g)
{
    int k = (int)(sqrtf(g->vx * g->vx + g->vy * g->vy + g->vz * g->vz) * 6.0f * 1.38f);
    return g->speed < 0 ? -k : k;
}

float sim_gear_view_yaw(const sim_gear *g)
{
    return wrap180(g->heading + g->twist.cur);
}
