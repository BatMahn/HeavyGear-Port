/*
 * sim.h - the simulation (HGSIM), first part: a vehicle moving under its control (traced: HGSIM 0x10060491, the
 * control step, and 0x1005d75c, the move; the sim loop 0x10060296 runs the move (+0x30 of the vehicle record) then
 * the control (+0x34) once a frame, with the frame's ticks).
 *
 * Time runs in the sim's ticks, 5 ms each (HGSIM 0x1001fe62 / 0x1001ff23); a sim second is 181 ticks (0xb5, the
 * heading's rate divisor 0x1010134c and the lags' 0x100eb788). Distances are world units; speeds units per tick,
 * headings degrees (0 = +z, 90 = +x).
 *
 * The gear block's lags (0x10039c56 / 0x10039cad: cur += (target - cur) x ticks / T, stopping at the target; T is
 * seconds x 181, set by 0x1005cf76): +4 the torso's twist (0.4 s the player's, 0.2 others), +0x14 its tilt (0.2), +0x24
 * the speed (0.2), +0x34 the turn rate (0.3), +0x44 / +0x54 the roll / pitch (gears 0.7, class 8 0.6, class 10 0.9,
 * others 0.3), +0x64 the height (0.6), +0x74 the throttle (0.2). The move updates them first, in that order.
 *
 * The control step (0x10060491): the throttle's target is control +8 (0..1/64) x the speed factor (+0x500) + 1/64;
 * the player's, at 1/64 with the engine running, is 1/64 + 1/512 (0x10060f86: a test of |turn| >= 0 that always
 * holds). The speed's target is (throttle - 1/64) x the top speed (+0x4d4), class 8 x 4, halved and negated in
 * reverse (+0x59, toggled by reverse_direction +0x5a). The turn rate's target is turn / 1024 x 90 (classes 2 / 8:
 * 180) x cos(min(speed x 6, 80) degrees) (0x1006f3e6); skating or in a jump the angle doubles and the rate halves
 * (x 0.5). The heading turns by the rate / 181 a tick (so 1.105 x the rate per real second: the MEASURED "1.1").
 * Feet to torso (+0x1d, the player's): the legs turn at 55 degrees a second (25 skating) toward the torso while the
 * twist is at least 1 degree, the twist taking back what the legs turned. The twist's target is control +4,
 * held to +0x534 (180 for the player's gear, 0x100c4b81 -> 0x100c4d47).
 *
 * The move (0x1005d75c): on the ground, the velocity's target is the speed along the heading (x 2 skating, x 1.2 on
 * a 0x200 object worth 1000), reached at (target - velocity) / 45 a tick (/ the frame when longer); in the air or a
 * jump a quadratic drag instead (the air speed top x 2.5 / 64). The position takes (frame x a / 2 + v) x frame.
 * Gravity is 0.0296 a tick squared (0x10110088); a jump adds the jets' lift (0.148 x the torso's factor 0.25 /
 * 0.283 / 0.316 / 0.35, 0x1004e6a0), damped as it climbs ((g - lift) x 1448 x vy / 100000). On landing below the
 * ground the gear stands on it (y = ground + stand), vy = 0; a landing faster than 140 / 6 a tick plays AFXHGJLR
 * (0x1005366d) and, with the option at 0x1014a564 + 3, damages the legs. Gears' pitch and roll follow the ground's
 * polygon: pitch 90 - atan2(n.y, n.z), roll atan2(n.y, n.x) - 90 (degrees), held to +-10 for gears.
 * Skating (+0x562 bit 1): skate (+0x56) at a throttle of at most 1/128 starts the wheels (AFXHGWEN), and after +0x60b
 * ticks (0xb5) they run (AFXHGWHE); they stop (AFXHGWDE) when the throttle falls to 0.
 * Jump jets (+0x510 / +0x514 / +0x518): ten jumps of 0x2d4 ticks each; jumpjet_enabled (+0x1e) starts one
 * (AFXHGJJT looping, AFXHGJJF), which runs out its time.
 */
#ifndef HG_SIM_H
#define HG_SIM_H

typedef struct { float target, cur, T; } sim_lag;

/* the control block (the player's 0x101334d8; AI vehicles +0x29c), as the sim reads it */
typedef struct {
    float throttle;       /* +8 / (1/64): 0..1 */
    float turn;           /* +0xc / 1024: -1 (left) .. 1 */
    float torso_pan;      /* +4: degrees, -90..90 */
    float torso_tilt;     /* +0: degrees, -60..60 */
    int   reverse;        /* +0x5a: toggles reverse (+0x59) */
    int   skate;          /* +0x56 */
    int   jump;           /* +0x1e jumpjet_enabled */
    int   crouch;         /* +0x54 */
    int   standup;        /* +0x55 */
    int   feet_to_torso;  /* +0x1d */
    int   straffe_l, straffe_r;   /* +0x57 / +0x58 (held: Shift + the arrows): only the animations read them */
} sim_control;

