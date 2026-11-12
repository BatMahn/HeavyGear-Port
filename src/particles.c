/* particles.c - the sim's particle engine (HGSIM 0x10079c54 and its kinds; see particles.h) */
#include "particles.h"
#include "fsutil.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PI_F 3.14159265f

enum { PF_TRACER = 1, PF_MISSILE, PF_SMOKE, PF_SPRAY, PF_LASER, PF_ACCEL, PF_RAIL };

struct particles {
    part_spec spec[PART_TYPES];
    part_sys  s[PART_MAX];
    part_view view;
    int       have_view;
    int       live;
    int       off;       /* hgsnd +0x28 Particles off (0x10079cec: 0x1011ea30 = 0) */
    /* the beams' points (the beam pool's +0x92, 0x10018a9c) */
    struct { int used; float pts[16][3]; } beam[PART_BEAMS];
    void (*laser_sound)(void *ctx, int on);   /* 0x1007b956 / 0x1007aec2's AFXWLLLC (particles_set_sound) */
    void *laser_ctx;
};

/* 0x1011e700 + n x 0x1e: the names, and the static defaults (red, green, blue, +0xb, intensity 0xff; size 8, 1 for
 * "accel" and "laser") */
static const char *const type_name[PART_TYPES] = {
    "railgun", "haywire", "accel", "laser", "shockwave", "skate", "gun", "smoke", "grenade", "mortar", "missile",
    "chunket", "tracer", "spark", "missilespark", "laserspark", "pweaponspark", "projpuff", "laserpuff",
    "pweaponpuff", "missilepuff", "footstep"};

/* 0x1011e9d0: the sizes' 24 factors (one in 24 doubles) */
static const float size_jitter[24] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2};
/* 0x1011e998: the corners' texture coordinates, four in a row from a random start (0x10078c29, 0x10086810) */
static const float corner_uv[7][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}};

/* the original's random numbers come from its own table (0x10089e69); here the C library's */
static int prand(int n) { return n > 0 ? rand() % n : 0; }

/* the colour ramps 0x100792ec builds: 0x1018ef20[i] = 0x1f - i / 2; 0x1018ef40[i] = 0xfe - i (0xf0 at 15);
 * 0x1018f010[i] = 0x27 - i; 0x1018f000[i] = (15 - i) / 4 */
static int ramp_grey(int i) { return 0x1f - i / 2; }
static int ramp_smoke(int i) { return i >= 15 ? 0xf0 : 0xfe - i; }

/* ---- PartSpec.ini (GetPrivateProfile*: keys and sections case-insensitive) ---- */

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

static void spec_defaults(part_spec *sp, int t)
{
    memset(sp, 0, sizeof *sp);
    sp->r = sp->g = sp->b = sp->a = sp->intensity = 0xff;
    sp->size = (t == 2 || t == 3) ? 1 : 8;
    sp->slot = -1;
}

static void parse_ini(particles *p, const char *text, size_t len)
{
    int cur = -1;
    char line[512];
    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && text[j] != '\n') j++;
        size_t n = j - i < sizeof line - 1 ? j - i : sizeof line - 1;
        memcpy(line, text + i, n);
        line[n] = 0;
        i = j + 1;
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            char *e = strchr(line, ']');
            if (e) *e = 0;
            cur = -1;
            for (int t = 0; t < PART_TYPES; t++) if (!strcasecmp(line + 1, type_name[t])) cur = t;
            continue;
        }
        if (cur < 0) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        trim(key);
        trim(val);
        part_spec *sp = &p->spec[cur];
        int v = atoi(val);
        if (!strcasecmp(key, "Red")) sp->r = (uint8_t)v;
        else if (!strcasecmp(key, "Green")) sp->g = (uint8_t)v;
        else if (!strcasecmp(key, "Blue")) sp->b = (uint8_t)v;
        else if (!strcasecmp(key, "Intensity")) sp->intensity = (uint8_t)v;
        else if (!strcasecmp(key, "Size")) sp->size = (uint8_t)v;
        else if (!strcasecmp(key, "Lock")) sp->lock = v;
        else if (!strcasecmp(key, "Order")) sp->order = v;
        else if (!strcasecmp(key, "Loop")) sp->loop = v;
        else if (!strncasecmp(key, "Texture", 7)) {
            int k = atoi(key + 7);
            if (k >= 0 && k < 32 && val[0]) {
                snprintf(sp->frame[k], sizeof sp->frame[k], "%.8s", val);
                for (char *c = sp->frame[k]; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
            }
        }
    }
}

