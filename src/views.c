#include "views.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

/* 0x10039c56 / 0x10039d72: a filter on value cur (and target the same), time seconds x 181 (0x100eb788) */
static void filt_set(view_filter *f, int now, float value, float seconds, float period)
{
    f->last = now;
    f->target = f->cur = value;
    f->time = seconds * 181.0f;
    f->period = period;
}

/* 0x10039cad: eased toward the target, snapping within 1e-7 or on passing it */
static float filt_run(view_filter *f, int now)
{
    float d = f->target - f->cur;
    if (fabsf(d) < 1e-7f) f->cur = f->target;
    else {
        float s = (float)(now - f->last) * d / f->time;
        if ((d < 0 && s <= d) || (d > 1e-7f && d <= s)) f->cur = f->target;
        else f->cur += s;
    }
    f->last = now;
    return f->cur;
}

/* an angle into (-period / 2, period / 2] (0x10039efa's fold) */
static float fold(float a, float p)
{
    float r = fmodf(a, p);
    if (r > p / 2) r -= p;
    else if (r < -p / 2) r += p;
    return r;
}

/* 0x10039efa: the target, folded */
static void filt_angle_target(view_filter *f, float a) { f->target = fold(a, f->period); }

/* 0x10039dd2: the folded difference, eased the same way, the value kept within the period */
static float filt_angle_run(view_filter *f, int now)
{
    float d = fold(f->target - f->cur, f->period);
    float s = (float)(now - f->last) * d / f->time;
    if ((d < 0 && s <= d) || (d > 1e-7f && d <= s)) f->cur = f->target;
    else f->cur += s;
    f->cur = fmodf(f->cur, f->period);
    f->last = now;
    return f->cur;
}

void views_init(views *v, int now)
{
    memset(v, 0, sizeof *v);
    v->view = v->was = VIEW_COCKPIT;
    v->before = VIEW_COCKPIT;
    v->orbit = 180.0f;   /* 0x10110400 */
    v->zoom = v->zoom1 = v->zoom2 = 1.0f;
    v->missile = -1;
    /* 0x10036cf0 */
    filt_set(&v->fx, now, 0, 0.5f, 0);
    filt_set(&v->fz, now, 0, 0.5f, 0);
    filt_set(&v->fy, now, 0, 0.7f, 0);
    filt_set(&v->look_yaw, now, 0, 0.2f, 0);
    filt_set(&v->look_pitch, now, 0, 0.2f, 0);
    filt_set(&v->fyaw, now, 0, 0.2f, 360.0f);
    filt_set(&v->fpitch, now, 0, 1.0f, 360.0f);
}

float views_fov(float fov1, float zoom)
{
    if (zoom <= 0) zoom = 1;
    return 2.0f * atanf(tanf(fov1 * PI_F / 360.0f) / zoom) * 180.0f / PI_F;
}

void views_cockpit(views *v, int now, const float eye[3], float heading, float pitch, int glance, camera *cam)
{
    if (v->was != VIEW_COCKPIT) {
        /* 0x10037a72 entering: the glances' filters start again (their last time now, their speed 0) */
        v->look_yaw.last = v->look_pitch.last = now;
    }
    /* 0x10037a72: glance_left / glance_right put the pilot pan at -70 / +70, glance_up / glance_down the tilt at -50 /
     * +40 (the first held, in that order); with none held after a frame that had one, both go to 0 */
    float pan = v->pilot_pan, tilt = v->pilot_tilt;
    if (glance & VIEW_GLANCE_LEFT) pan = -70.0f;
    else if (glance & VIEW_GLANCE_RIGHT) pan = 70.0f;
    else if (glance & VIEW_GLANCE_UP) tilt = -50.0f;
    else if (glance & VIEW_GLANCE_DOWN) tilt = 40.0f;
    else if (!v->noglance) pan = tilt = 0.0f;
    v->noglance = !(glance & (VIEW_GLANCE_LEFT | VIEW_GLANCE_RIGHT | VIEW_GLANCE_UP | VIEW_GLANCE_DOWN));
    v->look_yaw.target = pan;
    v->look_pitch.target = tilt;
    /* 0x10037bd8: the gear's angles plus the eased pan and tilt (the original's pitch is down-positive) */
    cam->roll = 0;
    cam->yaw = heading + filt_run(&v->look_yaw, now);
    cam->pitch = pitch - filt_run(&v->look_pitch, now);
    cam->x = eye[0]; cam->y = eye[1]; cam->z = eye[2];
    v->was = VIEW_COCKPIT;
}

int views_zoom(views *v, float zoom_factor)
{
    float was = v->zoom;
    v->zoom1 = zoom_factor;
    v->zoom = v->view == VIEW_COCKPIT ? v->zoom1 : v->zoom2;
    return v->zoom != was;
}

void views_rise(views *v, int now, int t, const float eye[3], float heading, camera *cam)
{
    if (v->was != 4) {
        /* 0x10037c73 entering: the speed 0, straight down, its start now; the zoom (0x100371d9) */
        v->rise_v = 0;
        v->rise_start = now;
        cam->pitch = -90.0f;
        cam->roll = 0;
        cam->yaw = heading;
    }
    float f = (float)t * 0.2368f;   /* 0x1011041c */
    cam->y += (f * 0.5f + v->rise_v) * (float)t;
    v->rise_v += f;
    cam->x = eye[0];
    cam->z = eye[2];
    cam->yaw += (float)(now - v->rise_start) * 0.007f;   /* 0x100eb774 */
    v->was = 4;
}

