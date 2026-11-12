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

#define GEARMODEL_MAX 64

typedef struct {
    char  model[9];         /* the model the sim draws */
    char  model0[9];        /* detail slot 0's: the one the shell's views draw (0x1011d8bd loads the ODF's first model) */
    float x, y, z;          /* from the root, at rest */
    int   zone;             /* the part's +0x10: its damage zone (1..17; 1 head, 2 torso, 3 / 4 arms, 5 / 6 legs, ...) */
    int   rec;              /* its record index */
} gearmodel_part;

#define GEARMODEL_MAX_POF 16
typedef struct {
    char  name[9];          /* e.g. pof_hwrh, lower case */
    float x, y, z;
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
    int            nrec;                /* every part record's place (its origin, from the root), by record index */
    float          rec[96][3];
} gearmodel;

/* 0 on success */
int gearmodel_load(hg_archive *a, const char *gdf, gearmodel *g);
/* the same from a GDF in memory */
int gearmodel_parse(const uint8_t *d, size_t len, gearmodel *g);

/* where a weapon of equipment kind 8..16 fires from, for a vehicle at x, y, z (its root) facing heading: the POF point
 * the kind names (0x1004e6a0: "POF_AWLA" + n * 9; vehicles' kinds 0x21.. take their first POF point), turned with
 * the vehicle. out is left alone if there is none. */
void gearmodel_mount(const gearmodel *g, float x, float y, float z, float heading, int kind, float out[3]);

#endif