particles *particles_new(hg_archive *a, const char *ini)
{
    particles *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    for (int t = 0; t < PART_TYPES; t++) spec_defaults(&p->spec[t], t);
    size_t len = 0;
    uint8_t *d = ini ? fs_read_all(ini, &len) : NULL;
    if (!d && a) d = hga_load(a, "partspec", "INI", &len, NULL);
    if (d) parse_ini(p, (const char *)d, len);
    free(d);
    /* 0x10078e34: the frames are counted up to the first empty Texture<nn>; 0x100790be: the types with frames take
     * texture slots 0x100, 0x101 .. in table order */
    int slot = 0x100;
    for (int t = 0; t < PART_TYPES; t++) {
        part_spec *sp = &p->spec[t];
        int n = 0;
        while (n < 32 && sp->frame[n][0]) n++;
        sp->nframes = n;
        sp->slot = n ? slot++ : -1;
    }
    return p;
}

void particles_free(particles *p) { free(p); }

void particles_set_view(particles *p, const part_view *v)
{
    p->view = *v;
    p->have_view = 1;
}

int particles_count(const particles *p) { return p->live; }

const part_spec *particles_spec(const particles *p, int type)
{
    return type >= 0 && type < PART_TYPES ? &p->spec[type] : NULL;
}

/* ---- the view ---- */

/* camera space: x right, y up, z ahead (as world.c draws) */
static void to_view(const camera *c, const float p[3], float out[3])
{
    float cy = cosf(c->yaw * PI_F / 180), sy = sinf(c->yaw * PI_F / 180);
    float cp = cosf(c->pitch * PI_F / 180), sp = sinf(c->pitch * PI_F / 180);
    float px = p[0] - c->x, py = p[1] - c->y, pz = p[2] - c->z;
    float x1 = px * cy - pz * sy, z1 = px * sy + pz * cy;
    out[0] = x1;
    out[1] = py * cp - z1 * sp;
    out[2] = py * sp + z1 * cp;
    if (c->roll != 0.0f) {   /* the view's roll (world.c view_roll) */
        float cz = cosf(c->roll * PI_F / 180), sz = sinf(c->roll * PI_F / 180), x0 = out[0], y0 = out[1];
        out[0] = cz * x0 + sz * y0;
        out[1] = -sz * x0 + cz * y0;
    }
}

/* 0x100b87f9: a point's screen position; 0 behind or off the screen */
static int project(const camera *c, int w, int h, const float p[3], float *sx, float *sy, float *z)
{
    float v[3];
    to_view(c, p, v);
    *z = v[2];
    if (v[2] <= 1.0f) return 0;
    float f = (float)w * 0.5f / tanf(c->fov * 0.5f * PI_F / 180);
    *sx = (float)w * 0.5f + v[0] * f / v[2];
    *sy = (float)h * 0.5f - v[1] * f / v[2];
    return *sx >= 0 && *sy >= 0 && *sx < (float)w && *sy < (float)h;
}

/* the spawns' test (0x10084362 ..): its distance from the eye if within the far distance, on the screen and in
 * sight, else 0 (no system) */
static float spawn_distance(const particles *p, const float at[3])
{
    if (!p->have_view) return 0;
    const part_view *v = &p->view;
    float dx = at[0] - v->cam.x, dy = at[1] - v->cam.y, dz = at[2] - v->cam.z;
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d > v->far) return 0;
    float sx, sy, z;
    if (!project(&v->cam, v->w, v->h, at, &sx, &sy, &z)) return 0;
    if (v->blocked) {
        float eye[3] = {v->cam.x, v->cam.y, v->cam.z};
        if (v->blocked(v->ctx, at, eye)) return 0;
    }
    return d;
}

