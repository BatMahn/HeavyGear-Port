#include "radar.h"

#include <string.h>

/* the mode records (0x1011f568): +0x18 range, +0x20 the default, +0x24 min, +0x28 max */
static const int def_range[5] = {0, 200000, 200000, 0, 100000};
static const int min_range[5] = {0, 50000, 50000, 0, 30000};
static const int max_range[5] = {0, 400000, 400000, 0, 100000};

void radar_init(radar *r)
{
    memset(r, 0, sizeof *r);
    r->mode = 0;
    r->req = 1;
    r->hud = 1;
    memcpy(r->range, def_range, sizeof r->range);
}

int radar_range(const radar *r)
{
    return r->mode >= 0 && r->mode < 5 ? r->range[r->mode] : 0;
}

/* 0x1008920f */
static void request(radar *r, int m)
{
    if (r->mode == m) return;
    if (r->mode < 3 && m < 3) r->req = m;
    else if (r->mode < 3) r->req = 3;
    else { r->req = 5; r->before = m; }
}

int radar_toggle_hud(radar *r)
{
    if (r->mode == 4) return 0;   /* 0x1003b3ba case 0x2b */
    r->hud = (r->hud + 1) % 3;
    if (r->hud == 1) request(r, 1);           /* 0x100654cf: the defaults, radar mode 1 */
    else if (r->hud == 2 && !r->hud_saved) r->hud = 0;   /* 0x1006539d */
    return 1;
}

void radar_zoom(radar *r, int dir, int option)
{
    int m = r->mode;
    if (m <= 0 || m >= 5 || m == 3 || !option || m == 4) return;   /* 0x100892a1 */
    if (dir == 0) r->range[m] = def_range[m];
    else if (dir == 1) {
        r->range[m] /= 2;
        if (r->range[m] < min_range[m]) r->range[m] = max_range[m];
    } else if (dir == 2) {
        r->range[m] <<= 1;
        if (max_range[m] < r->range[m]) r->range[m] = min_range[m];
    }
}

/* 0x10089262 */
static void map_off(radar *r)
{
    if (r->mode == 4 && r->req == 4) r->req = 5;
    else if (r->mode < 3 && r->req < 3) r->req = 3;
}

void radar_map_key(radar *r, int uplink, int started, int now)
{
    /* 0x1003b3ba case 0x4a: no uplink, the message (0x10056a61(2), only once the gear has run: 0x10148e28) */
    if (!uplink) { if (started) r->msg_until = now + 0x389; return; }
    map_off(r);
}

int radar_step(radar *r, int lost)
{
    int ev = 0;
    if (r->mode == 4 && lost) r->req = 5;
    if (r->mode == r->req) return 0;
    switch (r->req) {
    case 0: case 1: case 2:
        r->before = r->mode;
        r->mode = r->req;
        break;
    case 3:
        r->hud = 0;
        r->req = 4;
        ev = 3;
        break;
    case 4:
        r->before = r->mode;
        r->mode = 4;
        break;
    case 5:
        r->hud = 1;
        r->req = r->before;
        ev = 5;
        break;
    }
    return ev;
}
