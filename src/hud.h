/*
 * hud.h - the cockpit HUD (HGSIM's HUD windows, 0x10020d48 / 0x10020fc0; RE_NOTES "HUD").
 * Drawn at the original's 640 x 480 layout, then scaled to the target.
 */
#ifndef HG_HUD_H
#define HG_HUD_H

#include "gear.h"
#include "hgprj.h"

#include <stdint.h>

typedef struct hud hud;

/* the reticle shapes (the name table's 51.., 0x1005413c) */
enum { HUD_RET_PROJ = 51, HUD_RET_PROJ_BUSY = 53, HUD_RET_LASER = 55, HUD_RET_LASER_ON = 57, HUD_RET_MISSILE = 59,
       HUD_RET_MISSILE_LOCK = 61 };
/* radar contacts */
enum { HUD_RADAR_FRIEND, HUD_RADAR_ENEMY, HUD_RADAR_NEUTRAL, HUD_RADAR_NAV, HUD_RADAR_NAV_SEEN };

#define HUD_MAX_CONTACTS 96
#define HUD_MENU_MAX 8
enum { HUD_MENU_ITEM = 5, HUD_MENU_SPACER = 3, HUD_MENU_EXIT = 6 };
enum { HUD_MENU_SLIDER = 1, HUD_MENU_CHOICE = 2 };

typedef struct {
    float heading;              /* the engine's degrees */
    float kph;                  /* the speed readout */
    int   backward;
    float throttle;             /* 0..1: the speed bar shows the throttle setting */
    /* the damage display: the player's zones (NULL: undamaged) */
    const float *armor, *internal;
    const int   *ztotal;
    /* the weapon panels */
    int   ammo[GEAR_MAX_WEAPONS], gone[GEAR_MAX_WEAPONS], selected;
    /* the message box: the current objective (NULL: none) */
    const char *obj_label, *obj_text;
    /* the message bar's text (0x1006a0ec / 0x1006a27f: slot 0, HMESSBAR across the screen's top, e.g. "Exiting Mission
     * Area..."), NULL none */
    const char *msg;
    /* the reticle at the aim point's projection */
    int   ret_on;
    float ret_x, ret_y;
    int   ret_shape;
    /* the target (a vehicle): its box or an edge arrow, its compass marker */
    int   tgt;
    int   tgt_in_view;
    float tgt_x, tgt_y, tgt_size;
    int   tgt_side;             /* 0 friend, 1 enemy, 2 neutral (0x10042b07) */
    float tgt_bearing;          /* degrees from the heading, clockwise */
    /* the selected nav */
    int   nav;
    int   nav_in_view, nav_seen;
    float nav_x, nav_y, nav_bearing, nav_range;   /* range in metres */
    const char *nav_name;
    /* the radar: contacts relative to the player, x to the right and z ahead, world units */
    int   ncontacts;
    struct { float x, z; int kind; } contact[HUD_MAX_CONTACTS];
    int   tgt_contact, nav_contact;   /* index into contact[] of the target / nav, -1 none */
    float radar_range;          /* world units across the radar window */
    int   radar_mode;           /* src/radar.h: 0 none, 1 the cockpit radar (window 0), 2 the large radar (the screen's
                                 * middle), 4 the map (contact x / z are then 640 x 480 screen points; only the navs and
                                 * the target / nav marks draw over the world's top-down render) */
    int   hud_off;              /* 0x10118e90 == 0 (F11, or the map): no window draws, the message bar and warn do */
    const char *warn;           /* 0x10056944's line (e.g. "SATELLITE UPLINK NOT FUNCTIONING"), NULL none */
    int   frame;                /* animation counter (the nav marker's frames) */
    /* the message box opened (0x1014cb0c: a radio menu is open, 0x10062afd) */
    int   box_open;
    /* the open radio menu (0x100636da in window 15): its title and items. type: HUD_MENU_ITEM an entry numbered 1..,
     * HUD_MENU_SPACER an unnumbered row (the mission channel's), HUD_MENU_EXIT the exit entry numbered 0 */
    const char *menu_title;
    int   nmenu;
    struct {
        int type; const char *text;
        int kind;              /* the handler's column: 0 none, HUD_MENU_SLIDER value (16.16, 0x100645c0), HUD_MENU_CHOICE
                                * vtext (0x10064deb) */
        int value; const char *vtext;
    } menu[HUD_MENU_MAX];
    int   menu_style;          /* 0 a radio channel's (window 15), 1 the escape screen's (record 5: ESCAPSCR) */
    int   menu_sel;            /* the list's selected item (+0x10), drawn in the record's +0x50 colour */
    /* DISPLAY_OBJECTIVES (0x100b21ce, window 13): the player's group's shown objectives by category (their order
     * byte: 1, 2, 4, 0, 8), each with its state (5 done, 6 failed, else in progress), and the time line */
    int   objlist;
    int   nobjl;
    struct { int order; const char *text; int state; } objl[64];
    const char *time_line;
} hud_state;

/* faction 0 North (the HN... shapes), 1 South (HS...) */
hud *hud_load(hg_archive *a, int faction);
/* the player's gear: its weapons fill the weapon panels (icons are loaded from the archive) */
void hud_set_gear(hud *h, hg_archive *a, const gear *g, int selected);
void hud_free(hud *h);
/* draw over color (w x h, 0xAARRGGBB) with the mission palette */
void hud_draw(const hud *h, uint32_t *color, int w, int h_, const uint8_t (*pal)[3], const hud_state *st);
/* a state with nothing but the heading and the speed (the viewer) */
void hud_state_simple(hud_state *st, float heading, float kph);

#endif