/* 0x1007953c / 0x10079c54: a free system (the count below the pool's size, 0x1014b594 + 1 < 0x1014b590) */
static part_sys *alloc_sys(particles *p, int fn, int type, int clock)
{
    if (p->live + 1 >= PART_MAX) return NULL;
    /* 0x1007953c: with particles off only the beams' systems (0x10079c54's last argument 1) are made */
    if (p->off && fn != PF_ACCEL && fn != PF_RAIL && fn != PF_LASER) return NULL;
    for (int i = 0; i < PART_MAX; i++) {
        part_sys *s = &p->s[i];
        if (s->used) continue;
        memset(s, 0, sizeof *s);
        s->used = 1;
        s->fn = fn;
        s->type = type;
        s->start = clock;
        s->beam = -1;
        for (int k = 0; k < 8; k++) s->n[k].frame = -1;
        /* the order: 1 from the first frame, 2 from a random one, else each node random every draw */
        const part_spec *sp = &p->spec[type];
        s->seq = -1;
        if (sp->nframes) s->seq = sp->order == 1 ? 0 : sp->order == 2 ? prand(sp->nframes) : -1;
        p->live++;
        return s;
    }
    return NULL;
}

static void free_sys(particles *p, part_sys *s)
{
    if (!s->used) return;
    s->used = 0;
    p->live--;
}

/* ---- spawns ---- */

/* 0x10084362: eight nodes at (k + 2) / 8 of the frame's motion ahead of the shell's last place; drifting by 1/8 of
 * it each frame */
void particles_tracer(particles *p, int clock, const float at[3], const float d[3])
{
    float dist = spawn_distance(p, at);
    if (dist == 0) return;
    part_sys *s = alloc_sys(p, PF_TRACER, 12, clock);
    if (!s) return;
    s->persp = p->view.camdist / dist / 4.0f;
    for (int k = 0; k < 3; k++) { s->pos[k] = at[k]; s->drift[k] = d[k] / 8.0f; }
    for (int n = 0; n < 8; n++)
        for (int k = 0; k < 3; k++) s->n[n].o[k] = s->drift[k] * (float)(n + 2);
}

/* 0x10082556: the same, each node off by rand(10) - 5, rand(50) - 25, rand(10) - 5 */
void particles_missile(particles *p, int clock, const float at[3], const float d[3])
{
    float dist = spawn_distance(p, at);
    if (dist == 0) return;
    part_sys *s = alloc_sys(p, PF_MISSILE, 10, clock);
    if (!s) return;
    s->persp = p->view.camdist / dist / 4.0f;
    for (int k = 0; k < 3; k++) { s->pos[k] = at[k]; s->drift[k] = d[k] / 8.0f; }
    for (int n = 0; n < 8; n++) {
        s->n[n].o[0] = (float)prand(10) + s->drift[0] * (float)(n + 2) - 5.0f;
        s->n[n].o[1] = (float)prand(50) + s->drift[1] * (float)(n + 2) - 25.0f;
        s->n[n].o[2] = (float)prand(10) + s->drift[2] * (float)(n + 2) - 5.0f;
    }
}

/* 0x100805dc: nodes at (k + 2) / 16 of the motion, each off by rand(50) - 25; drifting by 1/256 of it */
void particles_smoke(particles *p, int clock, const float at[3], const float d[3])
{
    float dist = spawn_distance(p, at);
    if (dist == 0) return;
    part_sys *s = alloc_sys(p, PF_SMOKE, 8, clock);
    if (!s) return;
    s->persp = p->view.camdist / dist / 4.0f;
    for (int k = 0; k < 3; k++) { s->pos[k] = at[k]; s->drift[k] = d[k] / 256.0f; }
    for (int n = 0; n < 8; n++)
        for (int k = 0; k < 3; k++) s->n[n].o[k] = (float)(prand(50) - 25) + d[k] / 16.0f * (float)(n + 2);
}

/* 0x1008514d (the 3D renderers only): the type by the weapon type - 0xf..0x1b "missilepuff", 0x25 / 0x2d
 * "pweaponpuff", 0x21..0x23 / 0x28 "laserpuff", else "projpuff"; the system drifts by -vx / 5, -10, -vz / 5, its
 * nodes start within 5 of it, each with its own motion rand(40) - 20 off the system's */
void particles_spray(particles *p, int clock, const float at[3], const float v[3], int wtype)
{
    if (p->have_view && p->view.software) return;
    float dist = spawn_distance(p, at);
    if (dist == 0) return;
    int type = wtype >= 0xf && wtype <= 0x1b ? 20 : (wtype == 0x25 || wtype == 0x2d) ? 19 :
               ((wtype >= 0x21 && wtype <= 0x23) || wtype == 0x28) ? 18 : 17;
    part_sys *s = alloc_sys(p, PF_SPRAY, type, clock);
    if (!s) return;
    s->persp = p->view.camdist / dist / 4.0f;
    for (int k = 0; k < 3; k++) s->pos[k] = at[k];
    s->drift[0] = -v[0] / 5.0f;
    s->drift[1] = -10.0f;
    s->drift[2] = -v[2] / 5.0f;
    for (int n = 0; n < 8; n++)
        for (int k = 0; k < 3; k++) {
            s->n[n].o[k] = (float)prand(10) - 5.0f;
            s->n[n].v[k] = (float)prand(40) + s->drift[k] - 20.0f;
        }
}

