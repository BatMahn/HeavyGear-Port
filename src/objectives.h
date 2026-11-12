/*
 * objectives.h - the mission's objectives (MTBL) and its outcome, traced from HGSIM 0x10066090 .. 0x1006932f.
 *
 * Each group has a table of objectives (mission.h). Every sim second (181 ticks, 0x1006932f) every objective of
 * every group steps (0x100684db):
 *   - it is active when not yet done and its conditions hold (0x100683d6: all of them, or any one; a condition is
 *     another objective done 'C', succeeded 'S' or failed 'F'); start (0x10) and radio (0x8000..0x8002) objectives
 *     are always active. An active objective notes the time it became active.
 *   - targets: navpoints (1), vehicles (2) and structures (4) inside a BTAG named by the objective, whose mask meets
 *     the objective's, registered as they load (0x10066794).
 *   - by type: destroy (1, 2) succeeds once every target is destroyed; defend (4) fails as one is, succeeds when its
 *     time runs out; reach (8) when the group has visited a target nav, 0x20 also needs the group's other mandatory
 *     objectives done; go to (0x100) when the group's leader is within a target nav's radius (20000 when it gives
 *     none; aircraft measured flat); leave (0x2000) when it is outside every target nav; hit (0x4000) when a target
 *     was hit by the group; radio calls (0x8000..) wait for the player. Waits (0, 0x200, 0x400, 0x800) succeed when
 *     their time runs out. Others fail when their time runs out. Actions, done at once: 0x10000 the group (arg)
 *     wins, 0x20000 it loses, 0x40000 toggles an objective's display, 0x80000 / 0x100000 fail / complete one,
 *     0x200000 resets a group's objectives, 0x400000 one.
 *   - a group's mission: won when every mandatory ('M') objective succeeded, lost when one failed, or when its own
 *     time limit runs out.
 * The player's group winning or losing ends the mission.
 */
#ifndef HG_OBJECTIVES_H
#define HG_OBJECTIVES_H

#include "mission.h"

#define OBJ_MAX_TARGETS 0x28

enum { OBJ_PENDING = 3, OBJ_DONE = 5, OBJ_FAILED = 6, OBJ_FAILED_AI = 8 };
enum { OBJT_NAV = 1, OBJT_VEHICLE = 2, OBJT_STRUCTURE = 4 };
enum { GROUP_RUNNING = 0, GROUP_WON = 2, GROUP_LOST = 3, GROUP_TIMEOUT = 4 };

typedef struct {
    int state;                 /* OBJ_PENDING / DONE / FAILED / FAILED_AI */
    int start, end;            /* sim seconds; -1 unset */
    int ntgt;
    struct { int type, idx; } tgt[OBJ_MAX_TARGETS];
    int visible;
    int called;                /* a radio objective sent from the mission channel: its type is now 0x10000 (0x10009d0e) */
} obj_state;

/* what the objectives ask of the sim */
typedef struct {
    void *ctx;
    int  (*vehicle_dead)(void *ctx, int gps);            /* GPS record index (the player: -1 -> see player_gps) */
    int  (*structure_dead)(void *ctx, int tgt);
    /* the group's leader: 0 if it has none; *flat 1 for aircraft (classes 5 / 8, 0x10066eeb) */
    int  (*leader)(void *ctx, int group, float *x, float *y, float *z, int *flat);
    /* was the vehicle hit by a member of the group since last asked (0x10066b86)? */
    int  (*hit_by)(void *ctx, int gps, int group);
    /* an objective changed state (the original speaks its voice and shows "text completed / failed") */
    void (*announce)(void *ctx, int group, int obj, int state);
    /* the player's group first reached a nav that counts visits (0x100673af: AFXHDSAT at -25 dB); may be NULL */
    void (*nav_reached)(void *ctx, int nav);
} obj_world;

typedef struct {
    const mission *m;
    obj_state      st[16][64];
    int            gstate[16];
    int            gstart[16];
    int            gend[16];      /* the sim second the group was decided (0x10290c88) */
    int            current[16];   /* the group's first active order objective (0x1028fc20) */
    unsigned       nav_seen[0x80]; /* groups that visited each nav (nav +0x26) */
    int            now;           /* sim seconds */
    int            player_running; /* the player's vehicle has started (flag 0x2000, state 2): navs count visits */
} objectives;

/* the player's GPS record (role 0) isn't in the shipped records (the shell writes USERSTAR.BWD): pass the player's
 * mask (the shell's 0x4006 widened, 0x4007) and the index the sim uses for it */
void objectives_init(objectives *o, const mission *m, int player_gps, unsigned player_mask);
/* once a sim second; returns the player's group state (GROUP_*) */
int  objectives_tick(objectives *o, int sim_ticks, const obj_world *w, int player_group);
/* the objective's type as the sim sees it (a called radio objective reads 0x10000) */
unsigned objectives_type(const objectives *o, int g, int k);
/* the mission channel (0x100096c3 / 0x100097a7 / 0x1000988b): for slot 0..2 (radio type 0x8000 + slot) the player's
 * group's last shown objective of that type, -1 none; objectives_call sends it (0 can't be: the original's test) */
int  objectives_radio_slot(const objectives *o, int player_group, int slot);
void objectives_call(objectives *o, int player_group, int k);

#endif
