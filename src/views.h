/*
 * views.h - the sim's views (traced: HGSIM 0x10036ea6 and its cameras).
 *
 * 0x10036ea6 runs the view (0x101316ec, set by 0x1003719d) each frame: 0 the cockpit (0x10037a72: the eye on the
 * standing skeleton, the glances), 1 the outside track view (0x100375db: an orbit behind the gear eased by the
 * camera filters), 3 the ordinance view (0x1003754a: on the player's last missile until it is gone, then back), 2
 * the free eye (0x10037d5f, the port's free camera). The command COCKPIT_VIEW ('c', GAMEKEY.MAP id 0x13, command
 * 0x14) toggles 0 and 1; ORDINANCE_VIEW (F10, id 0x24, command 0x25) asks for 3 (0x1001ca8a: only while the
 * missile flies).
 *
 * The camera filters (0x10039c56 / 0x10039d72 set them, 0x10039cad / 0x10039dd2 run them): a value eased toward its
 * target by (now - last) x (target - value) / time, snapping on arrival; the angle ones wrap at 360. Times (ticks
 * of 181 a second, 0x100eb788): the track view's x and z offsets 0.5 s, y 0.7 s, yaw 0.2 s, pitch 1 s; the
 * cockpit's glances 0.2 s (0x10036cf0).
 */
#ifndef HG_VIEWS_H
#define HG_VIEWS_H

#include "world.h"
#include "units.h"

enum { VIEW_COCKPIT = 0, VIEW_TRACK = 1, VIEW_FREE = 2, VIEW_ORDINANCE = 3, VIEW_RISE = 4 };

typedef struct { int last; float target, cur, time, period; } view_filter;

typedef struct {
    int   view, was, before;      /* 0x101316ec, the last frame's (0x1011040c), the view before the ordinance one */
    /* the track view (0x101316d4 ..): distance and its limits, height and its limits, the orbit angle (0x10110400) */
    float dist, dmin, dmax, h, hmin, hmin_crouch, hmax, orbit;
    view_filter fx, fy, fz, fyaw, fpitch;   /* 0x107caa50, 0x107ca990, 0x107ca920, 0x107ca960, 0x107ca940 */
    view_filter look_yaw, look_pitch;       /* 0x107caa30, 0x107ca980: the cockpit's glances */
    float zoom;                             /* the camera's zoom (+0x18) as 0x100371d9 sets it */
    float zoom1, zoom2;                     /* 0x10110404 (zoom_factor, the cockpit's) / 0x10110408 (the others') */
    float pilot_pan, pilot_tilt;            /* 0x1013353c / 0x10133538: the pilot's pan and tilt (their axes) */
    int   noglance;                         /* 0x10131714: the last frame had no glance */
    int   rise_start; float rise_v;         /* view 4: its start (0x10131710) and rising speed (0x1013170c) */
    int   missile;                          /* the shot followed (its index), -1 none */
} views;

void views_init(views *v, int now);
/* the cockpit (0x10037a72 -> 0x10037bd8): the eye at eye[], the gear's heading and pitch (degrees, the port's: pitch
 * up); glance the held glances (VIEW_GLANCE_*: glance_left / right / up / down set the pilot pan to -70 / +70 or its
 * tilt to -50 / +40; with none held after a glance both go to 0 once, then follow v->pilot_pan / v->pilot_tilt) */
enum { VIEW_GLANCE_LEFT = 1, VIEW_GLANCE_RIGHT = 2, VIEW_GLANCE_UP = 4, VIEW_GLANCE_DOWN = 8 };
void views_cockpit(views *v, int now, const float eye[3], float heading, float pitch, int glance, camera *cam);
/* 0x100371d9, each frame after the view: zoom1 = zoom_factor (0x10036ea6); the camera's zoom is zoom1 in the cockpit,
 * else zoom2 (1 from the track view, 2 from the ordinance view, 0x1003754a); returns 1 when it changed (the caller
 * plays AFXHDZMI at -25 dB, priority 5, 0x50) */
int  views_zoom(views *v, float zoom_factor);
/* view 4 (0x10037c73, the player's gear wrecked, 0x100620b8 state 5 for 0x389 ticks): from the eye, straight down
 * (pitch 90), rising by its speed (t x 0x1011041c a tick, eased by 0x100eb770) and turning 0x100eb774 a tick */
void views_rise(views *v, int now, int t, const float eye[3], float heading, camera *cam);
/* the track view (0x100375db) round the eye point eye[] of a gear facing heading, standing stand (+0x53c) with its
 * root the given depth below the eye (+0x520, the lowest height), crouched (+0x560 & 0x20); the ground's height at a
 * point from ground(); adjustments per frame: distance, height, the eyepoint's tilt (degrees) and pan (degrees) */
void views_track(views *v, int now, const float eye[3], float heading, float stand, float root_depth, int crouched,
                 float (*ground)(void *ctx, float x, float z), void *ctx,
                 float d_dist, float d_height, float tilt, float d_pan, camera *cam);
/* the ordinance view (0x1003754a / 0x100191d7): the camera at the shot's place looking along its motion, level */
void views_ordinance(const float pos[3], const float vel[3], camera *cam);
/* the camera's horizontal field for its zoom (+0x18 scales the focal length) */
float views_fov(float fov1, float zoom);

/* the player's own gear in the views: its parts placed on its pose and shown (the cockpit too: 0x1002f694 keeps the
 * player's vehicle at detail 0 in the cockpit view, so its gun shows below the eye) or hidden */
/* the player's gear: in the cockpit only its right arm (the dum_rash / rash subtree, gear +0x490) with the near plane
 * at 8 (0x10090d53), elsewhere every part */
void views_player_parts(world *w, const unit *u, int show, int cockpit);

#endif