void particles_beam_points(particles *p, int id, const float pts[16][3])
{
    if (id < 0 || id >= PART_BEAMS) return;
    memcpy(p->beam[id].pts, pts, sizeof p->beam[id].pts);
    p->beam[id].used = 1;
}

/* 0x100baf31: kind 6 (accelerators) 15 segments (0x1007acf3 with point k, colour 0x1f) living k x 0x16 ticks;
 * kind 4 (rail guns) every second point (0x1007a0a6) living k x 0x16; others (lasers) 15 segments (0x1007b956,
 * colour 7) living the beam's life (+0x34 - +0x30) */
void particles_beam(particles *p, int clock, int id, int kind, int life, const float pts[16][3])
{
    if (id < 0 || id >= PART_BEAMS) return;
    particles_beam_points(p, id, pts);
    for (int k = 0; k < 15; k++) {
        part_sys *s;
        if (kind == 6) {
            if (!(s = alloc_sys(p, PF_ACCEL, 2, clock))) return;
            s->life = k * 0x16;
            s->colour = 0x1f;
        } else if (kind == 4) {
            if (k & 1) continue;
            if (!(s = alloc_sys(p, PF_RAIL, 0, clock))) return;
            s->life = k * 0x16;
        } else {
            if (!(s = alloc_sys(p, PF_LASER, 3, clock))) return;
            s->life = life;
            s->colour = 7;
            if (p->laser_sound) p->laser_sound(p->laser_ctx, 1);   /* 0x1007b956: AFXWLLLC looped */
        }
        s->beam = id;
        s->seg = k;
        memcpy(s->n[0].o, pts[k], sizeof s->n[0].o);
        memcpy(s->n[1].o, pts[k + 1], sizeof s->n[1].o);
    }
}

/* ---- the frame's update (0x10079dfa) ---- */

void particles_frame(particles *p, int clock, void (*effect)(void *ctx, int type, const float at[3]), void *ctx)
{
    for (int i = 0; i < PART_MAX; i++) {
        part_sys *s = &p->s[i];
        if (!s->used) continue;
        int age = clock - s->start;
        switch (s->fn) {
        case PF_TRACER:   /* 0x10083dde: 0x17 ticks; colour 0x1f */
            if (age >= 0x17 || s->ended) { free_sys(p, s); continue; }
            for (int k = 0; k < 3; k++) s->pos[k] += s->drift[k];
            s->colour = 0x1f;
            break;
        case PF_MISSILE:  /* 0x10081afb: 0x6e ticks; rising rand(10) a frame; the grey ramp by age / 110 x 15 */
            if (age > 0x6e || s->ended) { free_sys(p, s); continue; }
            s->pos[0] += s->drift[0];
            s->pos[1] += (float)prand(10) + s->drift[1];
            s->pos[2] += s->drift[2];
            s->colour = ramp_grey((int)((float)age / 110.0f * 15.0f));
            break;
        case PF_SMOKE:    /* 0x1007ffb3: 0x16a ticks; the nodes wander rand(60) - 30 a frame; 0x1018ef40 by age / 362 */
            if (age > 0x16a || s->ended) { free_sys(p, s); continue; }
            s->pos[0] += s->drift[0];
            s->pos[1] += (float)prand(10) + s->drift[1];
            s->pos[2] += s->drift[2];
            s->colour = ramp_smoke((int)((float)age / 362.0f * 15.0f));
            for (int n = 0; n < 8; n++)
                for (int k = 0; k < 3; k++) s->n[n].o[k] += (float)(prand(60) - 30);
            break;
        case PF_SPRAY: {  /* 0x10084a87: 0x5a ticks; 0x1018f010 / 0x1018f000 by age / 90 x 3 */
            if (age > 0x5a || s->ended) { free_sys(p, s); continue; }
            s->pos[0] += s->drift[0];
            s->pos[1] += (float)prand(10) + s->drift[1];
            s->pos[2] += s->drift[2];
            int ix = (int)((float)age / 90.0f * 3.0f);
            s->colour = 0x27 - ix;
            s->colour2 = (15 - ix) / 4;
            break;
        }
        case PF_LASER: case PF_ACCEL: case PF_RAIL:
            /* 0x1007aec2 / 0x1007a26c / 0x10079f7d: past its life it ends (a rail gun's point with effect 0x49);
             * else it takes its points again */
            if (s->life < age || s->ended) {
                if (s->fn == PF_RAIL && effect) effect(ctx, 0x49, s->n[0].o);
                if (s->fn == PF_LASER && p->laser_sound) p->laser_sound(p->laser_ctx, 0);   /* 0x1007aec2: stopped */
                free_sys(p, s);
                continue;
            }
            if (s->beam >= 0 && p->beam[s->beam].used) {
                if (s->fn == PF_RAIL) memcpy(s->n[0].o, p->beam[s->beam].pts[s->seg + 1], sizeof s->n[0].o);
                else {
                    memcpy(s->n[0].o, p->beam[s->beam].pts[s->seg], sizeof s->n[0].o);
                    memcpy(s->n[1].o, p->beam[s->beam].pts[s->seg + 1], sizeof s->n[1].o);
                }
            }
            break;
        }
    }
}

