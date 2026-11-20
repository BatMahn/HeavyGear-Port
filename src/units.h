/*
 * units.h - the mission's other vehicles: placed from the GPS records at their group's start, drawn from their GDF
 * skeletons.
 *
 * Placement (traced): DoFirstObjtv (0x10066a68 -> 0x1004271a) puts each group's leader on its objective-0 navpoint
 * facing the nav's heading; the others go to their formation slots (0x100428c2): the group's formation (FTBL,
 * 0x100bfca4) slot offset, made relative to the leader's slot (0x10042603), moved through the leader's matrix
 * (0x10051940). A vehicle's slot is its order within its group (the GPS loader's per-group counter, 0x107c8ee4,
 * through 0x100425c9; units_spawn) and every group starts in formation 0 (the reset at 0x10042437 copies formation 0
 * to all 16 groups).
 */
#ifndef HG_UNITS_H
#define HG_UNITS_H

#include "combat.h"
#include "gearmodel.h"
#include "objectives.h"
#include "sim.h"
#include "world.h"

#define UNITS_MAX 64

typedef struct {
    char      gear[9];
    int       group, slot, leader;
    sim_gear  s;
    gearmodel gm;
    int       obj[GEARMODEL_MAX];   /* world object per part, -1 if its model is missing */
    int       placed;         /* 0: the group had no start navpoint */
    combatant cb;
    int       shown_dead;
    int       side;           /* the group's side (STAR +4) */
    int       target;         /* the unit it shoots at, or -1 */
    unsigned  vflags;         /* the vehicle record's +0x18 (the GPS's +0x1a; the shell's player 0x400, mates 0x480) */
    int       player;         /* the player's own gear: driven from outside, its parts not drawn (the cockpit view) */
    int       remote;         /* simulated on another machine (multiplayer, src/mp.h): no AI here, drawn */
    int       gps;            /* its GPS record (mission.gps), -1 for the player */
    unsigned  hit_groups;     /* groups whose shots struck it since the objectives last asked (vehicle +0x14c) */
    int       start_at;       /* the tick its engine is running (state 2, 0x100620b8: 0x43e + a random 0..0x169) */
    int       order;          /* the player's last order (a squad mate: UNIT_ORDER_*) */
    int       order_target;   /* the unit named by it, -1 none */
    float     order_x, order_z;   /* scout: the player's nav it was sent to; flank: its point */
    float     order_r;        /* scout: the nav's radius (at least 100, 0x1000d1f1) */
    int       order_pt;       /* flank: the point is set (0 makes it on the next tick) */
    int       voice;          /* a wingman's record voice (+0x16, src/squad.h) */
    int       react;          /* the AI's decision range (+0x1b2: GPS +8, 0 read as 1, + 1) */
    int       skill;          /* its skill table row (+0x1b5: GPS +0x14, 0 read as 5) */
    int       next_decide;    /* the tick of its fire controller's next decision (+0x1ba) */
    float     turn_ctl;       /* its last turn control (-1..1: the control block's +0xc / 1024) */
    float     aim_pitch;      /* the player's: its view's pitch, degrees (set by the caller; the engage test) */
    /* the combat AI's state machine (0x100a58c0; the vehicle's +0x1f4 ..): its state (-1 to choose), the last one,
     * the state forced next (+0x1f8, -1 none), the state's end tick (+0x1fc), its own timers (+0x200 / +0x200 + 0x18),
     * its step (+0x214), a side or slot (+0x214's neighbour +0x85 x 4), its point and the last distance (+0x264) */
    int       ai_state, ai_prev, ai_next, ai_end, ai_t1, ai_t2, ai_step, ai_side;
    float     ai_px, ai_pz, ai_lastd;
    int       ai_pt;          /* ai_px / ai_pz hold a point */
    int       ai_reverse;     /* the control's +0x59: backwards (half speed) */
    /* the mode (0x1000a811 .. 0x1000e7c9, units.c "Modes and their rules"): +0x1a2 the mode, +0x1a6 its target and
     * +0x1a4 its point (codes: 0x200 | unit, 0x100 | nav, 0x1ff the vehicle's own waypoint, 0x400 | structure), the
     * one-deep stack +0x17c / +0x194, the rule sets in its three slots (+0x170 ..: 0 none, else ai_sets[k]), the
     * mode's active rules (+0x16c, 0x1000ab37), the ranges +0x1ce / +0x1c6 / +0x1ca (GPS +0xa / +0xc / +0xe, 0 read
     * as 250, x 100), the flags +0x1aa and +0x1ae, a leader's pending request +0x19c / +0x19e */
    int       ai_init;
    int       mode, mtgt, marg;
    int       stk_n, stk_mode, stk_tgt;
    int       rset[3];
    int       nrule;
    const struct ai_rule_s *rule[24];
    int       r_1ce, r_1c6, r_1ca;
    unsigned  f1aa;
    int       f1ae;
    int       req_cond, req_code;
    float     wp_x, wp_y, wp_z, wp_r;   /* its own waypoint (0x100af430: code 0x1ff) */
    float     pat[4][3];      /* mode 7's four waypoints about its target (0x1000e963) */
    int       pat_i, pat_k;   /* the one it heads for; +0x218 */
    int       order_n, order_done_n;   /* order_n: counted up by the caller with each order (applied once) */
    int       order_last, order_last_tgt;
    float     order_last_x;
    anim_player ap;           /* its animation (classes 1 / 9 / 5; 0x10013d53) */
    float     reach;          /* the farthest its parts' boxes reach from its origin, any axis (units_hit's cull; 0 not
                               * yet measured) */
    int       body_seen;      /* the fall bits it had (the fall's sound as they come) */
    float     thr_ctl;        /* its last throttle (the control's +8, held while a rule changes the mode) */
    /* the obstacle probe (0x100aa2ac): its avoidance turn's sign (+0x25a, 0 none), the object it avoids (+0x246), the
     * steep ground's point (+0x24a), the next probe's tick (+0x256), the probe's length factor (+0x25c), the controls
     * it set (held between probes) */
    int       av_dir, av_obj, av_next;
    float     av_pt[3], av_len, av_turn, av_thr;
    /* aircraft (classes 5 / 8) holding their height (0x10010e31 / 0x10010d9c and 0x1005d75c's flight branch): the
     * height wanted over the ground (+0x23a), the offset the ground's steps and the probe add (+0x23e), the landed
     * height (+0x588), the height's lag (gear block +0x64 .., 0.6 s), the last ground under it (+0x78) */
    float     alt, alt_off, alt_land, alt_cur, alt_gprev;
    int       alt_init;
    /* the torso: its twist control (+4, degrees; the weave 0x10010aa3 and the fire controllers 0x10005570 /
     * 0x10005a87 set it), the weave's next step (+0x1fc) and side (+0x214) */
    float     pan_ctl;
    int       cover;          /* the cover point mode 0xf walks to (+0x236), -1 none */
    int       weave_next, weave_side;
    /* the cover states (14 / 16 / 17 / 22 / 23, 0x100a8aa4 .. 0x100a9095): the outcome (+0x228: 1 got there, -1
     * failed), the point reached (+0x22a), the search radius (+0x218), the peek / walk timers (+0x21c, +0x220,
     * +0x224), shots fired in a peek (+0x218), the stance the state asks (the control's +0x54 crouch, +0x57 / +0x58
     * straffe: -1 left, 1 right), the table in use (+0x278: 0 none yet, 1 the class's, 2 the cover table) */
    int       ai_out, ai_got, ai_r, ai_tm[3], ai_shots, ai_crouch, ai_straffe, ai_tab, ai_fired;
} unit;

