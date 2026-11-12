/*
 * debrief.h - the debriefing's score (traced: HGSIM 0x1006977c writes the objectives to hgmsn.cfg, HGSHELL
 * 0x100df8e4 scores the mission into the game record, 0x100e35cc fills the score list, 0x100b9fad draws it).
 *
 * The sim's objective records (hgmsn.cfg, 0x1006977c): every objective of the player's group whose runtime display
 * flag (+0xae, the MTBL's 'V', toggled by 0x40000 actions) is set: +0 done (state 5), +4 its order byte (MTBL +0x2e),
 * +0x14 its text. The list (0x100e35cc) takes those with text and order 1 or 2: "Complete" when done, else
 * "Incomplete" ("In Progress", state 0, is never made). Kills: HGCAR +0x48 +0x4a +0x4c +0x4e +0x50 +0x56 +0x5a (the
 * Vehicle Kills); deaths: HGCAR +0x1e (the player's vehicle destroyed, 0x10059e9d).
 *
 * The scoring (0x100df8e4, when the debriefing opens, before the list's 0x100e35cc):
 * - complete_pays ([0x35], 0x100bae59): on the story's success, whether the mission played was the last reached;
 *   otherwise 1.
 * - kills_before ([0x36], 0x100bae72): 0; on the story's success the mission's best kills (the record's +0x138 +
 *   mission x 4) when this run beat it, else this run's kills.
 * - Story success, first time (the mission played was the last reached): stats 4, 5, 0x10, 0x11 and 3 + 1, stat 0x19
 *   = stat 0x19 << 6 | kills & 0x3f, the mission bonus 100; and the done objectives of order 1 / 2 100 each.
 * - Story success: kills above the mission's best pay 50 each, stat 6 + those, the best becomes this run's.
 * - Tour of duty (stat 11 the mission type): on success the type's bonus (200 / 50 / 150 for 6 / 7 / 8) and 100, stats
 *   4, 5, 0x10, 0x11, 3 + 1, stat 0x19 as the story's, the campaign stats by type (0: 0x15 + 1, 0x16 + kills; 5:
 *   0x18 + 1; 7: 0x14 + 1; 0xc: 0x17 = kills); on failure stats 0x11 / 5 / 0x12 / 0x13 / 0x17 = 0, 3 + 1, 0x19 << 6
 *   and the type's campaign stats 0; always kills x 50, deaths x -50 and the done objectives' 100s.
 * - A counted success (the tour's, the story's first) counts the medal counters (0x1011275d) by the kills.
 * - Experience (stat 0) + mission bonus + kill bonus + death penalty + the objectives' 100s; var 0x1d = the bonus.
 * The list (0x100b9fad) shows Complete items 100 when complete_pays, Mission Bonus var 0x1d, Kill Bonus (kills -
 * kills_before) x 50, Death Penalty deaths x -50, Mission Total their sum, Career Total stat 0.
 */
#ifndef HG_DEBRIEF_H
#define HG_DEBRIEF_H

#include "career.h"

#define DEBRIEF_MAX 64

typedef struct {
    char text[64];
    int  done;    /* state 5 */
    int  order;   /* the MTBL's order byte: 1 primary, 2 secondary ... */
} debrief_obj;

/* what the sim hands the shell */
typedef struct {
    int         n;
    debrief_obj o[DEBRIEF_MAX];
    int         kills, deaths;
    int         valid;
    int         crit_zones;   /* HGCAR +0x138: the player's zones destroyed by critical hits (0x1005b5c6) */
} debrief_in;

/* the list as 0x100b9fad draws it */
typedef struct {
    int  n;
    struct { char text[64]; int cat; int value; } item[DEBRIEF_MAX];   /* cat: 2 Complete, 1 Incomplete */
    int  bonus, kill_bonus, death_penalty, total, career;
    int  valid;
} debrief_list;

/* 0x100df8e4's scoring of the game record c (class 0 story, 1 tour; outcome 2 success) and 0x100e35cc's list.
 * stat 11 must already hold the mission played. Returns the experience gained. */
int debrief_score(career_char *c, int cls, int outcome, const debrief_in *in, debrief_list *out);
/* 0x1011275d: the record's medal counter i + 1, with its carries */
void debrief_medal_count(career_char *c, int i);

#endif