/* ---- the draw ---- */

/* the type's frame for node n (0x10083e8a's choice): locked nodes keep theirs; else the system's frame steps on per
 * node drawn (loop 1 wraps, 2 holds the last, else the system ends); a negative frame is a random one. -1: the
 * system ended */
static int pick_frame(part_sys *s, const part_spec *sp, int n, int locked)
{
    if (sp->nframes < 2) return 0;
    int f = -1;
    if (!locked) {
        if (s->seq >= 0) {
            f = s->seq + 1;
            if (f >= sp->nframes) {
                if (sp->loop == 1) f = 0;
                else if (sp->loop == 2) f = sp->nframes - 1;
                else { s->seq = -1; s->ended = 1; return -1; }
            }
            s->seq = f;
        }
    } else f = s->n[n].frame;
    if (f < 0) f = prand(sp->nframes);
    s->n[n].frame = f;
    return f;
}

typedef struct { float x, y, z; int size, frame; const part_sys *s; } quad;

static uint32_t pal_rgb(const uint8_t (*pal)[3], int c)
{
    if (!pal) return 0xff000000u | (uint32_t)(c & 255) * 0x010101u;
    const uint8_t *q = pal[c & 255];
    return 0xff000000u | ((uint32_t)q[0] << 16) | ((uint32_t)q[1] << 8) | q[2];
}

static void plot(raster_target *t, int x, int y, float iw, uint32_t c)
{
    if (x < 0 || y < 0 || x >= t->w || y >= t->h) return;
    size_t o = (size_t)y * (size_t)t->w + (size_t)x;
    if (iw <= t->depth[o]) return;
    t->color[o] = c;
}

/* a node's square (0x10078c29): half its size each way, four corners' texture coordinates from a random start; the
 * texels blend by their alpha (DX5DRAW's SRCALPHA / INVSRCALPHA), depth-tested */
static void draw_square(raster_target *t, const quad *q, float half, const raster_texture *tx, uint32_t flat)
{
    int x0 = (int)floorf(q->x - half), x1 = (int)ceilf(q->x + half), y0 = (int)floorf(q->y - half), y1 = (int)ceilf(q->y + half);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    float iw = 1.0f / q->z;
    int r = prand(4);
    const float *c0 = corner_uv[r], *c1 = corner_uv[r + 1], *c2 = corner_uv[r + 2], *c3 = corner_uv[r + 3];
    for (int y = y0; y < y1; y++) {
        if (y < 0 || y >= t->h) continue;
        float tt = ((q->y + half) - ((float)y + 0.5f)) / (2 * half);   /* 0 at the bottom (the first corners) */
        for (int x = x0; x < x1; x++) {
            if (x < 0 || x >= t->w) continue;
            size_t o = (size_t)y * (size_t)t->w + (size_t)x;
            if (iw <= t->depth[o]) continue;
            if (!tx) { t->color[o] = flat; continue; }
            float ss = (((float)x + 0.5f) - (q->x - half)) / (2 * half);
            float u = (1 - ss) * (1 - tt) * c0[0] + ss * (1 - tt) * c1[0] + ss * tt * c2[0] + (1 - ss) * tt * c3[0];
            float v = (1 - ss) * (1 - tt) * c0[1] + ss * (1 - tt) * c1[1] + ss * tt * c2[1] + (1 - ss) * tt * c3[1];
            int tu = (int)(u * (float)tx->w), tv = (int)(v * (float)tx->h);
            if (tu >= tx->w) tu = tx->w - 1;
            if (tv >= tx->h) tv = tx->h - 1;
            if (tu < 0) tu = 0;
            if (tv < 0) tv = 0;
            uint32_t s = tx->texels[(size_t)tv * tx->w + tu], a = s >> 24;
            if (a == 0) continue;
            uint32_t d = t->color[o], out = 0;
            for (int sh = 0; sh < 24; sh += 8) {
                uint32_t sc = (s >> sh) & 255, dc = (d >> sh) & 255;
                out |= ((sc * a + dc * (255 - a) + 127) / 255) << sh;
            }
            t->color[o] = 0xff000000u | out;
        }
    }
}