/* the squad mate channel's orders (the menu's items, HGSIM 0x101063cc ..: attack, flank, defend, scout - sent to the
 * player's nav -, get to cover, return to formation; disengage and engage at will are the other menus' items). An
 * order sets the vehicle's mode (+0x1a2, 0x1000f2b0 -> 0x1000e7c9: attack 2, defend 7, flank 0xd, scout 0xe, cover
 * 0xf, formation 5) run by 0x1000cb8f (units.c). The GPS records' AI names ("mmb00std" ...) are copied by the loader
 * (0x100c40a8) and never read: no AI file exists. */
enum { UNIT_ORDER_NONE, UNIT_ORDER_ATTACK, UNIT_ORDER_FLANK, UNIT_ORDER_DEFEND, UNIT_ORDER_SCOUT, UNIT_ORDER_COVER,
       UNIT_ORDER_FORM, UNIT_ORDER_DISENGAGE, UNIT_ORDER_ENGAGE };

typedef struct {
    int  n;
    int  ticks;               /* sim ticks run by units_ai_tick */
    const combat_shots *cs;   /* the shots in flight this tick (the cover states' 0x100abf2d) */
    int    view_set, view_w, view_h;   /* the last drawn view (units_set_view: the animations' off-screen test) */
    camera view;
    float  view_far;
    int  lead[16];            /* each group's leader (0x107c8ee0 + group x 0x6c), -1 none */
    int  frame, frame_pending; /* a frame began (units_frame): the AI's once-a-frame rolls run on the next tick */
    int  pgroup;              /* the player's GPS group (the player's unit counts as in it) */
    int  gobj[16];            /* each group's current objective as the modes last saw it (0x1000f7d8 on a change) */
    unsigned char gseen[16][64];   /* navs (0..255) / vehicles (256 + i) a group has reached (0x100b05db's 0x20 and
                                    * group bits, bit by bit) */
    float spos[64][3];        /* the structures' positions the goals name (0x400 | k) */
    /* the cover points (0x107ee7e4, 0x44 bytes each: 0x100274d8): their place (+0x20) and box (+0x2c), whether one is
     * taken (+0x10) and by whom (+0x14) */
    int   ncov, cov_loaded;
    float cov_pos[200][3], cov_box[200][6];
    int   cov_taken[200], cov_by[200];
    /* the squad's radio replies the AI raised (0x10017566 codes: 4 a target destroyed, by 0x1000cace for a vehicle
     * whose group the player leads); the caller speaks and clears them (units_take_reply) */
    int  nreply;
    struct { int unit, code; } reply[8];
    unit u[UNITS_MAX];
} units;

