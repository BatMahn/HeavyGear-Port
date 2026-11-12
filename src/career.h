/*
 * career.h - the player's games (HGSHELL's "characters"), saved in CHARINFO.HG1 in the game folder.
 *
 * File (FUN_101371a2 reads, FUN_101373bf writes): i32 count, i32 x3 (the shell's last selections), i32 the records'
 * size, then the records zlib-compressed. A record (FUN_10111ce0 / FUN_10111996):
 *   string name, string gear (the player's gear, a GDF name: the in-memory record's +0x14, set by the briefing's gear
 *   list, 0x100f08c0, and written to USERGEAR at the launch, 0x1012e7c2; the port's `desc`); i32 type; i32 stat[26]; i32 [8]; i32 [14]; i32 flag[22]; 10 x 20 bytes;
 *   f32 front[19] (the briefing map's front line, for the game types without the story's table); 128 bytes;
 *   10 strings.
 * A string: i32 length, i32 (the length again), then length + 1 UTF-16 units when length > 0.
 * type: 2 the story (FUN_1011178a class 0), 0 / 1 tour of duty (class 1), 3 / 4 multiplayer (class 2).
 * stat: [2] rank, [10] the last campaign mission reached, [12] the current one (the campaign table entry).
 */
#ifndef HG_CAREER_H
#define HG_CAREER_H

#include <stddef.h>
#include <stdint.h>

#define CAREER_NAME 64

typedef struct {
    char    name[CAREER_NAME], desc[CAREER_NAME];   /* Latin-1 */
    int     type;
    int32_t stat[26];
    int32_t a8c[8], aac[14], flag[22];
    uint8_t r25c[200];
    float   front[19];
    uint8_t b13c[128];
    char    str[10][CAREER_NAME];
} career_char;

typedef struct {
    int          count;
    int32_t      last[3];
    career_char *c;
} career;

enum { CAREER_RANK = 2, CAREER_REACHED = 10, CAREER_MISSION = 12 };

/* load path (a missing file is an empty career); 0 on success */
int  career_load(career *k, const char *path);
int  career_save(const career *k, const char *path);
void career_free(career *k);
/* a new story game (FUN_10110eaa with type 2): returns its index, or -1 */
int  career_new_story(career *k, const char *name);
void career_delete(career *k, int i);
/* the class of a type as FUN_1011178a: 0 story, 1 tour of duty, 2 multiplayer */
int  career_class(int type);

#endif
