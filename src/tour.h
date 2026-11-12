/*
 * tour.h - the Tour of Duty's game logic for the front end (HGSHELL): its new game (B08's accept, FUN_100ea4fc ->
 * 0x1013702e -> FUN_10110eaa), the faction routing (FUN_100e8f73 -> FUN_100e9072), B04's mission type list
 * (FUN_100fcf6c -> 0x100fcfcf) and the generator's run (FUN_100fcbc3 -> 0x100fcc3f -> src/randmsn.h).
 */
#ifndef HG_TOUR_H
#define HG_TOUR_H

#include "career.h"
#include "randmsn.h"

/* FUN_10111766: 0 for types 1 / 4, else 1 (the faction the hubs, B02 / B09, and the generator go by) */
int tour_faction(int type);

/* 0x1013702e / FUN_10110eaa for a tour game: type = faction != 1 (0 or 1); stat 7 = the unit, stat 8 = 1 << it,
 * stat 11 = -1; the front line 19 x (rand() % 1000 / 50000 + 0.5) (FUN_1011109f); returns its index or -1 */
int tour_new_game(career *k, const char *name, int faction, int unit);

/* 0x100fcfcf: the mission types offered. Its first branch (a game with no missions yet or with stat 5 > 0): by stat
 * 11, the level (0x1a adds the duel) and the unit (stat 7); the second (missions played, stat 5 < 1): one type drawn
 * by rand() (FUN_10034fab) from stat 11's pair. n returned, at most 16 */
int tour_mission_types(const career_char *c, int level, int *types);

/* 0x100fcc3f: the generator for mission type `type` with seed var 0x15 and mode var 0x16; fills r and returns 1 on
 * success. *region gets the region (stat 14) and *alone (stat 15) as the original stores them */
int tour_generate(hg_archive *a, const career_char *c, int level, int seed, int mode, int type, rmg *r, int *alone);

/* B08's unit spinner (FUN_100edde3, a tour game): North's units 0..3 (pictures 0x5e..0x61), South's 10..12 (0x68..0x6a);
 * units (if given) gets them, -1 ending; returns the one it starts on (1 North, 12 South) */
int tour_units(int faction, int *units);


#endif
