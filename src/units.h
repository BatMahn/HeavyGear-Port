/*
 * units.h - the mission's other vehicles: placed from the GPS records at their group's start, drawn from their GDF
 * skeletons.
 *
 * Placement (traced): DoFirstObjtv (0x10066a68 -> 0x1004271a) puts each group's leader on its objective-0 navpoint
 * facing the nav's heading; the others go to their formation slots (0x100428c2): the group's formation (FTBL,
 * 0x100bfca4) slot offset, made relative to the leader's slot (0x10042603), moved through the leader's matrix
 * (0x10051940). A vehicle's slot is its order within its group (the GPS loader's per-group counter, 0x107c8ee4;
 * INFERRED from the decompile, the call's argument isn't visible) and every group starts in formation 0 (the reset at
 * 0x10042437 copies formation 0 to all 16 groups).
 */
#ifndef HG_UNITS_H
#define HG_UNITS_H

#include "combat.h"
#include "gearmodel.h"
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
    int       player;         /* the player's own gear: driven from outside, its parts not drawn (the cockpit view) */
    int       gps;            /* its GPS record (mission.gps), -1 for the player */
    unsigned  hit_groups;     /* groups whose shots struck it since the objectives last asked (vehicle +0x14c) */
    int       start_at;       /* the tick its engine is running (state 2, 0x100620b8: 0x43e + a random 0..0x169) */
    int       order;          /* the player's last order (a squad mate: UNIT_ORDER_*) */
    int       order_target;   /* the unit named by it, -1 none */
    float     order_x, order_z;   /* scout: the player's nav it was sent to */
    int       voice;          /* a wingman's record voice (+0x16, src/squad.h) */
} unit;

/* the squad mate channel's orders (the menu's items, HGSIM 0x101063cc ..: attack, flank, defend, scout - sent to the
 * player's nav -, get to cover, return to formation; disengage and engage at will are the other menus' items): their
 * AI is INFERRED (the original's AI files aren't traced) */
enum { UNIT_ORDER_NONE, UNIT_ORDER_ATTACK, UNIT_ORDER_FLANK, UNIT_ORDER_DEFEND, UNIT_ORDER_SCOUT, UNIT_ORDER_COVER,
       UNIT_ORDER_FORM, UNIT_ORDER_DISENGAGE, UNIT_ORDER_ENGAGE };

typedef struct {
    int  n;
    int  ticks;               /* sim ticks run by units_ai_tick */
    unit u[UNITS_MAX];
} units;

/* every GPS record except the player's (role 0) */
void units_spawn(units *us, world *w, hg_archive *a);
/* moves the parts to the units' current positions (destroyed units stay as wrecks, still) */
void units_update(units *us, world *w);
/* One 5 ms tick of the other vehicles' minds. INFERRED throughout (the AI, the GPS records' "mmb00std"-style AI
 * files, is not traced yet): each armed vehicle picks the nearest live vehicle of another side (the player
 * included) within its selected weapon's reach (speed x life), turns toward it in place and holds the trigger while
 * facing it within 5 degrees, aiming at its middle. With nothing to shoot, it drives toward its group's goal (the
 * nav of the group's current objective when that is a go-to: the objectives decide where, traced; the steering is
 * INFERRED). The player's unit is left alone. */
typedef struct { int go; float x, y, z; } units_goal;   /* per group: the nav its current go-to objective names */
void units_ai_tick(units *us, world *w, const units_goal goal[16], combat_shots *s);

/* the player's gear as a unit (hit like the others, never drawn): its index, -1 on failure. internal_scale: the
 * player's internal points read 6 x the table's in the running original (MEASURED, not traced). */
int  units_add_player(units *us, world *w, hg_archive *a, const char *gdf, int side, float internal_scale,
                      const sim_gear *g);

/* the unit made from GPS record `gps` (-1: the player's), or -1 */
/* a wingman of the player's group (the launch's USERSTAR record: group 0, role 2) in squad slot `slot` (1, 2) */
int  units_add_mate(units *us, world *w, hg_archive *a, const char *gdf, int slot, int voice);
int  units_by_gps(const units *us, int gps);
/* a group's leader: its GPS leader (the player for group 0), else its first live member (INFERRED); -1 if none */
int  units_leader(const units *us, int group);

/* the first unit part the segment p0 -> p1 passes through (not `owner`'s, not dead): the unit's index, the fraction
 * along the segment and the part's zone; -1 if none. Parts are their models' boxes, turned with the unit. */
int  units_hit(const units *us, const world *w, int owner, const float p0[3], const float p1[3], float *t, int *zone);

#endif
