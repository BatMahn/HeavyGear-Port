/*
 * gearmodel.h - a vehicle's model from its GDF: the parts' WTB models placed by the skeleton at rest.
 *
 * GDF parts (0x438 bytes each from 0x88, count at +0x84): +0x14 the segment name, +0x7c the models (6 detail slots of
 * 0x20; a name starting 'g' means the next slot's is the one drawn, 0x1004dc8f), +0x15c the part's pivot,
 * +0x54 the next sibling and +0x78 the first child (part indices, 0 = none; part 0 is the root, the hip), and from
 * +0x168 up to 9 links of 0x48 bytes (+0 1 when used, +0x24 the x, y, z offset of the k-th child from this part).
 * +0x10 is the part's damage zone. Names starting SOL_ (the soles), EYE_ (the eye) and POF_ (weapon mounts) are points, not models. The sole's height
 * below the root is the gear's standing height (the Hunter's 245.3, as measured in the running original), and the
 * eye's position is the cockpit eye.
 *
 * After the parts (0x1004c8c4): a 0x3c-byte block whose first word is 1 or 2, then more. Its +0x28 (the template's
 * +0xb4) is the vehicle class SimMain switches on (0x100c40a8 on): 1 gears, 2 / 4 ground vehicles (the Antelope),
 * 5 and 8 aircraft (the Orca, the Redjacket), 9 infantry.
 */
#ifndef HG_GEARMODEL_H
#define HG_GEARMODEL_H

#include "hgprj.h"
#include "anim.h"

#define GEARMODEL_MAX 64

typedef struct {
    char  model[9];         /* the model the sim draws */
    char  model0[9];        /* detail slot 0's: the one the shell's views draw (0x1011d8bd loads the ODF's first model) */
    float x, y, z;          /* from the root, at rest */
    int   zone;             /* its damage zone, 0x1003cd60 of its segment type (1..16; 0: none, a hit does no harm) */
    int   rec;              /* its record index */
    float m[9];             /* its world rotation from the root (row-major, v' = M v): the identity at rest */
    int   parent;           /* the nearest drawn ancestor part (index into part[]), -1 none: a zone destroyed takes
                             * the part with that zone and everything under it (0x100964e4 / 0x10096474) */
} gearmodel_part;

#define GEARMODEL_MAX_POF 16
typedef struct {
    char  name[9];          /* e.g. pof_hwrh, lower case */
    float x, y, z;
    int   rec;
} gearmodel_point;

typedef struct {
    int            nparts;
    gearmodel_part part[GEARMODEL_MAX];
    float          stand;               /* the root's height above the soles */
    float          eye[3];              /* EYE_ point; found_eye 0 if none */
    int            found_eye;
    int            npof;
    gearmodel_point pof[GEARMODEL_MAX_POF];   /* weapon mounts */
    int            vclass;              /* the vehicle class (0x1010b5a8 types; 2 is read as 4), 0 if absent */
    int            faction;             /* the block's +0x2c (template +0xb8 -> gear +0x613, 0x1004e6a0 / 0x1004f86d): 0 North, 1 South */
    int            vclass_raw;          /* the class as the GDF gives it (the record's +0) */
    int            infantry;            /* "infa" in the block's name (+4; 0x1004f86d's test) */
    int            nrec;                /* every part record's place (its origin, from the root), by record index */
    float          rec[96][3];
    /* the skeleton as the sim's objects have it (0x100c058b -> one object per record): each record's parent record
     * (-1 the root), its offset from it (the parent's link less its own pivot), its name's characters 4..7 (what an
     * animation's tracks bind to), its world rotation; the eye's and the soles' records */
    int            rec_parent[96];
    float          rec_off[96][3];
    char           rec_tag[96][5];
    float          rec_m[96][9];
    int            eye_rec, nsol, sol_rec[4];
    float          eye_local[3];        /* the eye's offset from its parent (the camera lifts it by y, 0x100c4c26) */
    char           sol_side[4];         /* 'l' / 'r': SOL_L.. / SOL_R.. (the gear block's +0x4c0 / +0x4c4) */
    float          rec_q[96][4];        /* each object's rotation (+0x3c) as the animations leave it (x, y, z, w) */
    int            anim_set;            /* +0x5ff: 0 / 0x100 / 0x200 by the weapons (gearmodel_stand) */
    float          root_dy;             /* the root lowered so the lower sole stands on the ground (0x1001386b) */
} gearmodel;

/* 0 on success */
int gearmodel_load(hg_archive *a, const char *gdf, gearmodel *g);
/* the same from a GDF in memory */
int gearmodel_parse(const uint8_t *d, size_t len, gearmodel *g);

/* where a weapon of equipment kind 8..16 fires from, for a vehicle at x, y, z (its root) facing heading: the POF point
 * the kind names (0x1004e6a0: "POF_AWLA" + n * 9; vehicles' kinds 0x21.. take their first POF point), turned with
 * the vehicle, from the skeleton as posed (gearmodel_stand). out is left alone if there is none. */
void gearmodel_mount(const gearmodel *g, float x, float y, float z, float heading, int kind, float out[3]);

/* poses the skeleton by an animation's frame 0 (0x1001386b with no aim, twist or head turn: every bound track's
 * rotation, the identity elsewhere is left as the rest's) and stands it on its lower sole: the parts', points' and
 * eye's places and rotations and the standing height follow. NULL restores the rest. */
void gearmodel_pose(gearmodel *g, const anim3di *an);
/* the standing pose of a gear or infantry (classes 1 / 9; the set by its weapons, anim_weapon_set; *STAND): loads the
 * 3DI and poses; 0 done, -1 (left at rest) for the other classes or without the animation */
int  gearmodel_stand(gearmodel *g, hg_archive *a, const int *types, const int *kinds, int nweapons, int version);
/* 0x1001386b on a step: each track the animation sets this step (anim_track_q) turns its object (HDHD only with
 * head: not for a wreck or a stunned vehicle, 0x10013d53's 0x10108378), then the skeleton is posed and stood on its
 * lower sole (a sole whose leg is gone, flags 0x2000 / 0x4000, left out) - the standing height (stand) is kept, the
 * parts lowered by root_dy */
void gearmodel_animate(gearmodel *g, const anim_player *p, unsigned flags, int head);

/* part k is gone: its zone or an ancestor part's is destroyed (zone_gone[17], combatant.zone_gone); 0x100964e4 takes
 * the part whose object's zone it is with everything under it */
int  gearmodel_part_gone(const gearmodel *g, int k, const int *zone_gone);

#endif
