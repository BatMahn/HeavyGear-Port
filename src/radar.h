/*
 * radar.h - the radar's modes, zoom and the full-screen map, and the HUD's on / off state (traced: HGSIM 0x100891b4 ..
 * 0x100892a1, 0x100878fc, 0x10087b59, the command switch 0x1003b3ba, 0x100654cf / 0x1006539d).
 *
 * Modes (0x1018ea7c, the request 0x1018ea84; records at 0x1011f568): 0 none, 1 the cockpit's radar (window 0, 200000
 * across, zoomable 50000 .. 400000), 2 the large radar (the screen's middle 0.13 .. 0.87 across, full height, the same
 * ranges), 4 the map (the whole screen, 100000; a top-down render of the world, north up, from the gear's place at a
 * height of the range: 0x100784e0, the scene in view 6 0x1002f694, then the navs and the target / nav marks). 3 and 5
 * are the steps into and out of the map (0x100878fc): into it the HUD goes off (0x10118e90 = 0), the mode record's
 * sound plays (+0xc, AFXHDSAT) and the computer says report 0x27; out of it the HUD comes back (1) with the mode's
 * other sound (+0x10, AFXHDSAT) and the mode before. The map closes by itself when the player's gear is lost
 * (0x10148e1c) or its +0x18 has 6.
 *
 * Commands (GAMEKEY.MAP; the command switch's cases are the ids + 1): RADAR_MAP_TOGGLE (F8) opens the map when the
 * gear has its satellite uplink (+0xbb3: the head's option bit 7), else shows "SATELLITE UPLINK NOT FUNCTIONING"
 * (0x10056a61(2): 0x389 ticks, centred, three font heights above the screen's bottom, colour 7, 0x10056944); pressed
 * on the map it closes it (0x10089262). RADAR_ZOOM_IN / OUT (x / Shift+x) halve / double the range, wrapping past
 * the limits to the other end, and only when the option at 0x1014a564 + 9 (hgdif.cfg +9) is set and the map isn't
 * open (0x100892a1); the shell never sets it for a single-player game. NEXT_RADAR_MODE (unbound) steps 0 -> 1 -> 2.
 * TOGGLE_HUD (F11, 0x10118e90): ignored on the map; else 0 -> 1 -> 2 -> 0, 1 the default HUD (0x100654cf: every
 * window's default mode, the compass on, radar mode 1), 2 the saved HUD (0x1006539d: the one SAVE_HUD, unbound,
 * wrote to mercs.hud), none saved: 0. 0: no HUD window draws (0x10021519).
 */
#ifndef HG_RADAR_H
#define HG_RADAR_H

typedef struct {
    int mode, req, before;      /* 0x1018ea7c, 0x1018ea84, 0x1018ea74 */
    int range[5];               /* each mode's +0x18 */
    int hud;                    /* 0x10118e90 */
    int hud_saved;              /* 0x10148e54: a SAVE_HUD was made */
    int msg_until;              /* 0x107beda0: the uplink message's end (ticks), 0 none */
} radar;

/* the radar's state at the sim's start (0x100877d6: mode 0, request 1; the HUD on) */
void radar_init(radar *r);
/* F11: returns 1 if it changed the HUD's state */
int  radar_toggle_hud(radar *r);
/* x / Shift+x (dir 1 in, 2 out, 0 reset): option = hgdif.cfg +9 */
void radar_zoom(radar *r, int dir, int option);
/* F8: uplink = the gear's satellite uplink works; started = the gear has run once (0x10148e28: the message needs it);
 * now = the sim's ticks */
void radar_map_key(radar *r, int uplink, int started, int now);
/* once a frame before the radar draws (0x100878fc): lost = the gear is lost; returns 0, or 3 (into the map: the
 * caller plays the sound and the report) or 5 (out of it: the sound) */
int  radar_step(radar *r, int lost);
/* the range across the radar's window in its current mode */
int  radar_range(const radar *r);

#endif