void views_track(views *v, int now, const float eye[3], float heading, float stand, float root_depth, int crouched,
                 float (*ground)(void *ctx, float x, float z), void *ctx,
                 float d_dist, float d_height, float tilt, float d_pan, camera *cam)
{
    const float ex = eye[0], ey = eye[1], ez = eye[2];
    if (v->was != VIEW_TRACK) {
        if (v->dist == 0.0f) {
            /* the first time: 5 x the standing height, from / 1.75 to x 4, the height a fifth of... / 8 */
            v->dist = stand * 5.0f;
            v->dmin = v->dist / 1.75f;
            v->dmax = v->dist * 4.0f;
            v->h = v->dist / 8.0f;
            v->hmin = -root_depth;
            v->hmin_crouch = -root_depth + 500.0f;
            v->hmax = v->dmax;
        }
        v->fx.last = v->fy.last = v->fz.last = v->fyaw.last = v->fpitch.last = now;
        v->fx.cur = cam->x - ex;
        v->fy.cur = cam->y - ey;
        v->fz.cur = cam->z - ez;
        v->fyaw.cur = cam->yaw;
        v->fpitch.cur = -cam->pitch;
        if (fabsf(cam->x - ex) < 1e-7f) cam->x += 10.0f;
        v->zoom2 = 1.0f;   /* 0x100375db: 0x10110408 = 1, then 0x100371d9 */
    }
    v->dist += d_dist;
    if (v->dist > v->dmax) v->dist = v->dmax;
    else if (v->dist < v->dmin) v->dist = v->dmin;
    float hh = v->h + d_height;
    v->orbit = fmodf(v->orbit + d_pan, 360.0f);
    /* the look at the eye point (0x100b0d4b: its heading and pitch from the camera) */
    float dx = ex - cam->x, dy = ey - cam->y, dz = ez - cam->z;
    float hd = atan2f(dx, dz) * 180.0f / PI_F, pt = atan2f(dy, sqrtf(dx * dx + dz * dz)) * 180.0f / PI_F;
    filt_angle_target(&v->fpitch, -pt - tilt / 2.0f);
    cam->roll = 0;
    cam->pitch = -filt_angle_run(&v->fpitch, now);
    filt_angle_target(&v->fyaw, hd);
    cam->yaw = filt_angle_run(&v->fyaw, now);
    /* the orbit: the gear's heading less the orbit angle, folded to (-180, 180] */
    float a = fold(heading - v->orbit, 360.0f), r = a * PI_F / 180.0f;
    v->fx.target = v->dist * sinf(r);
    v->fz.target = v->dist * sinf(r + PI_F / 2.0f);
    cam->x = filt_run(&v->fx, now) + ex;
    cam->z = filt_run(&v->fz, now) + ez;
    /* kept 200 above the ground (0x1003714c) */
    float yrel = hh, g = ground ? ground(ctx, cam->x, cam->z) + 200.0f : -1e30f;
    if (ey + yrel < g) yrel = g - ey;
    v->h = hh;
    if (hh > v->hmax) v->h = v->hmax;
    else if (crouched && hh < v->hmin_crouch) v->h = v->hmin_crouch;
    else if (hh < v->hmin) v->h = v->hmin;
    v->fy.target = yrel;
    cam->y = filt_run(&v->fy, now) + ey;
    v->was = VIEW_TRACK;
}

void views_ordinance(const float pos[3], const float vel[3], camera *cam)
{
    /* 0x100191d7 for the followed shot: its place, the heading of its motion, level (0x101306e8 ..) */
    cam->x = pos[0]; cam->y = pos[1]; cam->z = pos[2];
    cam->roll = 0;
    cam->yaw = atan2f(vel[0], vel[2]) * 180.0f / PI_F;
    cam->pitch = 0;
}

void views_player_parts(world *w, const unit *u, int show)
{
    float s = sinf(u->s.heading * PI_F / 180), c = cosf(u->s.heading * PI_F / 180);
    for (int k = 0; k < u->gm.nparts; k++) {
        if (u->obj[k] < 0) continue;
        int gone = gearmodel_part_gone(&u->gm, k, u->cb.zone_gone);
        world_show_object(w, u->obj[k], show && !gone);
        if (!show || gone) continue;
        const gearmodel_part *p = &u->gm.part[k];
        world_move_object(w, u->obj[k], u->s.x + p->x * c + p->z * s, u->s.y + p->y, u->s.z - p->x * s + p->z * c,
                          u->s.heading);
        const float ry[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
        float m[9];
        for (int r = 0; r < 3; r++)
            for (int q = 0; q < 3; q++)
                m[r * 3 + q] = ry[r * 3] * p->m[q] + ry[r * 3 + 1] * p->m[3 + q] + ry[r * 3 + 2] * p->m[6 + q];
        world_set_object_matrix(w, u->obj[k], m);
    }
}
