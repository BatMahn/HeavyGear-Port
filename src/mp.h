/*
 * mp.h - the sim's multiplayer messages (traced: HGSIM 0x10031389 .. 0x10035eb9, the loop 0x10031a86).
 *
 * Every message starts with a two-letter tag (the dispatcher compares it, 0x10031a86, with the strings at 0x1011015c
 * ..), then its fields, little-endian, unaligned. The ones the port sends and takes:
 * - "DA" (0x42 bytes, unreliable, every 0x24 ms - 0x10110138 - from 0x10032782): +2 the vehicle, +3 u16 its target
 *   (+0xec), +5 u16 its last attacker (+0x14c), +7 u16 flags (low nibble the gear's state +0x4f0; << 12 the gear's
 *   +0x562 low bits - 2 skating), +9 u8 flags, +0xa i32 the sender's sim clock, +0xe / +0x12 / +0x16 i32 its x, y, z
 *   (vehicle +0x54..), +0x1a / +0x1e / +0x22 its pitch, heading, roll (+0x60 / +0x64 / +0x68) x 65536, +0x26 / +0x2a
 *   i32 the gear's +0x520 / +0x588 (its standing height), +0x2e / +0x32 / +0x36 / +0x3a / +0x3e the gear's +0x2c
 *   (speed), +0x7c (the throttle's lag), +0x38 (the turn rate), +8, +0x18, x 65536. The receiver (0x10032cbe) keeps
 *   the newest clock per vehicle, puts the vehicle there and moves it on by its velocity times the smoothed lag
 *   (0.2 new + 0.8 old ticks, held to 0x24), unless that would put it under the ground (60); a vehicle silent for
 *   0x38e ms is dropped (0x10031a86: hidden, flags 0x4800). The master's DA also sets a joiner's sim clock.
 * - "WE" (5, unreliable, 0x10033dec): +2 the vehicle, +3 u16 the weapons that started firing since the last (the
 *   gear's +0xbf5 + slot x 4); the receiver (0x10033ec2) selects each and sets it firing (+8 = 2): every machine flies
 *   every shot.
 * - "DM" (0x16, reliable when changed, 0x100338e2): +2 the vehicle, +3 its last attacker, +5 seventeen zones, each the
 *   damage level 15 - (int)((internal / 2 + armor / 2) x 3 / zone +0x38) held to 0..15, | 0x10 once the zone's
 *   internal is gone; the receiver (0x10033a8a) shows the level and destroys the zones flagged.
 * - "KI" (0xc, reliable, 0x10035088): +2 ten bytes, the sender's deaths by each vehicle (0x1028cc2c[killer][victim],
 *   shorts, 10 x 10); 0x10035105 scores each vehicle: its kills of vehicles of other groups less those of its own.
 * - "GO" (2): a joiner tells the master it is in (0x10035673); the master, once every player's is in, sends it to
 *   all and they start (0x100356ce). "EG" (2, reliable, 0x100357d4): the mission is over (0x10035801).
 * Each vehicle is simulated by its own machine (damage and death are the owner's: 0x1005ab68 destroys only the
 * local vehicle); the others show it from its DA, fire its weapons from its WE and see its damage from its DM.
 *
 * The port's choices where the original's setup isn't reproduced (its multiplayer missions take their pieces from
 * the random mission generator and the shell's player stars, not traced): the players' vehicles follow the
 * mission's own units in slot order (the wire's vehicle index is that order), each slot its own team, the players
 * stood beside the mission's player start 1500 apart; the mission's own vehicles run on the master and are shown
 * elsewhere from its DA / DM (the original shares their AI over "AI" / "DI", 0x100344ef, not done).
 */
#ifndef HG_MP_H
#define HG_MP_H

#include "net.h"
#include "units.h"

typedef struct mp mp;

/* bind the session to a loaded mission: base = the mission's own units (units_spawn's count); the local player's
 * unit is added by the caller; mp_add_remotes adds the others */
mp  *mp_open(net *n, units *us, world *w, hg_archive *a, int base);
void mp_close(mp *m);
int  mp_slot(const mp *m);            /* my slot */
int  mp_count(const mp *m);           /* the players */
/* where slot k starts: the template's start<k> nav (multiplayer missions), else the mission's player start moved
 * sideways 1500 a slot (the port's choice); mp_has_starts: the mission has the start navs */
int  mp_has_starts(const mp *m);
void mp_start_pos(const mp *m, int slot, float *x, float *z, float heading);
/* the local player's unit and the others' gears (the same GDF for all: the port has no multiplayer gear screen) */
void mp_set_local(mp *m, int unit);
int  mp_add_remotes(mp *m, const char *gdf, const sim_gear *proto);
/* 1 once the master says go (0x100356ce); before it the caller's sim waits */
int  mp_started(const mp *m);
/* each frame: receive, dispatch, send (DA / WE / DM / KI), drop the silent */
void mp_frame(mp *m, int sim_clock, int *clock_out);
/* each sim tick after the local weapons' tick: the weapons that started firing; the remote vehicles' weapons */
void mp_tick(mp *m, combat_shots *s);
/* 1 if a unit is simulated on another machine (its damage is that machine's) */
int  mp_remote(const mp *m, int unit);
/* the local player's vehicle destroyed by `owner` (a unit, -1 none): counts the death, sends KI */
void mp_killed(mp *m, int victim, int owner);
/* the mission is over here: tell the others (EG) */
void mp_end(mp *m);
/* 1 once another machine ended it (EG) or the session is gone */
int  mp_ended(const mp *m);
/* the scores (0x10035105) by slot */
int  mp_score(const mp *m, int slot);
const char *mp_name(const mp *m, int slot);
/* the last message to show ("'name' destroyed" ...), NULL none; consumed */
const char *mp_message(mp *m);

#endif
