/*
 * anim.h - the vehicles' skeletal animations (3DI records, HGSIM 0x10012dc5 / 0x1001386b / 0x1001325d).
 *
 * A 3DI: i32 version (1), i32 the length in frames (+4), i32, i32 the track count (+0xc); from +0x10 the tracks,
 * each: char name[0x20] (a skeleton part's, e.g. "GN11RASH": parts are matched by its characters 4..7 against the
 * vehicle's objects' names, 0x1009623a / 0x1009629f, depth first, self before children before siblings; names
 * shorter than 8 become "++++SKIP"), i32 position keys, 0x10 bytes each (i32 frame, x, y, z), i32 rotation keys, 0x14
 * bytes each (i32 frame, quaternion x, y, z, w); then the control track (i32 count, 0x39-byte events: sounds, the
 * stance changes; 0x10013735). Nothing in the sim reads the position keys when posing (0x1001386b).
 *
 * Posing (0x1001386b): each track with rotation keys sets its object's rotation (+0x3c) to the key at the frame
 * (0x1001325d: the key on the frame, else between the keys around it, 0x1005267f); TRHP's is multiplied after the
 * object's own (0x100956b7), TRSO's, HDHD's and RASH's before it (0x1009566e; the twist, the head's and the arm's aim
 * put there each tick), any other replaces it (0x10095620). Then gears and infantry are stood on their lower sole.
 *
 * The sets (0x100147b7, 0x10012fc2: id = set + state, 0x1000 more for infantry, 0x1003f7b0's class 9 branch): a
 * vehicle's set (+0x5ff) starts at 0 (GNW*, no hand weapon) and each weapon in slot order may change it (0x1004e3db
 * by the weapon type: 2..8, 0xd, 0xe, 0x21..0x25, 0x28 the rifles, 0x100, GRF*; 0x1d..0x20 the bazookas, 0x200, GBZ*;
 * infantry's: type 0 0x100, 0x29 0x200). State 0 is the stand (*STAND). Aircraft (class 5) take their own (V13 /
 * V63 SPINN) and the other vehicles none.
 */
#ifndef HG_ANIM_H
#define HG_ANIM_H

#include "hgprj.h"

#define ANIM_MAX_TRACKS 64

typedef struct { int frame; float q[4]; } anim_qkey;

typedef struct {
    char  tag[5];           /* the name's characters 4..7 */
    int   nrot;
    float q0[4];            /* the rotation key on frame 0 (x, y, z, w); the identity if none */
    int   has_q0;
    anim_qkey *key;         /* the rotation keys (nrot), by frame */
} anim_track;

/* a control-track event (0x39 bytes: +0 the frame, +4 its name, "FOOTSTEP" / "FALLDOWN", +0xd the foot (0 / 1, 2,
 * -1 none), +0x11 the stance (1: 0x562 | 0x1000)) */
typedef struct { int frame; char name[10]; int foot, stance; } anim_event;

typedef struct {
    int        frames;      /* +4 */
    int        fps;         /* +8: frames a sim second (30) */
    int        ntracks;
    anim_track track[ANIM_MAX_TRACKS];
    int        nev;
    anim_event *ev;
} anim3di;

/* 0 on success */
int anim_parse(const uint8_t *d, size_t len, anim3di *an);
int anim_load(hg_archive *a, const char *name, anim3di *an);

void anim_free(anim3di *an);

/* quaternions (x, y, z, w): 0x10052ebe's product (a then b, the order 0x100956b7 / 0x1009566e use), 0x1005267f's
 * spherical blend (t 0: a, 1: b; nearly opposite: a turned a quarter about its own axis, by sin(t x 90)) */
void anim_qmul(const float a[4], const float b[4], float out[4]);
void anim_slerp(const float a[4], const float b[4], float t, float out[4]);
/* 0x1001325d: a track's rotation at a frame (taken modulo len): the key on it, else blended between the keys around
 * it (wrapping), t = (frame - (before + 1)) / (after - (before + 1)); -1 with no keys */
int  anim_key(const anim_track *t, int frame, int len, float q[4]);
/* 0x10013735: the event on the frame (modulo len) or the last one before it (wrapping); NULL with none */
const anim_event *anim_event_at(const anim3di *an, int frame);

