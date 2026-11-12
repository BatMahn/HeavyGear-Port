/*
 * barracks.h - the barracks screen's contents (HGSHELL FUN_1013ae22, B03), from the current game's record.
 *
 * Traced (the builder's controls, run on entering the screen):
 * - the rank (FUN_100eb6d3 -> 0x100eb738): stat 2's picture at B03_RANK (42, 141): ranks 0..10 pictures 0x7b..0x71
 *   (B03NB_CP .. B03N1COL), 11..22 pictures 0x87..0x7c (B03SCSCP .. B03S1_CM);
 * - the medals (FUN_100eadd4 -> 0x100eaebd): the panel B03MEDAL (picture 0x4b) at (42, 239), then each medal the
 *   record has (its +0xac [k] > 0) as its picture at the next record's rectangle and the count (itoa, font 9, colour
 *   0xfe) in the one after: a South game (FUN_10111766: types 1 / 4) medals 6..13, a North one 0..5 and 13;
 * - the abilities (FUN_100ecc65 -> 0x100ecd04): the record's +0xe4 flags as lines (the best of 0..2 speed, 3..9,
 *   the best of 10..12 recoil, 13..16, the better of 17 / 18 armour, 19..21; "<NONE>" for none), each ended by the
 *   separator 0x10254f90 ("\n"), under the panel B03AIINF (picture 0x4d) at (322, 46)-(617, 194), font 9, 0xfe;
 * - the statistics (FUN_100efd6d -> 0x100efde4) in B03STATS (26, 54)-(286, 219), font 9, 0xfe;
 * - the unit list (FUN_1000ec4f at B03ULIST (328, 230)-(560, 412), filled by FUN_100edfa4 -> 0x100edff3): a North
 *   story game unit 1 only; another North game the units 0..9 of stat 8's bits, a South game 10..18 of bits 10..18;
 *   each item its insignia (pictures 0x5e + unit) at (7, 0) and its text (0x100ee8e2: the unit's name, two
 *   separators, "Perks" ":  " and the perk) in (7, 0)-(227, 140), font 7, colour 0xfe; stat 7's unit selected.
 *   Prev / next step through it; exit (FUN_100efccb -> 0x100efd1a) makes the selected unit stat 7.
 */
#ifndef HG_BARRACKS_H
#define HG_BARRACKS_H

#include "career.h"

#define BARRACKS_UNITS 19

typedef struct shell_barracks_s {
    int  rank_pic;                       /* picture record, -1 none */
    int  nmedal;
    int  medal_pic[14], medal_n[14];     /* picture records (their rectangles follow them in the table) */
    char ai[640];
    char stats[512];
    int  nunit;
    int  unit[BARRACKS_UNITS];           /* unit numbers 0..18 */
    int  unit_pic[BARRACKS_UNITS];
    char unit_text[BARRACKS_UNITS][400];
    int  unit_sel;
} shell_barracks;

/* the screen's contents for game record c (story games: the player is Edward Scott) */
void barracks_build(const career_char *c, shell_barracks *b);
/* the side of a game type (FUN_10111766): 0 for types 1 and 4, else 1 */
int  barracks_side(int type);
/* the unit names (0x102569f8) and perks (0x101ab9a0), units 0..18 */
const char *barracks_unit_name(int unit);
const char *barracks_unit_perk(int unit);
/* the rank names (0x10254808, 0..22) and the combat priority names (0x101acda0, levels 0..26: "C9" .. "A1") */
const char *barracks_rank_name(int rank);
const char *barracks_priority_name(int level);

#endif
