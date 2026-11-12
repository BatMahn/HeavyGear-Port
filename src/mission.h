/*
 * mission.h - load a mission's world: the xxx_SCN1 record and everything it INCLudes.
 *
 * Objects (INFERRED from the data, to be traced in HGSIM 0x100c1152): an OBJ chunk (60 bytes) is
 *   i16 index, i16 parent (-2 at the top level), i16 ?, i32 scale[3] (1 = unit), i32 rot[3] (16.16 fixed degrees),
 *   i32 offset[3], 8 bytes ?, char model[10] at +0x32.
 * Inside a BLKX ... ENDB block the offset is relative to the BLKX position (i32 x, y, z at +0x18 of its 36 bytes).
 * The ground tiles (xxx_GRND) sit in a plain BLK block with their coordinates in the model itself.
 */
#ifndef HG_MISSION_H
#define HG_MISSION_H

#include "hgprj.h"
#include "material.h"

typedef struct {
    char  model[11];
    float x, y, z;       /* world position */
    float oy;            /* the OBJ's own y offset (block objects) */
    float rot[3];        /* degrees */
    int   ground;        /* 1: a ground tile */
    int   in_block;      /* 1: placed relative to a BLKX block */
    int   kind;          /* OBJ +4 (7 on most placed objects, 0 on some variants, -1 on children; meaning open) */
    int   parent;        /* obj slot of the parent node, or -1 */
    int   child;         /* 1: positioned from its parent */
} mission_obj;

/* NAVP (HGSIM 0x100c3718, nav table 0x107bc3a0, 0x54 bytes per nav): a navpoint inside a BTAG ... ETAG block. */
typedef struct {
    char  tag[17];       /* the enclosing BTAG's name, e.g. WPPL00 */
    float x, y, z;       /* +0x00 i32 x, y, z */
    float heading;       /* +0x0c i32, 16.16 fixed degrees (* 1/65536, 0x10103880) */
    int   group;         /* +0x16 i16 */
    unsigned mask;       /* +0x1a u16, widened as 0x100c1120 does (0x730 / 3 families) */
    float radius;        /* +0x18 u16 x 100 (0x100c3718); 0: the objectives' 20000 */
    int   visit;         /* +0x10 i16: nonzero, shown (radar, the nav list) and markable as visited (nav +0x00) */
    char  name[22];      /* +0x1c: the label the HUD shows ("Echo") */
} mission_nav;

/* GPS: a gear placement (92 bytes; the shell's USERSTAR records are the same layout, HGSHELL 0x100ff1e6). */
typedef struct {
    char tag[17];        /* the enclosing BTAG */
    int  group;          /* +4 */
    int  leader;         /* +5: 1 on the first record of a group */
    int  role;           /* +6: 0 = the player (the GPS branch of 0x100c1152, 0x100c3f1f), 2 = AI */
    char gear[9];        /* +0x1c */
    unsigned mask;       /* +0x18 widened (0x100c1120): what objectives it can be a target of */
} mission_gps;

/* GT: a destructible structure (the GT branch of 0x100c1152, the object table 0x107f2540, 0x50 bytes each). */
typedef struct {
    char     tag[17];    /* the enclosing BTAG */
    int      obj;        /* +0: the OBJ it is (obj slot, -1 if not found) */
    int      alt;        /* +2: the OBJ shown once destroyed (obj slot or -1) */
    int      hp;         /* +0x10: hit points (32000: can't be destroyed; negative: |hp| and a flag) */
    unsigned mask;       /* +0x12 widened */
    char     name[23];   /* +0x18 */
} mission_target;

