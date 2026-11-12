/*
 * garagepanel.h - the gear editor's statistics panel (HGSHELL B07: FUN_100a7b0a, run when the design or its selection
 * changes; FUN_100e8a17 the combat priority line).
 *
 * Traced:
 *   - B07CPRTY: "Combat Priority " (0x1022d878) and the game's level's name (0x1011238e(0), 0x101acda0 + level x 5);
 *   - B07WEAPS: the design's items of types 5 .. 10 in the design's order (FUN_1011ddcc), each its short name
 *     (0x101aff98 + id x 0xaa); FUN_100cd1fe colours it by its option word's high half (0x1011b467): 0 0x0f, 1 0xb7,
 *     2 0x07, else 0xfe (the list's +0xce / +0xcf / +0xd0 / +0xc8 from FUN_1013fa9f's FUN_100cd05f(..., 9, 0xfe, 7, 5,
 *     0xf, 0xb7, 7));
 *   - the text: with nothing selected FUN_1016c0cc (garage_design_text); the HP and type spinners (B07HPSPN and its
 *     buttons, B07_TYPE and its arrows) hidden (FUN_10011726 / FUN_100126b0). With an item selected the text box
 *     takes rectangle +0x104 / +0x114 / +0x124 and the item's own text (0x1016d114 ...; not done: the panel shows
 *     nothing then), the engine shows both spinners, a weapon whose +0x3e is 2 .. 4 the type spinner.
 */
#ifndef HG_GARAGEPANEL_H
#define HG_GARAGEPANEL_H

#include "design.h"
#include "shell.h"

/* set the panel for design g, its selected item (pick, -1 none), the game's level */
void garage_panel(shell *sh, const design *g, int pick, int level);

#endif
