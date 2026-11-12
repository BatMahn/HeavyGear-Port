/*
 * escmenu.h - the sim's main menu (GAMEKEY.MAP MAIN_MENU Esc -> command 0x4b -> 0x10062aba(5)): the radio menu record
 * 5 (0x10118bb0 in 0x1011cbe0[]: its own window with the ESCAPSCR background, src/hud.c escape_menu) and its item
 * lists, traced from HGSIM (labels are the sim's language strings, 0x100a0384 lookups at 0x10099281 ..; the English
 * keys):
 *   MAIN MENU (0x10118a28): Abort Mission (-> list 0x10118620), Monitor Brightness (0x10118778), Audio Ctrl
 *     (0x10122ba0), Combat Variables (0x1011ed40), Flee to Windows (0x101188d0), Accept (type 2, the exit item: 0).
 *   ABORT MISSION: "Confirmation requested" (type 3, unnumbered), 1 Accept -> 0x10030810: hgdif +1 = 0
 *     (invulnerability off), command 0x76 (0x1003b3ba: 0x1014a558 = 1, the sim ends), the menu closed (0x10062b4f).
 *   FLEE TO WINDOWS: 1 Accept -> 0x100308a8: 0x10131458 = 1, hgdif +1 = 0, the player's vehicle destroyed
 *     (0x1005ca49 -> 0x10059e9d), the menu closed; the end sequence then runs (0x1003ad72). Nothing quits: 0x10131458
 *     only keeps 0x1003ad72 on its own path (the player's group isn't decided), and the sim ends as after any loss.
 *   BRIGHTNESS: 1 Monitor Brightness, a slider (0x100645c0, item record 0x101185f8: get 0x100185e0 = level / 15,
 *     preview 0x10018608, set 0x10018650: the level ftol(value x 15) into hgsnd +0x44 and the palette's gamma table
 *     row (0x1003ccd2 -> 0x1003cd04: 0x107ca080, built by 0x1003cc10: (c / 63) ^ (1 / (level x 0.0625 + 0.5)) x 63),
 *     cancel 0x100186a0), 0 Accept.
 *   AUDIO VOLUME: 1 Music (ctx 2), 2 Sound Effects (ctx 0), 3 Voice (ctx 1): sliders on 0x10015450 (get) / 0x1001550d
 *     (preview) / 0x1001569c (set: hgsnd v[3] / v[1] / v[2]) / 0x1001582f (cancel); 3Dsound (type 3: unnumbered, a
 *     choice Off / On shown from 0x1002e648, no setter); 0 Accept.
 *   COMBAT VARIABLES: 1 Particles (hgsnd +0x28, 0x10086a56: 0x10079cec), 2 Shadows (+0x30, 0x10086ac6), 3 Multiple
 *     Lights (+0x2c, 0x10086a8e: 0x1004b2c4), 4 Joystick Auto Center (+0x64, 0x10086a2a): choices Off / On; 0 Accept.
 * A list's item digit selects it (+0x10) and acts at once (0x100636da: '1'.. the numbered items, '0' the exit item);
 * on a slider or a choice that steps it (+0x199a, held to 0 .. 0x10000; the next string), with the preview call; the
 * exit item (Accept) commits every item (list state 4: each handler's setter, 0x100645c0 / 0x10064deb case 4) and
 * goes back (0x100644e4); Esc leaves (state 5: the cancels).
 */
#ifndef HG_ESCMENU_H
#define HG_ESCMENU_H

#include "hud.h"

/* the menus, as the port's menu variable holds them (negative: apart from the radio channels 1 ..) */
enum { ESCMENU_MAIN = -1, ESCMENU_ABORT = -2, ESCMENU_FLEE = -3, ESCMENU_BRIGHT = -4, ESCMENU_AUDIO = -5,
       ESCMENU_COMBAT = -6 };
/* what a pick does */
enum { ESCMENU_NONE, ESCMENU_CLOSE, ESCMENU_OPEN_ABORT, ESCMENU_OPEN_FLEE, ESCMENU_DO_ABORT, ESCMENU_DO_FLEE,
       ESCMENU_UNSUPPORTED, ESCMENU_OPEN_BRIGHT, ESCMENU_OPEN_AUDIO, ESCMENU_OPEN_COMBAT, ESCMENU_STEP,
       ESCMENU_COMMIT };

/* the submenus' values: 16.16 sliders (brightness level / 15; music, effects, voice levels), choices 0 / 1 */
typedef struct {
    int bright, music, fx, voice;
    int sound3d, particles, shadows, lights, joycenter;
    int sel;                  /* the open list's selected item (+0x10) */
} escmenu_vals;

/* fills the HUD's open menu for menu m (one of ESCMENU_*) */
void escmenu_fill(hud_state *hs, int m, const escmenu_vals *v);
/* digit '0'..'9' (0..9) picked in menu m: the action (ESCMENU_STEP has stepped v's item already) */
int  escmenu_pick(int m, int digit, escmenu_vals *v);
/* the brightness level (0 .. 15) of a slider value (0x10018650: ftol(value x 15), rounded) and back (0x100185e0) */
int  escmenu_level(int value);
int  escmenu_value(int level);
/* the gamma table row for a level (0x1003cc10), 64 entries of 6-bit colour */
void escmenu_gamma(int level, unsigned char out[64]);

#endif
