/*
 * gear.h - a gear's definition (GDF) - so far its weapon list.
 *
 * GDF (loader HGSIM 0x1004c8c4): u32 0, ... header to 0x88 with the part count at +0x84; the parts (0x438 bytes each);
 * u32 version (1 or 2); a 0x3c-byte header whose +0x24 is the equipment count; the equipment records, 0x6c bytes each:
 * i32 kind, i32 id, ... The gear builder (0x1004e6a0) turns every record of kind 8..16 into a weapon slot, in order:
 * its weapon type and ammunition come from the sim's equipment tables by id (gear_tables.h).
 *
 * Zones (version 2): record kind k (1..16) is zone k. Armor = the record's +8 (+5 / +10 by the header's +0x38 bits 1 /
 * 2). Internal: kinds 1 and 8..16 the equipment table's points by id; 2 (3 options), 3 / 4 and 5 / 6 (5 options): the
 * points plus each option's add, times each option's multiplier, truncated; the option indices are 4-bit fields of the
 * record's +0xc plus a base per field (0x1003d3b9..., 0x1003d977..., 0x1003dce6...); 7: +0xc's low 16 bits, over 950
 * only a fifth counts (0x1003d744). The builder then multiplies every zone's internal by an overload factor
 * (0x1003d0a5; taken as 1, the AI gears checked read their table values). Version 1: the record's kind picks the zone
 * (the switch in 0x1004f86d), armor = +8, internal = +0xc (plus, for some ids, a bonus not modelled).
 * Checked against the running original (mission 1): every zone of the AI Hunter, Jaguar, Antelope, Redjacket, Orca
 * and infantry. The player's Hunter reads 6 x the internal points (not traced).
 */
#ifndef HG_GEAR_H
#define HG_GEAR_H

#include "hgprj.h"

#define GEAR_MAX_WEAPONS 8   /* the gear block's slots (0x1002bb2b) */

typedef struct {
    int type;          /* weapon type (index into the sim's weapon table, 0x10119382 + type * 0xd6) */
    int ammo;
    int kind;          /* the equipment record's kind (8..16: where it is mounted) */
    int power_bonus;   /* added to its shots' power (the slot's +0x28, by the ammunition: 0, 5 or 10) */
    const char *name;  /* the HUD's short name, e.g. "LAC" */
    const char *icon;  /* the HUD icon shape */
    const char *icon_hit;   /* the damaged one (the weapon table's next name, +0x10) */
} gear_weapon;

#define GEAR_ZONES 17

typedef struct {
    int         nweapons;
    gear_weapon weapon[GEAR_MAX_WEAPONS];
    /* damage zones 1..17 (index zone - 1): armor and internal points, as the builders set them (0x1004e6a0 for
     * version 2 GDFs, gears; 0x1004f86d for version 1, vehicles and infantry). has_zone 0: the GDF gives none. */
    float       armor[GEAR_ZONES], internal[GEAR_ZONES];
    int         has_zone[GEAR_ZONES];
    int         version;     /* the GDF's: 2 gears, 1 vehicles and infantry */
    int         crit_type;   /* the critical-hit table (+0xbdb, 1..5 by the torso record's id, 0x1004e6a0); 0 none */
    int         torso_id;    /* the torso record's id (+4; its jets' lift factor, 0x1004e6a0), -1 none */
    /* the builder's sums (version 2; RE_NOTES "Gear design"): top speed = speed_points x 47.1744 (+0x4d4), stealth
     * (+0xbdf) and sensors (+0xbe3) for the radar, handling (+0xbd7), the load and the internal factor it gives */
    int         speed_points, stealth, sensors, handling, weight, capacity, head_flags;
    int         power_spare;   /* +0xbbf: the torso's second option field is 9 */
    int         jump_jets;     /* the torso's option bit 12 (0x1003d3e9): the gear block's +0x514 = 10 jumps */
    float       internal_factor;
} gear;

/* the weapon table's fields the HUD reads (0x101192f0 + type x 0xd6): the shot kind (+0x10), guided (+0x86 bit 0),
 * the reticle's lock band (+0x4c .. +0x50) */
int   gear_weapon_kind(int type);
int   gear_weapon_guided(int type);
float gear_weapon_min_range(int type);
float gear_weapon_max_range(int type);

/* load gear `name` (e.g. "gdhuntea"); 0 on success */
int gear_load(hg_archive *a, const char *name, gear *g);

#endif
