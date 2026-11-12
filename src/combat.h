/*
 * combat.h - weapons, shots and damage.
 *
 * Weapon slots (the per-tick step 0x100ba550, times in 5 ms ticks): a ready slot (state 1) whose trigger is pulled
 * starts a burst (state 2) of `burst` shots (+0x2c); a shot leaves every `interval` ticks (+0x58), the first one
 * interval ticks after the pull, each using one round. When the burst is spent the slot waits `reload` ticks (+0x54,
 * state 3), then fires again while the trigger is held (weapons with +0x20 set) or goes back to ready. A slot whose
 * ammunition runs out is empty (state -1).
 *
 * Shots (0x100bb425; a pool of 0x200, 0x1078d0e0): the velocity is the aim direction times the weapon's speed (+0x3c,
 * units per tick), the vertical acceleration -0.0296 x +0x38 per tick squared, the life +0x5c ticks; they carry the
 * weapon's power (+0x78), multiplier (+0x7c), chip damage (+0x80) and mode (+0x84). Each tick (0x100191d7): position
 * += (acceleration / 2 + velocity), velocity += acceleration; the segment is tested against vehicles and the ground.
 * Beams (the 0x100baf31 weapons: lasers, rail guns, particle accelerators) don't fly: every frame of their life the
 * ray from the vehicle's mount along its aim, as they are then, is tested out to the weapon's reach (+0x50,
 * 0x10018a9c), harming what it strikes each time; rail guns and particle accelerators (kinds 4 / 6) end after one
 * test, lasers (kind 2) last their life (combat_beams_frame).
 *
 * Damage (0x1005c1f8), on the zone hit: penetration = power - armor (modes 3 and 4: power - armor / 2). Penetrating:
 * d = (int)(penetration x multiplier); while the zone has armor, mode 1 takes d off the armor, mode 2 d / 2 off armor
 * and internal, modes 3 / 4 d off the internal; armor below zero spills into the internal. Not penetrating: the chip
 * damage comes off the armor (spilling the same way). A zone whose internal falls to 0 is destroyed (0x1005ab68): the
 * head (1) destroys gears and infantry (classes 1 / 9; other vehicles only lose the part); the torso (2) and zone 7 destroy zones 3, 4 and 1 and the vehicle; zones 8 and
 * 17 destroy the vehicle; arms (3, 4) and weapons (9..16) come off; legs (5, 6) stop it (+0x500 = 0); gears and infantry fall and crawl.
 */
#ifndef HG_COMBAT_H
#define HG_COMBAT_H

#include "gear.h"
#include "hgprj.h"

#define COMBAT_MAX_SHOTS 0x200

typedef struct {
    gear  gr;                       /* weapons and the zones as built */
    float armor[GEAR_ZONES], internal[GEAR_ZONES];
    int   ztotal[GEAR_ZONES];        /* zone +0x38: (internal + armor) / 10 as built (0x1004e6a0), the damage
                                       * display's scale */
    int   zone_gone[GEAR_ZONES];
    int   dead;
    int   crit_zones;               /* zones destroyed by a critical hit (0x1b8a; the player's is HGCAR +0x138) */
    int   invulnerable;             /* hgdif +1 for the player (0x1005c1f8 / 0x10059e9d skip its damage and death) */
    int   unlimited_ammo;           /* hgdif +0 for the player (0x100bb425 / 0x100baf31 spend no rounds) */
    int   vclass;                   /* the vehicle class (gearmodel's; 1 by default): 1 / 9 die by the head */
    int   strike_zone;              /* a guided shot's strike (0x8000) takes the zone of the vehicle's root part (its
                                     * model's +0x8c object's +0x26, 0x100191d7); 2 by default, 0 none */
    int   sel;                      /* the selected weapon */
    /* fire groups (the slots' +0xc): with fire_all a pull fires every ready weapon of the selected one's group whose
     * type's +8 has 4 (the control +0x28: 0x100bc53d cycling by 0x100bbcef); the AI's arm-all (0x10006473) puts
     * the weapons it arms in group 1 */
    int   wgroup[GEAR_MAX_WEAPONS];
    int   fire_all;
    int   state[GEAR_MAX_WEAPONS], left[GEAR_MAX_WEAPONS], timer[GEAR_MAX_WEAPONS], ammo[GEAR_MAX_WEAPONS];
    int   trigger_was;
    int   pierced;                  /* the last hit went through the armor (0x1005c1f8's penetration >= 1) */
    int   lock;                     /* the vehicle its guided shots home on (the slot's +0x18), -1 none: the caller's */
    /* critical hits (gears only, 0x1005b5c6): the zones' crit slots (+0x617, +0xb67) */
    int   crit_n[GEAR_ZONES + 1];
    unsigned short crit[GEAR_ZONES + 1][20];
    float speed_factor;             /* +0x500: the throttle's factor (engine, legs, hydraulics) */
    float sensor_factor;            /* +0xbe3 halved / cleared by sensor hits */
    int   crippled;                 /* a leg destroyed */
    unsigned fall;                  /* gear block +0x562 bits a leg's loss sets (0x1005ab68: falling forward 4 or back
                                     * 0x400 by the shared rand(2), gears' sole 0x4000 / 0x2000); the animation takes them */
    int   legs_gone;                /* +0x560 & 0x20 */
    int   knock;                    /* +0xc15: the 0x1e46 crit knocks a gear or infantry down (0x1005b5c6) */
    int   hyd_spare;                /* +0xbbb */
    int   stun;                     /* +0xc3d: ticks left of a power loss (0x1eaa: 0x21f); no moving, turning or
                                     * firing till then (0x10060491, 0x100bb425); the caller counts it down */
    /* the gear computer's reports for this vehicle (the player's): subject and verdict (RE_NOTES "Voices"); NULL none */
    void (*report)(void *ctx, int subject, int verdict);
    void *report_ctx;
} combatant;

