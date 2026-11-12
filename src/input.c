#include "input.h"

void hg_axis_init(hg_axis *a, int rest, int min, int max, int rate, int shift, int delta)
{
    a->range = max - min;
    a->min16 = min * 65536;
    a->rest = a->range ? ((rest * 65536 - a->min16) * 2) / a->range - 0x10000 : 0;   /* 0x10047850 */
    a->rate = rate;
    a->shift = shift;
    a->delta = delta;
    a->pos = a->rest;
    a->speed = 0;
}

float hg_axis_value(const hg_axis *a)
{
    int p = a->pos;
    if (p > 0x10000) p = 0x10000;
    if (p < -0x10000) p = -0x10000;
    /* 0x10048c58: (float)(((pos + 0x10000) x range / 2 + (min << 16)) >> shift) x 1 / 65536 (0x10101108) */
    long long v = ((long long)(p + 0x10000) * a->range) / 2 + a->min16;
    return (float)(v >> a->shift) * (1.0f / 65536.0f);
}

float hg_axis_step(hg_axis *a, int t, int plus, int minus, int reset)
{
    /* 0x100489a8: "the value" is the speed (+0x16) or, for a _delta axis, the position (+0x1a) itself */
    int *val = a->delta ? &a->pos : &a->speed;
    int moved = 0;
    if (reset) {
        *val = 0;
        a->pos = a->rest;
        moved = 1;
    } else {
        int step = (t * 3) << a->rate;
        if (plus) {
            if (*val < 0) *val = 0;
            *val += step;
            if (*val > 0x8000) *val = 0x8000;
            a->pos += *val;
            if (a->pos > 0x10000) a->pos = 0x10000;
            moved = 1;
        }
        if (minus) {
            if (*val > 0) *val = 0;
            *val -= step;
            if (*val < -0x8000) *val = -0x8000;
            a->pos += *val;
            if (a->pos < -0x10000) a->pos = -0x10000;
            moved = 1;
        }
        if (!moved) *val = 0;
    }
    return hg_axis_value(a);
}

void hg_axes_init(hg_axis ax[AX_COUNT])
{
    /* the control table's records (rest, min, max, +0x18, +0x1c) */
    hg_axis_init(&ax[AX_ZOOM], 1, 1, 4, 6, 0, 0);
    hg_axis_init(&ax[AX_PILOT_TILT], 0, -60, 60, 5, 0, 0);
    hg_axis_init(&ax[AX_PILOT_PAN], 0, -70, 70, 5, 0, 0);
    hg_axis_init(&ax[AX_EYE_TILT], 0, -16, 16, 4, 0, 0);
    hg_axis_init(&ax[AX_EYE_PAN], 0, -72, 72, 4, 0, 1);
    hg_axis_init(&ax[AX_TRACK_DIST], 0, -3600, 3600, 6, 0, 1);
    hg_axis_init(&ax[AX_TRACK_HEIGHT], 0, -3600, 3600, 6, 0, 1);
}