/* events a step reports (the sounds the original plays at them; the caller plays them) */
enum {
    SIM_EV_SKATE_ENGAGE = 1,   /* AFXHGWEN */
    SIM_EV_SKATE_ON = 2,       /* AFXHGWHE, looping */
    SIM_EV_SKATE_OFF = 4,      /* AFXHGWDE (the loop stopped) */
    SIM_EV_JUMP_START = 8,     /* AFXHGJJT looping, AFXHGJJF */
    SIM_EV_JUMP_END = 16,      /* the loop stopped */
    SIM_EV_LAND_HARD = 32,     /* AFXHGJLR (0x1005366d); legs damaged with the option on */
    SIM_EV_CROUCH = 64,
    SIM_EV_STAND = 128,
};

typedef struct {
    float x, y, z;          /* the gear's origin (its standing height above the ground) */
    float heading;          /* degrees (the legs) */
    float turn_rate;        /* degrees per sim second (181 ticks) */
    float speed;            /* units per tick, along the heading (negative in reverse) */
    float vx, vz;           /* the velocity, units per tick */
    float vy;
    float throttle;         /* the control's 0..1 */
    float top_speed;        /* units per tick at full throttle x 64 (the gear's +0x4d4) */
    float stand;            /* the origin's height above the ground */
    int   start;            /* ticks waited to start walking */
    /* traced state */
    sim_lag twist, tilt, spd, turn, roll, pitch, thr;
    float   ai_tilt;          /* the AI's tilt control (+0, the fire controllers' 0x1000d64b), for sim_gear_tick_pan */
    int   vclass;           /* the vehicle class (1 gears) */
    int   player;           /* the player's (the creep, feet to torso, the twist's lag) */
    int   reverse;          /* +0x59 */
    int   skating;          /* +0x562 bit 1 */
    int   skate_wait;       /* +0x607 / +0x60f */
    int   skate_ticks;      /* +0x60b */
    int   crouched;         /* +0x562 bit 0x10 */
    int   on_ground;        /* vehicle +0x7c */
    int   jumps;            /* +0x514 (0: none; -1 no jets) */
    int   jump_time;        /* +0x510 */
    int   jumping;          /* +0x518 */
    float jump_lift;        /* +0x540 */
    int   feet;             /* feet to torso running (0x1013194c) */
    float last_heading;     /* 0x10148e24 */
    float speed_factor;     /* +0x500 */
    int   fall_damage;      /* the option at 0x1014a564 + 3 */
    float land_speed;       /* the last hard landing's -vy x 6 */
    unsigned body;          /* the gear block's +0x562 fall bits (ANIM_FALLF / DOWN / RISE / FALLB, src/anim.h) */
    int   legs_gone;        /* +0x560 & 0x20: a leg destroyed (no standing up) */
    int   per_tick;         /* stepped every tick (sim_gear_tick): no 1/16 velocity dead zone (see sim.c) */
} sim_gear;

typedef float (*sim_ground_fn)(void *ctx, float x, float z);
/* the ground with its polygon's normal (may be NULL: flat) */
typedef float (*sim_ground_n_fn)(void *ctx, float x, float z, float n[3]);

void sim_gear_init(sim_gear *g, float x, float y, float z, float heading, float top_speed, float stand);
/* the jets of a torso record (id: its +4, 0x1004e6a0's switch): 10 jumps, lift 0.148 x the torso's factor */
void sim_gear_jets(sim_gear *g, int has_jets, int torso_id);
/* one step of dt ticks (the move, then the control, as the original's frame); running: the engine runs (state 2);
 * returns SIM_EV_ bits */
int  sim_gear_step(sim_gear *g, const sim_control *c, int dt, int running, sim_ground_n_fn ground, void *ctx);
/* one 5 ms tick for the AI: throttle 0..1 (below 0: reverse), turn -1 (left) .. 1 (right) */
void sim_gear_tick(sim_gear *g, float throttle, float turn, sim_ground_fn ground, void *ctx);
/* the same with the torso's twist control (+4, degrees; the AI's weave and fire controllers) */
void sim_gear_tick_pan(sim_gear *g, float throttle, float turn, float pan, sim_ground_fn ground, void *ctx);
/* the speed in units per second */
float sim_gear_speed(const sim_gear *g);
/* the HUD's speed (0x100b336b): |velocity| x 6 x 1.38, truncated, negative with the speed */
int  sim_gear_kph(const sim_gear *g);
/* the view's yaw: the heading plus the torso's twist */
float sim_gear_view_yaw(const sim_gear *g);

#define SIM_TICK_MS 5
#define SIM_TICKS_PER_SECOND 181.0f
/* the Hunter's top speed: 7 speed points x 47.1744 (MEASURED 330.221 in the running original) */
#define SIM_HUNTER_TOP_SPEED 330.221f
#define SIM_SPEED_POINT 47.1744f
#define SIM_GRAVITY 0.0296f

#endif
