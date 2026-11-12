/*
 * sim.h - the simulation (HGSIM), first part: the player's gear moving under throttle and turning.
 *
 * Time runs in the sim's ticks, 5 ms each (HGSIM 0x1001fe62 / 0x1001ff23: the clock counts 5 ms periods and a
 * frame's delta is in those). Distances are world units; speeds are units per tick, headings degrees (0 = +z).
 *
 * Traced (HGSIM 0x10060491, the gear's control step): the throttle's target is the control's throttle times the
 * gear's +0x500 factor (1) plus 1/64, and the target speed is (throttle - 1/64) x the gear's top speed (+0x4d4); a
 * full throttle is 1/64, so top speed / 64 per tick. The top speed is the gear's speed points x 47.1744 (0x1004e6a0).
 * The turn's target rate is the control's turn (-1024..1024) / 1024 x 90 degrees per second, times cos() of an angle
 * (0x1006f3e6).
 * MEASURED in the running original (mission 1, the Hunter, keys 1-0 and the arrows; tools/ref/simlog.py): the
 * Hunter's top speed is 330.221 (7 points); the speed, the velocity and the turn rate approach their targets as
 * first-order lags of 45, 125 and 54 ticks; the turn's cos() factor is 1 standing and 0.8577 moving, and the heading
 * turns at 1.1 x the gear's rate (+0x38, 85 degrees per second at full speed; the 1.1 is 0x101013a8). Driven with the
 * same inputs, the port's path stays within 80 units of the original's over 4,000 (a full-throttle run with a
 * 1.5-second turn). The full equations (FUN_1005d75c's
 * integration, the walk start, the legs / torso split) are still to trace: see KNOWN_ISSUES.
 */
#ifndef HG_SIM_H
#define HG_SIM_H

typedef struct {
    float x, y, z;          /* the gear's origin (its standing height above the ground) */
    float heading;          /* degrees */
    float turn_rate;        /* degrees per second */
    float speed;            /* units per tick, along the heading */
    float vx, vz;           /* the velocity, units per tick */
    float throttle;         /* 0..1 */
    float top_speed;        /* units per tick at full throttle x 64 (the gear's +0x4d4) */
    float stand;            /* the origin's height above the ground */
    int   start;            /* ticks waited to start walking */
} sim_gear;

typedef float (*sim_ground_fn)(void *ctx, float x, float z);

void sim_gear_init(sim_gear *g, float x, float y, float z, float heading, float top_speed, float stand);
/* one 5 ms tick: throttle 0..1, turn -1 (left) .. 1 (right) */
void sim_gear_tick(sim_gear *g, float throttle, float turn, sim_ground_fn ground, void *ctx);
/* the speed in units per second */
float sim_gear_speed(const sim_gear *g);

#define SIM_TICK_MS 5
/* the Hunter's top speed: 7 speed points x 47.1744 (MEASURED 330.221 in the running original) */
#define SIM_HUNTER_TOP_SPEED 330.221f
#define SIM_SPEED_POINT 47.1744f

#endif