static int cmp_far(const void *a, const void *b)
{
    float za = ((const quad *)a)->z, zb = ((const quad *)b)->z;
    return za < zb ? 1 : za > zb ? -1 : 0;
}

/* the size the 3D renderers give a node (0x10083e8a ..): base x the type's size x 0x1011e9d0[rand(24)], at least 1;
 * outside, x +0x3fe (when set), else x the zoom; truncated; below 1 the type's size / 2 */
static int node_size(const particles *p, const part_sys *s, const part_spec *sp, int base)
{
    float z = (float)base * (float)sp->size * size_jitter[prand(24)];
    if (z < 1.0f) z = 1.0f;
    if (p->view.outside && s->persp != 0) z *= s->persp;
    else z *= p->view.zoom > 0 ? p->view.zoom : 1.0f;
    int n = (int)z;
    if (n < 1) n = sp->size >> 1;
    return n;
}

/* a beam segment's band (0x1007a379 / 0x1007afdc): from point a to b on the screen, a quad 5 pixels across in the
 * texture of slot 0x100 + (colour >> 1) (the type's own when it has textures), then one a pixel across (2 to 3)
 * in slot 0x100 + colour's; across x when the segment runs more along y, else across y */
static void draw_band(raster_target *t, float scale, float ax, float ay, float az, float bx, float by, float bz,
                      float o0, float o1, const raster_texture *tx)
{
    if (!tx) return;
    int alongy = fabsf(ax - bx) <= fabsf(ay - by);
    raster_vertex v[4];
    memset(v, 0, sizeof v);
    float pa[4][2], o[4] = {o0, o0, o1, o1};
    pa[0][0] = bx; pa[0][1] = by; pa[1][0] = ax; pa[1][1] = ay; pa[2][0] = ax; pa[2][1] = ay; pa[3][0] = bx; pa[3][1] = by;
    for (int k = 0; k < 4; k++) {
        v[k].x = pa[k][0] + (alongy ? o[k] * scale : 0);
        v[k].y = pa[k][1] + (alongy ? 0 : o[k] * scale);
        v[k].iw = (k == 1 || k == 2) ? 1.0f / az : 1.0f / bz;
        v[k].u = corner_uv[k][0]; v[k].v = corner_uv[k][1];
        v[k].r = v[k].g = v[k].b = 1; v[k].fog = 1;
    }
    raster_triangle(t, &v[0], &v[1], &v[2], tx, 0);
    raster_triangle(t, &v[0], &v[2], &v[3], tx, 0);
}

static const raster_texture *slot_texture(const particles *p, int slot, int frame,
                                          const raster_texture *(*tex)(void *, const char *), void *tctx)
{
    if (!tex) return NULL;
    for (int t = 0; t < PART_TYPES; t++) {
        const part_spec *sp = &p->spec[t];
        if (sp->slot != slot || !sp->nframes) continue;
        if (frame < 0 || frame >= sp->nframes) frame = 0;
        const raster_texture *r = tex(tctx, sp->frame[frame]);
        return r && r->texels ? r : NULL;
    }
    return NULL;
}