typedef struct {
    int   active;
    int   owner;                    /* the firing vehicle's id (the caller's numbering) */
    int   type;                     /* weapon type */
    int   power;                    /* the type's power plus the slot's bonus */
    float x, y, z, vx, vy, vz, ax, ay, az;
    int   life;
    int   target;                   /* a guided shot's target (the world's vehicle id), -1 none */
    int   age;                      /* ticks flown (the shot's +0x30) */
    float arm;                      /* guidance starts once age > arm (+0x82): 0, or for lofted guided weapons (+0x86
                                     * bits 0x21) the flight time to the target's height (0x100af2a0); < 0 not set */
    int   beam;                     /* a beam (the 0x14-slot pool 0x107b7ce0): x, y, z its mount, dir its aim */
    float dir[3];
    float end[3];                   /* a beam: where its ray ended this frame (what it struck, the ground, its reach) */
    unsigned serial;                /* which firing this is (combat_shots.serial when it left) */
    unsigned flags;                 /* the shot's +2: 0x400 it struck the ground (a bouncing shell's, kept) */
    int   motion;                   /* the weapon's +0x76: 3 a bouncing shell (0x100191d7), else 0 (0x10019f1d) */
} combat_shot;

/* what the shots report (sounds and effects); any may be NULL */
typedef struct {
    void *ctx;
    /* a shot left: the shooter, the weapon type, the muzzle */
    void (*fire)(void *ctx, int owner, int type, const float at[3]);
    /* a shot struck: the shooter, the weapon type, what it hit (-1 the ground, else the id the world's hit() gave),
     * where; pen: a vehicle's armor was pierced (1) or not (0), -1 not a vehicle */
    void (*impact)(void *ctx, int owner, int type, int victim, int pen, const float at[3]);
    /* a shot destroyed a vehicle: the victim, the shooter */
    void (*killed)(void *ctx, int victim, int owner);
    /* a bouncing shell glanced off the ground (an object flagged 0x800): where, its velocity after (0x1008514d) */
    void (*spray)(void *ctx, int owner, int type, const float at[3], const float v[3]);
} combat_events;

typedef struct {
    combat_shot shot[COMBAT_MAX_SHOTS];
    combat_events ev;
    int beams_by_frame;             /* 1: combat_shots_tick leaves the beams to combat_beams_frame (the original's
                                     * pace, once a frame); 0: they are tested every tick */
    unsigned serial;                /* counts the shots fired */
    /* the shells' objects (0x100bb425: a shell needs a free object of its kind's pool, else it isn't fired, its
     * round spent all the same; 0x100bd1e7 sets it twice its model's radius ahead of the mount): any may be NULL
     * (no limit, no offset). obj_flags: the flags (+2) of what a shell struck (ids from the world's hit()) */
    void *pool_ctx;
    int   (*pool_cap)(void *ctx, int kind);
    float (*pool_radius)(void *ctx, int kind);
    unsigned (*obj_flags)(void *ctx, int id);
} combat_shots;

/* the geometry the shots fly through: returns the vehicle hit by the segment (and its zone and the hit's fraction
 * along it), or -1 for none; ground_y gives the ground's height */
typedef struct {
    void *ctx;
    int   (*hit)(void *ctx, int owner, const float p0[3], const float p1[3], float *t, int *zone);
    float (*ground_y)(void *ctx, float x, float z);
    combatant *(*victim)(void *ctx, int id);
    /* a hit on something that isn't a vehicle (victim() gave NULL): structures; may be NULL. owner: the shooter */
    void (*hit_other)(void *ctx, int id, int owner, int power);
    /* a vehicle was hit (for the objectives' "hit by" checks); may be NULL */
    void (*hit_vehicle)(void *ctx, int id, int owner);
    /* a guided shot's target's position: 0 when it is gone (destroyed); may be NULL (no guidance) */
    int  (*target_pos)(void *ctx, int id, float out[3]);
    /* a beam's ray now: the firing vehicle's mount (from) and aim (dir, any length; zero keeps the aim as fired); 0
     * if the vehicle is gone, which ends the beam. May be NULL: the ray stays as fired */
    int  (*beam_ray)(void *ctx, int owner, int type, float from[3], float dir[3]);
} combat_world;

