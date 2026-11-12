/*
 * randmsn.h - the random mission generator of the Tour of Duty (HGSHELL 1.2, the class 0x100642c9 .. 0x1007199f;
 * driven by 0x1006f69d). It picks the pieces of the M51 .. M83 kit by its tables (src/rmg_tables.h) and the C
 * library's rand(), copies them under fixed names into the game folder, as the original does (RAN1..4SCN1.BWD,
 * RANDMESH / RANDART1 / RANDLOC1 / RANDWAY1 / RANDROCK.BWD, OBJECT01..03.BWD, COMPUT01..02.BWD, RANDOBJ1..3.BRF,
 * GEAROBJ1.BRF), and fills the briefing from the template's BRF with its /xx tags replaced. RE_NOTES "The random
 * mission generator".
 */
#ifndef HG_RANDMSN_H
#define HG_RANDMSN_H

#include "brf.h"
#include "hgprj.h"

typedef struct {
    /* in (0x1006504e's arguments) */
    int      mode;          /* +4: 0 tour ('S' templates, the history files), 1 'C', 2 'M' */
    int      type;          /* +0x18: the mission type (0 .. 9 the list's, 0xd free for all) */
    int      side;          /* +0x1c: the template table's side (0 / 1) */
    int      p5;            /* +0x6c: SCN1 / SCN2 of the template; +0x70 = (p5 == 0), the enemy's side letter */
    int      level, supply, reinforce;   /* +0xa8 / +0xa0 / +0xa4: the threat = level + their steps, 0 .. 0x1a */
    float    front[19];     /* +0xd0: the game's front line (its record, FUN_10112881(0x22)) */
    unsigned seed;          /* srand's seed (FUN_10065fd8 -> 0x10064211) */
    /* out */
    int      tmpl;          /* +0x20: the template, 51 .. 83, 99 for M99 */
    int      geoset, mesh;  /* +0x24 / +0x28 */
    int      tod, artset, art;   /* +0x2c the time of day (RAN<tod+1>SCN1), +0x30, +0x34 */
    int      rock;          /* +0x38 */
    int      nobj;          /* +0x3c: the objective count (-1 / -3: 1 / 3 objectives without a location) */
    int      obj[3];        /* +0x40: the objectives' geo ids */
    int      threat;        /* +0xac */
    int      nenemy;        /* +0xb0: 2 */
    int      enemy[2];      /* +0xb4: the enemy kinds */
    int      region;        /* +0x11c: 0 .. 16 */
    char     scenario[16];  /* RAN<n>SCN1 */
    char     tag[10][64];   /* +0x620 ..: /ea /o1 /o2 /o3 /mn /tm /u1 /u2 /tr /ct */
    hg_brf   brf;           /* the briefing (the template's BRF, tags replaced) */
    int      movie;         /* 0x10065af2: the template's type name index (rmg_movie_name), -1 none */
} rmg;

/* Generate into the game folder of `a`. Returns 1 on success, 0 on a failure (as 0x1006f69d). */
int  rmg_generate(rmg *r, hg_archive *a);
/* the mission type's name (0x10254d80 table) */
const char *rmg_type_name(int t);

/* the C library's rand() as the original links it (MSVCRT: x = x * 214013 + 2531011, (x >> 16) & 0x7fff) */
void rmg_srand(unsigned seed);
int  rmg_rand(void);

#endif