void particles_draw(particles *p, raster_target *t, const camera *cam, const uint8_t (*pal)[3],
                    const raster_texture *(*tex)(void *ctx, const char *name), void *tctx)
{
    static quad q[PART_MAX * 8];
    int nq = 0;
    int sw = p->view.software || !tex;
    float scale = (float)t->h / 480.0f;   /* the original's sizes are pixels of its 640 x 480 screen */
    for (int i = 0; i < PART_MAX; i++) {
        part_sys *s = &p->s[i];
        if (!s->used || s->ended) continue;
        const part_spec *sp = &p->spec[s->type];
        if (s->fn == PF_RAIL) continue;   /* 0x1007a092 draws nothing */
        if (s->fn == PF_LASER || s->fn == PF_ACCEL) {
            /* the band's two slots: 0x7000 | (+0x346 >> 1) and 0x7000 | +0x346, or the type's slot for both; in
             * SOFTWARE the slots 0x100 + are empty (0x100790be loads none) and nothing is drawn */
            if (sw) continue;
            float ax, ay, az, bx, by, bz;
            project(cam, t->w, t->h, s->n[0].o, &ax, &ay, &az);
            project(cam, t->w, t->h, s->n[1].o, &bx, &by, &bz);
            if (az <= 1.0f || bz <= 1.0f) continue;
            int s1 = sp->slot >= 0 ? sp->slot : 0x100 + (s->colour >> 1);
            int s2 = sp->slot >= 0 ? sp->slot : 0x100 + s->colour;
            draw_band(t, scale, ax, ay, az, bx, by, bz, 0, 5, slot_texture(p, s1, 0, tex, tctx));
            draw_band(t, scale, ax, ay, az, bx, by, bz, 2, 3, slot_texture(p, s2, 0, tex, tctx));
            continue;
        }
        int locked = s->lock, first = nq;
        for (int n = 0; n < 8; n++) {
            float w[3];
            if (s->fn == PF_SPRAY)
                for (int k = 0; k < 3; k++) s->n[n].o[k] += s->n[n].v[k];   /* 0x10084b83 moves each node */
            for (int k = 0; k < 3; k++) w[k] = s->pos[k] + s->n[n].o[k];
            float sx, sy, z;
            if (!project(cam, t->w, t->h, w, &sx, &sy, &z)) continue;
            int frame = 0;
            if (sp->nframes && !sw) {
                frame = pick_frame(s, sp, n, locked);
                if (frame < 0) { nq = first; break; }   /* the animation ran out: nothing drawn (returns 0) */
            }
            int base = 1;
            if (s->fn == PF_SPRAY) base = (int)((float)(s->colour2 * 2000) / z);   /* 0x10084f3f: by 0x100b87f9's z */
            int size = 1;
            if (!sw) size = node_size(p, s, sp, base);
            else if (s->fn == PF_SPRAY) continue;
            if (size <= 0) continue;
            q[nq].x = sx; q[nq].y = sy; q[nq].z = z; q[nq].size = size; q[nq].frame = frame; q[nq].s = s;
            nq++;
        }
        if (sp->lock) s->lock = 1;   /* 0x10079eee: after its first draw a locked type's nodes keep their frames */
    }
    qsort(q, (size_t)nq, sizeof *q, cmp_far);
    for (int i = 0; i < nq; i++) {
        const part_sys *s = q[i].s;
        const part_spec *sp = &p->spec[s->type];
        uint32_t flat = pal_rgb(pal, s->colour);
        if (sw) {
            /* SOFTWARE: the tracer's nodes are single pixels of colour +0x346 (0x100ca6c4); the others squares of size
             * 1 in it (0x10078c29 -> 0x1008bd80's flat path) */
            plot(t, (int)q[i].x, (int)q[i].y, 1.0f / q[i].z, flat);
            continue;
        }
        const raster_texture *tx = NULL;
        if (sp->nframes) tx = tex(tctx, sp->frame[q[i].frame]);
        if (tx && !tx->texels) tx = NULL;
        draw_square(t, &q[i], (float)q[i].size * scale / 2.0f, tx, flat);
    }
}

void particles_set_sound(particles *p, void (*laser)(void *ctx, int on), void *ctx)
{
    p->laser_sound = laser;
    p->laser_ctx = ctx;
}

void particles_set_enabled(particles *p, int on)
{
    if (!p) return;
    p->off = !on;
    if (on) return;
    /* 0x10079cec(0): every live system but the forced ones freed (0x100794ce) */
    for (int i = 0; i < PART_MAX; i++)
        if (p->s[i].used && p->s[i].fn != PF_ACCEL && p->s[i].fn != PF_RAIL && p->s[i].fn != PF_LASER)
            free_sys(p, &p->s[i]);
}
