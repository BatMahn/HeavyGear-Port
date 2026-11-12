/*
 * brf.h - a mission's briefing (BRF), as HGSHELL reads it (FUN_101100b6; written back by FUN_101103d5):
 *   char name[13], name2[13], text[0x400] (the mission summary the briefing screen shows);
 *   i32 n, n x { char name[32], title[128], text[0x400] }      the debriefings (success / failure ...), at most 5;
 *   i32 n, n x { char name[32], file[13] }                     objective sub-briefings;
 *   i32 n, n x { f32 x, y; char icon[13], label[30] }          map markers (x, y in 0..1 of the map; icon upper-cased);
 *   f32 x0, y0, x1, y1                                         where the situation text sits on the map (0..1);
 *   char situation[0x400]; i32 squad; i32 n, n x 21 bytes     the narration (i32 seconds, i32, char wav[13])
 * squad (the in-memory record's +0x2316, read by the briefing's squad panel 0x100ef574): bit 0 the player goes alone,
 * bits 1..10 the pilots 0..9 who may fly with him (one bit alone: that pilot, and no second).
 * Text: '\n' breaks lines, '\t' advances to the next multiple of 10 pixels.
 */
#ifndef HG_BRF_H
#define HG_BRF_H

#include <stddef.h>

#define BRF_TEXT 0x400

typedef struct {
    char name[33], title[129], text[BRF_TEXT + 1];
} brf_debrief;

typedef struct {
    float x, y;
    char  icon[14], label[31];
} brf_marker;

typedef struct {
    int   seconds;     /* how long this part of the narration runs (FUN_10072f4e) */
    char  wav[14];
} brf_narration;

typedef struct hg_brf_s {
    char          summary[BRF_TEXT + 1];
    int           ndebrief;
    brf_debrief   debrief[5];
    int           nobjective;
    char          objective_file[10][14];
    int           nmarker;
    brf_marker    marker[10];
    float         sit_rect[4];
    char          situation[BRF_TEXT + 1];
    int           squad;
    int           nnarration;
    brf_narration narration[32];
} hg_brf;

/* 0 on success */
int brf_parse(const unsigned char *d, size_t len, hg_brf *b);

#endif