/* the sim's random numbers (0x10089e69): the next of the 127 made from srand(0), modulo n */
int  units_rand(int n);

/* every GPS record except the player's (role 0) */
/* the player's group's internal multiplier (the difficulty's, 0x1004e4cc; 6 by default) for the next spawn */
void units_set_internal_scale(float k);
void units_spawn(units *us, world *w, hg_archive *a);
/* moves the parts to the units' current positions (destroyed units stay as wrecks, still) */
void units_update(units *us, world *w);
/* One 5 ms tick of the other vehicles' minds (0x1000a811 each tick). Traced: the modes and their rules (src/ai_rules.h,
 * units.c "Modes and their rules": 0x1000a988, 0x1000ab37, 0x1000e7c9, 0x1000d9de, 0x1000d70f, the conditions
 * 0x1000c427 / 0x1000c502 / 0x1000c1ee / 0x1000cace / 0x1000c9d8, the actions, fighting back 0x1000adb8), the group's
 * objective handing (0x1000f7d8, 0x10010412, 0x1000ff96), the modes' functions (0x1000cb29 / 0x1000cb8f /
 * 0x1000cf4d), the weapon choice (0x100073b7, 0x100060a7, 0x10006473), the attack state's steering and throttle
 * (0x100a7e40: 0x1000d679, 0x1000d2bd, 0x100a7d9b), the fire controllers (0x10005a87 / 0x10005570 by the skill
 * table, their decision timer, window and aim-ray test 0x10004d60), the lock roll (0x10006672), the skill table
 * (0x10107c0c via 0x1000a52e), version 1 vehicles' top speed (0x1004f86d / 0x10050db8), the combat AI's state
 * machine (0x100a58c0: its tables, choice, timers and states) and the sim's random numbers (0x10089e69). The
 * player's unit is left alone. */
/* marks a frame (the caller, once per drawn frame): the original runs its AI once a frame (0x1000a811) */
void units_frame(units *us);
/* once a frame with its ticks (0x10013d53, after the moves' picks 0x1003fbb7): every gear's, infantry's and
 * aircraft's animation picked, stepped and posed; pc: the player's control (straffe), may be NULL. Returns the
 * player's events: 1 a foot came down (AFXHGWKL, 0x1003fbb7), 2 it fell (AFXHGFLS) */