/* MTBL (0x10066090): a group's objectives, 0x97 bytes each from +0x1e. */
#define MISSION_MAX_COND 8
typedef struct {
    unsigned type;       /* +0 the mask: 0x10 start, 1 / 2 destroy, 4 defend, 8 / 0x20 reach, 0x100 go to, 0x200 /
                          * 0x400 / 0x800 / 0 wait, 0x2000 leave, 0x4000 hit, 0x8000.. radio, 0x10000 win, 0x20000
                          * lose, 0x40000 show, 0x80000 fail, 0x100000 complete, 0x200000 reset all, 0x400000 reset */
    int  visible;        /* +4 'V' */
    int  all;            /* +6: every condition must hold (else any) */
    int  ncond;
    struct { int kind, obj, group; } cond[MISSION_MAX_COND];   /* +0xa + k * 4: 'C' 1 done, 'S' 2 succeeded,
                                                                 * 'F' 3 failed; the objective, the group */
    int  limit;          /* +0x2a: time limit (sim seconds, 181 ticks), -1 none */
    int  order;          /* +0x2e */
    int  must;           /* +0x2f 'M': the group's mission needs it */
    char ok_voice[12];   /* +0x34 */
    char fail_voice[12]; /* +0x3f */
    char tag[12];        /* +0x4a: the BTAG of its targets */
    int  arg_group, arg_obj;   /* +0x53, +0x55 */
    char text[65];       /* +0x57 */
} mission_objective;

typedef struct {
    int  present;
    int  limit;          /* +4 (chunk +0xc): the group's time limit, -1 none */
    char ok_voice[12], fail_voice[12];   /* +0xa, +0x15 */
    int  nobj;
    mission_objective *obj;
} mission_mtbl;

/* Where a group starts (DoFirstObjtv, SimMain -> 0x10066a68 -> 0x1004271a): the first navpoint registered on the
 * group's objective 0. Objective 0 names a BTAG; NAVP chunks inside that BTAG whose type mask meets the objective's
 * mask are registered on it as they load (0x10066794). */
typedef struct {
    int   found;
    int   nav;           /* index into mission.nav */
    float x, y, z, heading;
} mission_start;

/* FTBL: a formation (0x100bfca4; up to 32, 8 slots). Slot offsets are in the leader's frame (0x100428c2 moves them
 * by the leader's matrix, 0x10051940); 0x10042603 makes them relative to the leader's own slot. */
#define MISSION_MAX_FORM 32
typedef struct {
    char  name[17];
    int   nslots;
    float x[8], z[8], heading[8];
} mission_form;

typedef struct {
    char         name[16];
    char         scn[24];          /* the scene record, e.g. m01_scn1 (SKYGND.PAR keys on its first 4 letters) */
    mission_obj *obj;
    int          nobj;
    material_set mat;
    char         music[16];
    char         palette[17];      /* PALG's first name */
    char         luma[17];         /* LTBL: the luma table (TBL) */
    int          view_distance;   /* VIEW chunk (+4), e.g. 50000 */
    /* LITE (the ART record; HGSIM 0x100c1152, into the eyepoint at 0x1011f908 + 0x1c..0x2a, and 0x1011fa34) */
    int          has_lite;
    int          lite_dir[3];      /* +0x08 i32 x, y, z: the software light's direction (999999 x3: no light) */
    int          lite_ambient;     /* +0x14 i16: software ambient luma (the eyepoint's +0x2a) */
    int          lite_mode;        /* +0x16 i16: 1 directional */
    int          lite_shadedist;   /* +0x18 i32: software "shadedist" (> 0 darkens beyond it) */
    mission_nav *nav;   int nnav;
    mission_gps *gps;   int ngps;
    mission_target *tgt; int ntgt;
    mission_mtbl  mtbl[16];
    mission_form form[MISSION_MAX_FORM]; int nform;
    char         group_form[16][17];   /* each group's formation (GRP / STAR chunks); "" = formation 0 */
    int          group_side[16];       /* STAR +4: each group's side (mission 1: 0 the player's, 1 the enemy) */
    /* per group (MTBL +0, up to 16): objective 0's tag (MTBL +0x70) and mask (MTBL +0x26) */
    char         obj0_tag[16][17];
    unsigned     obj0_mask[16];
    int          ngroups;
    /* HGXP (gearsht1): the effect pools (HGSIM 0x100c1152, the record loader; the effect step 0x1001b3a1 takes a free one of the
     * effect's type). Per effect type (data +4, i16, 0..0x4c): the model (23 bytes from the end) and how many
     * of it (the i16 before the name) */
    char         fx_model[0x4d][17];
    int          fx_count[0x4d];
} mission;

int  mission_load(mission *m, hg_archive *a, const char *name, char *err, size_t errlen);
void mission_free(mission *m);
/* the start of a group; the player's squad is group 0 (the shell writes the player's GPS with group 0) */
mission_start mission_group_start(const mission *m, int group);

#endif