/* 0x100147b7's table (id = set + state, 0x1000 infantry, 0x2000 / 0x3000 the S.. / SA.. striders, 0x4000 / 0x5000
 * V63 / V13): its 3DI's name and whether it loops (0x100131df's third argument: then 0x1001459c makes HDHD's keys
 * the inverse of TRHP's times TRSO's, the head kept level); NULL if none */
const char *anim_name(int id, int *loop);
/* the shared table (0x10012fc2): the animation of an id, loaded on first use; NULL without one */
const anim3di *anim_get(hg_archive *a, int id);
void anim_cache_clear(void);

/* the gear block's +0x562 bits the animations read and set */
enum {
    ANIM_SKATE = 1, ANIM_FALLF = 4, ANIM_DOWN = 8, ANIM_CROUCH = 0x10, ANIM_RISE = 0x200, ANIM_FALLB = 0x400,
    ANIM_GRENADE = 0x800, ANIM_STANCE = 0x1000, ANIM_LSOLE = 0x2000, ANIM_RSOLE = 0x4000, ANIM_BUSY = 0x60e,
};

/* what 0x1003fbb7 reads of a vehicle */
typedef struct {
    int   cls;              /* the vehicle class (its record's +0) */
    int   set;              /* +0x5ff: 0, 0x100, 0x200 */
    int   prefix;           /* 0x1003f7b0: gears named S.. 0x2000 (SA.. 0x3000), aircraft ?13 0x5000 / ?63 0x4000 */
    float thr;              /* the gear block's +0x7c: the throttle's lag (1/64 .. 2/64) */
    int   straffe_l, straffe_r, reverse;   /* control +0x57 / +0x58 / +0x59 */
    int   stun;             /* +0xc3d */
    int   dying;            /* class 9: +0x560 & 0x400 (DEADS) */
} anim_in;

/* a vehicle's animation (its record's +0x88 / +0x8c / +0x90 and its 0x38-byte slot at 0x10819580) */
typedef struct {
    int cur, next, rate;    /* the playing id, the wanted one, the rate (1 x 0.75, 3 x 1.5, else x 1) */
    int frame;              /* +8 (while blending: the blend's count, 0 .. 9) */
    int old_frame;          /* +4: the frame the old animation was left on */
    int start;              /* +0xc: the frame the new one starts from after the blend */
    int blending;           /* +0x18 */
    int prev;               /* +0x1c: the id before (a grenade's throw goes back to it) */
    int foot;               /* +0x2c: the last footstep event's foot */
    int held;               /* +0x30 */
    const anim3di *an, *old;
    int footstep;           /* the vehicle's +0x84 bit 8: a foot came down (the caller clears it) */
    int dead_done;          /* class 9: +0x560 | 0x10 (its DEADS ran out) */
    int knock;              /* +0xc15 = 1 again (getting up with the legs gone) */
} anim_player;

void anim_player_init(anim_player *p);
/* 0x1003fbb7 then 0x1003f7b0: the wanted animation; flags is the gear block's +0x562 */
void anim_pick(anim_player *p, const anim_in *in, unsigned *flags);
/* 0x10013d53 for one vehicle: the wanted animation started (blended over 10 steps from the old), then the frame
 * stepped by ftol(fps x rate x ticks / 181 + 0.5), at least 1; legs_gone: +0x560 & 0x20 */
void anim_advance(anim_player *p, hg_archive *a, int ticks, unsigned *flags, int legs_gone);
/* the rotation 0x1001386b gives the current animation's track t on this step (blended while blending); -1: the track
 * isn't set this step (no keys past frame 0, or the old animation has no track t) */
int  anim_track_q(const anim_player *p, int t, float q[4]);

/* the vehicle's set (+0x5ff) from its weapons in slot order: 0, 0x100 or 0x200 (version: the GDF's; infantry: "infa"
 * in its block's name) */
int anim_weapon_set(const int *types, const int *kinds, int n, int version, int infantry);
/* the standing animation of an id (set, + 0x1000 for infantry; 0x100147b7): GNWSTAND, GRFSTAND, GBZSTAND, DNW.. */
const char *anim_stand_name(int id);

/* a quaternion (x, y, z, w) as a row-major matrix, v' = M v */
void anim_quat_matrix(const float q[4], float m[9]);

#endif