/* gdf e.g. "gdhuntea"; internal_scale multiplies the internal points (the player's 6 x) */
int  combat_init(combatant *c, hg_archive *a, const char *gdf, float internal_scale);

/* one tick of a vehicle's weapons. trigger: the fire control held. Shots leave from `from` toward `to` (the caller
 * picks the mount and the aim point per weapon through mount(), or NULL for `from` as given). Returns shots fired. */
typedef void (*combat_mount_fn)(void *ctx, int kind, float from[3]);
int  combat_weapons_tick(combatant *c, int owner, int trigger, const float from[3], const float to[3],
                         combat_mount_fn mount, void *mctx, combat_shots *s);

/* the crit slots and factors from c->gr (combat_init calls it) */
void combat_crits_init(combatant *c);

/* next weapon that is present */
void combat_cycle(combatant *c);

/* flies every shot one tick; returns how many hit something */
int  combat_shots_tick(combat_shots *s, const combat_world *w);
/* The beams' step (0x10018a9c, run by the frame's update over the beam pool): each beam's life drops by the frame's
 * ticks (it ends below 1); then its ray, from the vehicle's mount along its aim as they are now, is tested; a
 * vehicle struck within the weapon's reach (+0x50) takes the beam's damage on the struck part's zone, every frame
 * the beam lasts; a structure takes its power. Lasers and particle accelerators (kinds 4 / 6) end after one test.
 * Only a struck vehicle shows the effect (0x1001b3a1). Returns the vehicles struck. */
int  combat_beams_frame(combat_shots *s, const combat_world *w, int ticks);

/* applies a hit by a weapon type (of the given power) on zone (1..17); returns 1 if this hit destroyed the vehicle */
int  combat_damage(combatant *c, int type, int power, int zone);
/* a zone (1..17) destroyed as its internal points run out (0x1005ab68): a remote vehicle's, told by its owner */
void combat_destroy_zone(combatant *c, int zone);
/* the same with the shot's values given (an explosion's: 0x1001c75c passes mult 0.1, chip 0, mode 1) */
int  combat_damage_raw(combatant *c, int power, float mult, int chip, int mode, int zone);

/* the effect (gear_effect[], its sound) a shot of `kind` (the weapon's +0x10) makes where it strikes. flags, set as
 * the shot hits (0x100191d7): a vehicle 0x100, the player's vehicle 0x4000 (with its zones 1 / 2 also 0x2000), an
 * object flagged 0x800 0x400 (its OBJ's +0x34: every ground tile, some rocks; it also throws a spray, 0x1008514d,
 * and turns rockets' hits into the ground explosion 0x2c); objects without 0x100 / 0x400 / 0x800 turn the shot back. cockpit: the cockpit view. -1: none. A vehicle hit's effect is placed 300 lower
 * (combat_effect_drop). */
enum { COMBAT_HIT_VEHICLE = 0x100, COMBAT_HIT_GROUND = 0x400, COMBAT_HIT_CORE = 0x2000, COMBAT_HIT_PLAYER = 0x4000 };
int  combat_effect(int kind, unsigned flags, int cockpit);
float combat_effect_drop(int kind, unsigned flags, int cockpit);
/* whether the effect also takes a point light (0x1001b3a1's local_68) */
int  combat_effect_light(int kind, unsigned flags, int cockpit);

/* A guided weapon's seeker (0x100bc861, the gear block's +0x560 bits; +0x4d8 its timer). The selected slot's weapon
 * guided (+0x86 bit 0) and a target: within the weapon's band (+0x4c < distance <= +0x50) and with the aim within 16
 * degrees of it both ways (weapons with +0x86 bits 0x21 need only the band) it is in the cone (0x8000); the first
 * tick in the cone starts acquiring (0x40) with 0x16a ticks to go; at 0 (or with the vehicle record's +0xc0 bit 0x20)
 * it is locked (0x80). Out of the cone the lock drops at once, the acquiring after the timer climbs past 0x169. A
 * guided shot fired while locked takes the target (the slot's +0x18), else it flies unguided (0x100ba550). */
typedef struct { int acquiring, in_cone, locked, timer; } combat_seeker;
void combat_seeker_tick(combat_seeker *s, int type, int has_target, float dist, float yaw_err, float pitch_err,
                        int fast, int dt);

#endif
