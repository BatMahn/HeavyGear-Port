/*
 * squad.h - the player's wingmen (HGSHELL's briefing squad panel and the launch's USERSTAR.BWD).
 *
 * Traced:
 *   - the mission's BRF says who may fly (brf.squad, the record's +0x2316): bit 0 the player may go alone, bits 1..10
 *     pilots 0..9;
 *   - the briefing's two pilot lists (0x100ef574): with bit 0, "No squadmate" heads both lists and is chosen; with one
 *     pilot bit alone, that pilot fills list 1 and list 2 has "No squadmate"; else every pilot of the mask still
 *     alive (the game's pilot record +0x25c + k x 0x14, first i32 != 0) fills both, each list taking the first
 *     pilot the other hasn't (0x10076f1a); an empty list gets "No squadmate";
 *   - the pilots' names (0x100e7ebe North, 0x100e8038 South) and portraits (B05 pictures 0x11d + k / 0x128 + k
 *     North, 0x133 + k / 0x13e + k South, for lists 1 / 2);
 *   - the launch (0x100ff1e6): the player's record (group 0, leader, role 0, USERGEAR), then wingman 1 (leader 0,
 *     role 2, voice k - 1 for the North, k + 9 for the South) and wingman 2 (voice k), each in the gear its list
 *     shows (the pilot's gear, the game record's string k; none yet: the list's current gear, then kept for him);
 *   - in the sim, a vehicle of the player's group sets its slot's reply voice set from the record's voice
 *     (0x10017766: 0xffff 0, below 9 + 1, else - 1).
 */
#ifndef HG_SQUAD_H
#define HG_SQUAD_H

#include <stddef.h>

#define SQUAD_NONE1 (-2)   /* list 1's "No squadmate" */
#define SQUAD_NONE2 (-1)   /* list 2's */

typedef struct {
    int  pilot[2];         /* 0..9, or SQUAD_NONE1 / SQUAD_NONE2 */
    int  voice[2];         /* the record's +0x16 */
    char gear[2][32];      /* GDF names (without the extension) */
} squad;

/* the pilot's name ("Janus") for the side (1 North, else South) */
const char *squad_pilot_name(int north, int k);
/* the B05 portrait picture of pilot k in list n (0 / 1) */
int  squad_portrait(int north, int list, int k);
/* the two lists for a BRF squad mask (pilot numbers or SQUAD_NONE1 / 2); 1 if "No squadmate" is the choice */
int  squad_lists(int mask, const int alive[10], int l1[11], int *n1, int l2[11], int *n2);
/* the next (dir 1) / previous (-1) item of a list after cur that isn't `other` (the linked list's choice) */
int  squad_step(const int *l, int n, int cur, int other, int dir);
/* the records' voices for the chosen pilots */
void squad_voices(squad *s, int north);
/* the default choice for a BRF squad mask; alive[k] != 0 for the living pilots */
void squad_default(squad *s, int mask, const int alive[10], int north);
/* the game's tech level for the gear lists (0x10112146 / 0x1011238e(0)): its type (2 the story), experience (stat 0),
 * the side's prestige (the record's +0x8c [2] North, [6] South) and chapter (stat 7) */
int  squad_tech_level(int type, int stat0, int prestige, int chapter);
/* a GDF's tech (0x1005bb6d), its side (+0xb8: 0 North, 1 South) and display name; -1 if it isn't a gear the list
 * takes (no extra data, +0xd0) */
int  squad_gear_info(const unsigned char *gdf, size_t len, int *side, char *name, size_t nlen);
/* the stock gears, in the list's order (0x101c56f0): 25 names ("GACHEETA"), NULL past them */
const char *squad_stock_gear(int i);
/* the voice records' set index (0x10017766) for a record voice (as the u16 the sim reads) */
int  squad_voice_set(int voice);

#endif
