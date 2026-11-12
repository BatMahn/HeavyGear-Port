#include "sim.h"

#include <math.h>

#ifndef SIM_TAU_SPEED
#define SIM_TAU_SPEED 45.0f
#endif
#ifndef SIM_TAU_TURN
#define SIM_TAU_TURN 54.0f
#endif
#ifndef SIM_TAU_VEL
#define SIM_TAU_VEL 125.0f
#endif

#define DEG (3.14159265358979f / 180.0f)

void sim_gear_init(sim_gear *g, float x, float y, float z, float heading, float top_speed, float stand)
{
    g->x = x; g->y = y; g->z = z;
    g->heading = heading;
    g->turn_rate = 0;
    g->speed = 0;
    g->vx = g->vz = 0;
    g->throttle = 0;
    g->start = 0;
    g->top_speed = top_speed;
    g->stand = stand;
}

void sim_gear_tick(sim_gear *g, float throttle, float turn, sim_ground_fn ground, void *ctx)
{
    if (throttle < 0) throttle = 0;
    if (throttle > 1) throttle = 1;
    if (turn < -1) turn = -1;
    if (turn > 1) turn = 1;
    g->throttle = throttle;
    /* target speed: (throttle / 64) x top speed (0x10060491) */
    float target = throttle / 64.0f * g->top_speed;
    g->speed += (target - g->speed) / SIM_TAU_SPEED;              /* MEASURED lag */
    /* turn: 90 degrees per second at full deflection, x 0.857 when moving (MEASURED cos factor) */
    float rate = turn * 90.0f * (fabsf(g->speed) > 1.0f ? 0.8577f : 1.0f) * 1.1f;
    g->turn_rate += (rate - g->turn_rate) / SIM_TAU_TURN;         /* MEASURED lag */
    g->heading += g->turn_rate * (SIM_TICK_MS / 1000.0f);
    while (g->heading > 180) g->heading -= 360;
    while (g->heading < -180) g->heading += 360;
    /* the velocity follows the speed along the heading */
    float tx = g->speed * sinf(g->heading * DEG), tz = g->speed * cosf(g->heading * DEG);
    g->vx += (tx - g->vx) / SIM_TAU_VEL;                          /* MEASURED lag */
    g->vz += (tz - g->vz) / SIM_TAU_VEL;
    g->x += g->vx;
    g->z += g->vz;
    if (ground) g->y = ground(ctx, g->x, g->z) + g->stand;
}

float sim_gear_speed(const sim_gear *g)
{
    return sqrtf(g->vx * g->vx + g->vz * g->vz) * (1000.0f / SIM_TICK_MS);
}