int  units_animate(units *us, hg_archive *a, int ticks, const sim_control *pc);
/* per group: its current objective (0x1028fc20) as the modes need it: its index (-1 none), type (+2), how many
 * vehicles each target takes (+0x79, the MTBL's +0x30; 0 all) and its AI flag (+0x7d, the MTBL's +0x31: 1 / 2 set
 * +0x1aa bits 2 / 1, 0x1000fbf9), its targets (kind OBJT_NAV 1 / OBJT_VEHICLE 2 (a unit index) / OBJT_STRUCTURE 4,
 * the structure's position and whether it is down). go / x / y / z: the first nav of a go-to (0x100) objective. */
typedef struct {
    int go; float x, y, z;
    int obj;
    unsigned type;
    int count, aiflag;
    int reach;            /* 0x2100 (0x10069aac): the first target of the group's first reach (0x20) objective, as
                           * a code (0x100 | nav), 0 none */
    int ntgt;
    struct { int kind, idx, done; float x, y, z; } tgt[16];
} units_goal;
/* the next reply the AI raised (unit index, 0x10017566 code); 0 when none */
int  units_take_reply(units *us, int *unit, int *code);
void units_ai_tick(units *us, world *w, const units_goal goal[16], combat_shots *s);

/* The aim distance (the vehicle record's lag at +0xa8: +0xac its target, +0xb0 the distance, 0.2 s; 0x1001fa46
 * starts it at 50000; 0x100bcd98 each move): the 150000 ray from the eye along the aim (0x100bcf44) striking a
 * vehicle (0x100) or a structure (0x200) first sets +0xc0 to its distance (2000 at least), kept otherwise (0 at the
 * start); the target is +0xc0, stepped by 0x10039bd2 (integer: (target - cur) x ticks / T, T = 36). The shots and
 * the reticle aim at that distance along the ray (0x10055de4, 0x100bcec7). */
typedef struct { int last, target, cur, T, c0, init; } units_aimd;
/* hit: the struck vehicle's / structure's distance, or -1; now: sim ticks. Returns the aim distance. */
float units_aim_dist(units_aimd *a, int now, float hit);
/* the view last drawn, for units_animate's test (0x10013d53: a vehicle but the player's whose sphere - its standing
 * height at the focal length over the depth - lies 25 pixels off the screen or past the far distance doesn't step its
 * animation, unless it plays one of the ids 4 .. 8, 0xd, 0x11, 0x15, 0x17 .. 0x19, 0x10013cd8) */
void units_set_view(units *us, const camera *cam, int w, int h, float far_dist);
/* the goals from the objectives' state: each group's current objective (0x1028fc20), its type, count, flag and
 * targets (vehicles as unit indices; structures where their models stand, down when hidden) */
void units_goals(units_goal goal[16], const objectives *o, const units *us, const world *w);

/* the player's gear as a unit (hit like the others, never drawn): its index, -1 on failure. internal_scale: the
 * player's internal points read 6 x the table's in the running original (MEASURED, not traced). */
int  units_add_player(units *us, world *w, hg_archive *a, const char *gdf, int side, float internal_scale,
                      const sim_gear *g);

/* the unit made from GPS record `gps` (-1: the player's), or -1 */
/* a wingman of the player's group (the launch's USERSTAR record: group 0, role 2) in squad slot `slot` (1, 2) */
int  units_add_mate(units *us, world *w, hg_archive *a, const char *gdf, int slot, int voice);
int  units_by_gps(const units *us, int gps);
/* a group's leader (0x107c8ee0): its GPS leader (0x10042603 from the loader); when the leader is destroyed, or a
 * group has none, the first live member in the group's order that isn't the old leader (0x1000b9d3, from
 * 0x10059e9d / 0x100429f6); -1 if none */
int  units_leader(const units *us, int group);

/* the first unit part the segment p0 -> p1 passes through (not `owner`'s, not dead): the unit's index, the fraction
 * along the segment and the part's zone; -1 if none. Parts are their models' boxes, turned with the unit. */
int  units_hit(const units *us, const world *w, int owner, const float p0[3], const float p1[3], float *t, int *zone);

#endif
