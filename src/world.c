#include "world.h"
#include "bwd.h"

#include "skygnd.h"
#include "tex.h"
#include "wtb.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265f

typedef struct {
    char           name[33];
    int            ok;
    raster_texture rt;      /* the 3D look's (TEX); rt.texels NULL when there's no TEX */
    raster_texture srt;     /* the SOFTWARE look's (XEL, at its own size); srt.idx NULL when there's no XEL */
    uint32_t      *px;
    uint8_t       *xel;
} tex_entry;

typedef struct {
    int      n;
    uint16_t idx[WTB_MAX_POLY];
    float    nx, ny, nz;
    int      tex;      /* tex_entry index or -1 */
    int      anim;     /* an animated material (MLT type 3): its world anim index, -1 none */
    int      seq;      /* MLT type 3 (an animated sequence, even of one frame): drawn unlit (draw kind 0x3000) */
    uint32_t color;    /* when untextured */
    uint16_t word;     /* MLT types 0 / 1 / 4: the draw word, type << 12 | +0x44 << 8 | +0x48 << 4 (0x100c5e73) */
} mesh_poly;

/* an MLT type 3 sequence: frames first..last, each shown 0xb5 / divisor ticks (0x100bef4c) */
typedef struct {
    char  key[40]; int n; int tex[64]; float ticks_per_frame;
    int   lead;    /* the effect instance that last restarted the sequence (all of a type share it), -1 none */
} world_anim;

typedef struct {
    char        name[11];
    float       sc[3];      /* the OBJ's scale baked into the vertices (0x100c5b46 -> ReadWTB 0x100c5e73) */
    int         ok;
    int         nverts, npolys;
    wtb_vertex *v;
    mesh_poly  *p;
    float       min[3], max[3];
} mesh;

#define MAX_LOD 10

typedef struct {
    int   mesh;
    int   lod_mesh[MAX_LOD];   /* SDF detail levels (mesh index or -1); lod_mesh[0] = mesh */
    float lod_dist[MAX_LOD];   /* switch distance of each level (SDF +0x14c + n*4) */
    int   nlod;
    float x, y, z;
    float rs, rc;      /* yaw sin / cos */
    int   ground;
    int   hidden;
    int   fx;          /* an effect (world_add_effect): turned to face the camera, its animation from fx_age */
    float fx_age;      /* ticks since the effect started */
    int   fx_seq;      /* its sequence: 1 looping, 2 one pass then frame 0, 0 stopped (frame 0) */
    int   solid;       /* a mission object that stops shells (world_ray) */
    int   has_m;       /* a full turn (debris): m, rows of model -> world, instead of the yaw */
    float m[9];
    int   close;       /* drawn with the near plane at 8 (the player's own arm in the cockpit, 0x10090d53) */
} instance;

#define WORLD_LIGHTS 0x100

struct world {
    hg_archive *a;
    float       near_z;   /* the view's near plane (camera +0x3c, world_set_near); 0: the 640-wide view's */
    mission     m;
    mesh       *meshes;  int nmeshes;
    tex_entry  *texs;    int ntexs, missing;
    world_anim *anims;   int nanims;
    instance   *inst;    int ninst;
    int        *obj_inst;         /* mission obj slot -> instance, -1 none */
    float       gmin[3], gmax[3];
    skygnd      sky;
    uint8_t     pal[256][3];      /* PALG[0]'s COL palette, 8-bit */
    int         d3d_grey;         /* 3D look: DX5DRAW's grey vertex shade (d3d_shade); 0: the coloured light sums (3DFX / PowerVR) */
    int         horizon_tex;      /* tex_entry index or -1 */
    int         sw_horizon_tex;   /* SOFTWARE look: horizontexture */
    int         sky_tex;          /* 3D look: skytexture (the cloud layer) */
    int         no_sky, no_filter;   /* B64's Sky off (hgsnd +0x38) / Texture Filtering off (+0x3c) */
    /* the point lights (0x1004b089: a pool of 0x100 records of 0x4c bytes at 0x107c2fa0, a list 0x10138d14 in the
     * order they're taken): +0x14 the world position, +0x3c the radius, +0x44 the life, +0x48 the age */
    struct { int on; float x, y, z, radius; int life, age; } light[WORLD_LIGHTS];
    int         light_order[WORLD_LIGHTS], nlight;
    int         dyn_lights;          /* 0x10118194 (1 at load; hgsnd.cfg +0x2c, 0x10086a8e -> 0x1004b2c4) */
    float       sky_ticks;        /* 5 ms ticks since the start, for the cloud layer's drift */
    uint8_t    *luma;             /* the mission's LTBL table (32 x 256), NULL if none */
    int         software;         /* 1: SOFTWARE look */
    /* shots in flight (world_set_shots): each kind's model (HGBT) and a pool of its instances */
    char        shot_model[WORLD_SHOT_KINDS][17];
    int         shot_cap[WORLD_SHOT_KINDS];
    int        *shot_pool[WORLD_SHOT_KINDS];
    int         shot_npool[WORLD_SHOT_KINDS];
    int         shot_loaded;
};

/* TEX -> ARGB. HG's Direct3D plugin turns a TEX (handed over with its own palette, 0x10002391, and the 16-bit flag
 * +0x18 set in the hardware path, 0x100012c5) into a 4444 surface (DX5DRAW 0x10005690): alpha = 15 - the stored
 * alpha's top four bits, the colours' top four bits each; the port keeps the full colours, alpha as 4 bits x 17. (Every
 * type 0 TEX in the game has stored alpha 0 on its texels: opaque.) */
static int load_anim(world *w, const mlt_entry *me);

static int load_texture(world *w, const char *name)
{
    for (int i = 0; i < w->ntexs; i++)
        if (!strcmp(w->texs[i].name, name)) return w->texs[i].ok ? i : -1;
    w->texs = realloc(w->texs, (size_t)(w->ntexs + 1) * sizeof *w->texs);
    tex_entry *e = &w->texs[w->ntexs];
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", name);
    size_t len;
    uint8_t *d = hga_load(w->a, name, "TEX", &len, NULL);
    hg_texture t;
    if (d && !tex_parse_tex(d, len, &t)) {
        e->px = malloc((size_t)t.w * t.h * 4);
        for (int i = 0; i < t.w * t.h; i++) {
            const uint8_t *p = t.pal[t.idx[i]];
            uint32_t al = (15u - (p[3] >> 4)) * 17u;
            e->px[i] = (al << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
        }
        e->rt.w = t.w; e->rt.h = t.h; e->rt.texels = e->px;
        tex_free(&t);
    }
    free(d);
    /* The software renderer loads the XEL of the name (0x100021e1 / 0x1000189d: type "XEL", 0x1011fd48), whatever its
     * size; a polygon whose texture has none isn't drawn (0x100012c5 / 0x10001513 give up on a NULL texture). */
    size_t xl;
    uint8_t *xd = hga_load(w->a, name, "XEL", &xl, NULL);
    hg_texture x;
    if (xd && !tex_parse_xel(xd, xl, &x)) {
        e->xel = x.idx;
        e->srt.w = x.w; e->srt.h = x.h; e->srt.idx = e->xel;
    }
    free(xd);
    e->ok = e->px || e->xel;
    if (!e->px) w->missing++;
    w->ntexs++;
    return e->ok ? w->ntexs - 1 : -1;
}

static int load_mesh_s(world *w, const char *name, const float sc[3]);
static int load_mesh(world *w, const char *name)
{
    static const float one[3] = {1, 1, 1};
    return load_mesh_s(w, name, one);
}

/* a model at a scale: the OBJ's scale (+6 x3, i32; 0 read as 1, 0x100c5b46) multiplies its vertices as the
 * reader builds the model (x * scale + 0), normals as they are */
static int load_mesh_s(world *w, const char *name, const float sc[3])
{
    for (int i = 0; i < w->nmeshes; i++)
        if (!strcmp(w->meshes[i].name, name) && w->meshes[i].sc[0] == sc[0] && w->meshes[i].sc[1] == sc[1] &&
            w->meshes[i].sc[2] == sc[2]) return w->meshes[i].ok ? i : -1;
    w->meshes = realloc(w->meshes, (size_t)(w->nmeshes + 1) * sizeof *w->meshes);
    mesh *ms = &w->meshes[w->nmeshes++];
    memset(ms, 0, sizeof *ms);
    snprintf(ms->name, sizeof ms->name, "%s", name);
    for (int k = 0; k < 3; k++) ms->sc[k] = sc[k];
    size_t len;
    uint8_t *d = hga_load(w->a, name, "WTB", &len, NULL);
    wtb_model m;
    if (!d || wtb_parse(d, len, &m)) { free(d); return -1; }
    free(d);
    /* the 45 TL1MBA* tiles in MW2's DOS layout (mission 27's ground): the reader (0x100c5e73) parses every record as
     * the HG layout and checks its geometry checksum (+4: per vertex (int)(x + y + z + u + v), per polygon its four
     * indices, +0x10 and n, each step kept to +-0xfffff); theirs fails ("ReadWTB: Bad Geometry Checksum", error 0x58,
     * which ends the sim, 0x100a4060 -> 0x100a4231). The port leaves them out instead of stopping. */
    if (m.count && m.obj[0].legacy) { wtb_free(&m); return -1; }
    /* the record's first object: the reader (0x100c5e01 -> 0x100c5e73) builds a model from the object at its
     * cursor, and the model loaders call it once (osatante is the one record of several objects; its others are
     * the oa / oc / o_ models of the same name, which have records of their own) */
    int nobj = m.count > 1 ? 1 : m.count;
    int nv = 0, np = 0;
    for (int i = 0; i < nobj; i++) { nv += m.obj[i].nverts; np += m.obj[i].npolys; }
    ms->v = malloc((size_t)(nv ? nv : 1) * sizeof *ms->v);
    ms->p = malloc((size_t)(np ? np : 1) * sizeof *ms->p);
    for (int k = 0; k < 3; k++) { ms->min[k] = 1e30f; ms->max[k] = -1e30f; }
    int vb = 0;
    for (int i = 0; i < nobj; i++) {
        wtb_object *o = &m.obj[i];
        for (int j = 0; j < o->nverts; j++) {
            wtb_vertex *v = &ms->v[ms->nverts++];
            *v = o->v[j];
            v->x *= sc[0]; v->y *= sc[1]; v->z *= sc[2];
            float c[3] = {v->x, v->y, v->z};
            for (int k = 0; k < 3; k++) { if (c[k] < ms->min[k]) ms->min[k] = c[k]; if (c[k] > ms->max[k]) ms->max[k] = c[k]; }
        }
        for (int j = 0; j < o->npolys; j++) {
            wtb_poly *g = &o->p[j];
            mesh_poly *q = &ms->p[ms->npolys++];
            q->n = g->n;
            for (int k = 0; k < g->n; k++) q->idx[k] = (uint16_t)(g->idx[k] + vb);
            q->nx = g->nx; q->ny = g->ny; q->nz = g->nz;
            q->tex = -1;
            q->anim = -1;
            q->seq = 0;
            q->color = 0xff808080u;
            /* label (upper-cased, 0x100ea690) -> TLT -> key -> MLT[key] -> texture = MLT name (+0x20, 0x100bef4c).
             * MLT types 0, 1, 4 are untextured (colour); 3 is an animated sequence (first frame drawn for now). */
            const tlt_entry *te = o->legacy ? NULL : material_find_label(&w->m.mat, g->label);
            const mlt_entry *me = te ? material_find_texture(&w->m.mat, te->texture) : NULL;
            if (me) {
                q->color = 0xff000000u | ((uint32_t)me->rgb[0] << 16) | ((uint32_t)me->rgb[1] << 8) | me->rgb[2];
                if (me->type == 0 || me->type == 1 || me->type == 4)
                    q->word = (uint16_t)(me->type << 12 | (me->p44 & 15) << 8 | (me->p48 & 15) << 4);
                if (me->type != 0 && me->type != 1 && me->type != 4) {
                    char nm[40];
                    if (me->type == 3) snprintf(nm, sizeof nm, "%s%s%u", me->name, me->p4c < 10 ? "0" : "", me->p4c);
                    else snprintf(nm, sizeof nm, "%s", me->name);
                    q->tex = load_texture(w, nm);
                    if (me->type == 3) q->anim = load_anim(w, me);
                    q->seq = me->type == 3;
                }
            }
        }
        vb += o->nverts;
    }
    wtb_free(&m);
    ms->ok = 1;
    return w->nmeshes - 1;
}

/* every frame of an MLT type 3 sequence (0x100bef4c: name + the frame number, zero-padded below 10) */
static int load_anim(world *w, const mlt_entry *me)
{
    for (int i = 0; i < w->nanims; i++) if (!strcmp(w->anims[i].key, me->name)) return i;
    world_anim *na = realloc(w->anims, (size_t)(w->nanims + 1) * sizeof *na);
    if (!na) return -1;
    w->anims = na;
    world_anim *a = &w->anims[w->nanims];
    memset(a, 0, sizeof *a);
    snprintf(a->key, sizeof a->key, "%s", me->name);
    a->lead = -1;
    /* at most 0x20 frames (0x10836d20: 0x380 bytes a record, 0x1c a frame; 0x10001725 steps them & 0x1f) */
    for (uint32_t f = me->p4c; f <= me->p50 && a->n < 32; f++) {
        char nm[40];
        snprintf(nm, sizeof nm, "%s%s%u", me->name, f < 10 ? "0" : "", f);
        a->tex[a->n++] = load_texture(w, nm);
    }
    /* a frame every 0xb5 / rate ticks, in whole ticks (0x100bef4c: 0xb5 / +0x54, integer division) */
    a->ticks_per_frame = me->p54 > 0 ? (float)(0xb5 / me->p54) : (float)0xb5;
    if (a->ticks_per_frame < 1.0f) a->ticks_per_frame = 1.0f;
    return w->nanims++;
}

/* height of the ground at (x, z): the highest ground polygon over the point */
float world_ground_height_n(const world *w, float x, float z, int *found, float n[3])
{
    float best = -1e30f;
    if (n) { n[0] = 0; n[1] = 1; n[2] = 0; }
    for (int i = 0; i < w->ninst; i++) {
        const instance *in = &w->inst[i];
        if (!in->ground) continue;
        const mesh *ms = &w->meshes[in->mesh];
        if (x < ms->min[0] || x > ms->max[0] || z < ms->min[2] || z > ms->max[2]) continue;
        for (int j = 0; j < ms->npolys; j++) {
            const mesh_poly *q = &ms->p[j];
            for (int k = 1; k + 1 < q->n; k++) {
                const wtb_vertex *a = &ms->v[q->idx[0]], *b = &ms->v[q->idx[k]], *c = &ms->v[q->idx[k + 1]];
                float d = (b->x - a->x) * (c->z - a->z) - (c->x - a->x) * (b->z - a->z);
                if (fabsf(d) < 1e-6f) continue;
                float s = ((x - a->x) * (c->z - a->z) - (c->x - a->x) * (z - a->z)) / d;
                float t = ((b->x - a->x) * (z - a->z) - (x - a->x) * (b->z - a->z)) / d;
                if (s < 0 || t < 0 || s + t > 1) continue;
                float h = a->y + s * (b->y - a->y) + t * (c->y - a->y);
                if (h > best) {
                    best = h;
                    if (n) {   /* the polygon's normal, facing up (0x10022591 keeps the struck polygon's) */
                        float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
                        float vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
                        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                        float l = sqrtf(nx * nx + ny * ny + nz * nz);
                        if (ny < 0) l = -l;
                        if (l != 0) { n[0] = nx / l; n[1] = ny / l; n[2] = nz / l; }
                    }
                }
            }
        }
    }
    if (found) *found = best > -1e29f;
    return best > -1e29f ? best : 0.0f;
}

/* the ground's height at (x, z) and the normal of the polygon it lies on (0x10022591 keeps it in 0x10130818..20) */
static float ground_at(const world *w, float x, float z, int *found, float nrm[3])
{
    float best = -1e30f;
    if (nrm) { nrm[0] = 0; nrm[1] = 1; nrm[2] = 0; }
    for (int i = 0; i < w->ninst; i++) {
        const instance *in = &w->inst[i];
        if (!in->ground) continue;
        const mesh *ms = &w->meshes[in->mesh];
        if (x < ms->min[0] || x > ms->max[0] || z < ms->min[2] || z > ms->max[2]) continue;
        for (int j = 0; j < ms->npolys; j++) {
            const mesh_poly *q = &ms->p[j];
            for (int k = 1; k + 1 < q->n; k++) {
                const wtb_vertex *a = &ms->v[q->idx[0]], *b = &ms->v[q->idx[k]], *c = &ms->v[q->idx[k + 1]];
                float d = (b->x - a->x) * (c->z - a->z) - (c->x - a->x) * (b->z - a->z);
                if (fabsf(d) < 1e-6f) continue;
                float s = ((x - a->x) * (c->z - a->z) - (c->x - a->x) * (z - a->z)) / d;
                float t = ((b->x - a->x) * (z - a->z) - (x - a->x) * (b->z - a->z)) / d;
                if (s < 0 || t < 0 || s + t > 1) continue;
                float h = a->y + s * (b->y - a->y) + t * (c->y - a->y);
                if (h > best) { best = h; if (nrm) { nrm[0] = q->nx; nrm[1] = q->ny; nrm[2] = q->nz; } }
            }
        }
    }
    if (found) *found = best > -1e29f;
    return best > -1e29f ? best : 0.0f;
}

float world_ground_height(const world *w, float x, float z, int *found)
{
    return ground_at(w, x, z, found, NULL);
}

/* 0x1006f4ee: the angle (degrees) of (b, a) from b's axis toward a's: atan2(a, b) */
static float atan2deg(float a, float b)
{
    return atan2f(a, b) * 180.0f / 3.14159265f;
}

/* Objects named o...: their SDF record (HGSIM 0x100c1152, the "SDF" lookup at 0x10122d50) lists up to 10 model names at
 * +0x0c + n*0x20. For o_ objects these are detail levels with a float switch distance each at +0x14c + n*4; for oc
 * objects (second letter 'c', special-cased by the loader) they are the collapse animation's frames. */
static void load_lods(world *w, instance *in, const char *model, const float sc[3])
{
    in->nlod = 1;
    in->lod_mesh[0] = in->mesh;
    in->lod_dist[0] = 0;
    if (model[0] != 'o' || model[1] == 'c') return;
    size_t len;
    uint8_t *d = hga_load(w->a, model, "SDF", &len, NULL);
    if (!d || len < 0x14c + 4 * MAX_LOD) { free(d); return; }
    {
        uint32_t u = (uint32_t)d[0x14c] | ((uint32_t)d[0x14d] << 8) | ((uint32_t)d[0x14e] << 16) | ((uint32_t)d[0x14f] << 24);
        memcpy(&in->lod_dist[0], &u, 4);
    }
    for (int n = 1; n < MAX_LOD; n++) {
        char nm[33];
        memcpy(nm, d + 0x0c + n * 0x20, 32);
        nm[32] = 0;
        if (!nm[0]) break;
        for (char *c = nm; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        int mi = load_mesh_s(w, nm, sc);
        float dist;
        uint32_t u = (uint32_t)d[0x14c + n * 4] | ((uint32_t)d[0x14d + n * 4] << 8) | ((uint32_t)d[0x14e + n * 4] << 16) |
                     ((uint32_t)d[0x14f + n * 4] << 24);
        memcpy(&dist, &u, 4);
        in->lod_mesh[in->nlod] = mi;
        in->lod_dist[in->nlod] = dist;
        in->nlod++;
    }
    free(d);
}

/* A GT structure's replacement OBJ (its 'oa' remains) is flagged hidden (0x800) when the GT is read (0x1009d8f2) and
 * shown in the structure's place when it is destroyed (0x1009d97e); children follow their parent's visibility
 * (0x1009dc4d). */
static int obj_hidden_at_start(const mission *m, int i)
{
    for (int t = 0; t < m->ntgt; t++)
        if (m->tgt[t].alt == i) return 1;
    return 0;
}

/* the view's roll (camera R = Ry Rx Rz as 0x10051fb0 / 0x10052411: view = Rz^T after the yaw and the pitch) */
static void view_roll(const camera *cam, float *x, float *y)
{
    if (cam->roll == 0.0f) return;
    float cz = cosf(cam->roll * PI_F / 180), sz = sinf(cam->roll * PI_F / 180), x0 = *x, y0 = *y;
    *x = cz * x0 + sz * y0;
    *y = -sz * x0 + cz * y0;
}

world *world_load(hg_archive *a, const char *mission_name, char *err, size_t errlen)
{
    world *w = calloc(1, sizeof *w);
    if (w) { w->d3d_grey = 1; w->dyn_lights = 1; }
    w->a = a;
    if (mission_load(&w->m, a, mission_name, err, errlen)) { free(w); return NULL; }
    for (int k = 0; k < 3; k++) { w->gmin[k] = 1e30f; w->gmax[k] = -1e30f; }
    w->inst = calloc((size_t)(w->m.nobj ? w->m.nobj : 1), sizeof *w->inst);
    w->obj_inst = malloc((size_t)(w->m.nobj ? w->m.nobj : 1) * sizeof *w->obj_inst);
    for (int i = 0; i < w->m.nobj; i++) w->obj_inst[i] = -1;
    for (int pass = 0; pass < 2; pass++) {   /* ground first, so objects can be dropped onto it */
        for (int i = 0; i < w->m.nobj; i++) {
            mission_obj *o = &w->m.obj[i];
            if (o->ground != (pass == 0)) continue;
            o->oy_world = o->y;
            if (o->child && o->parent >= 0) {
                /* under its parent's node: follows the parent where it was stood (mission.c) */
                const mission_obj *p = &w->m.obj[o->parent];
                float dy = p->y - p->oy_world;
                o->y += dy;
            } else if (o->in_block && o->y < 0.0f) {
                /* 0x1009de8e (instancing a block object): below y 0 it stands on the ground at its x, z
                 * (0x10022591) */
                int f;
                float n[3];
                float h = ground_at(w, o->x, o->z, &f, n);
                if (f) o->y = h;
                /* a boulder (its name holds "BOLD", strstr after 0x100a2bc9 upper-cases it) is turned to the
                 * ground's slope: 0x10051fb0 order 2 with x = 90 - atan2(ny, nz), y its own yaw (OBJ rot y),
                 * z = atan2(ny, nx) - 90, (nx, ny, nz) the ground polygon's normal; this replaces its turn (the
                 * block's too) */
                char up[11];
                for (int k = 0; k < 11; k++) up[k] = (char)toupper((unsigned char)o->model[k]);
                if (f && strstr(up, "BOLD")) {
                    const float d = 3.14159265f / 180.0f;
                    float ax = (90.0f - atan2deg(n[1], n[2])) * d, ay = o->rot[1] * d, az = (atan2deg(n[1], n[0]) - 90.0f) * d;
                    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
                    float *m = o->m;
                    m[0] = cz * cy; m[3] = cx * cy * sz + sx * sy; m[6] = cy * sz * sx - cx * sy;
                    m[1] = -sz;     m[4] = cz * cx;                m[7] = cz * sx;
                    /* the third column: the first two's cross product (0x10051a6c mode 2) */
                    m[2] = m[3] * m[7] - m[6] * m[4];
                    m[5] = m[6] * m[1] - m[0] * m[7];
                    m[8] = m[0] * m[4] - m[3] * m[1];
                }
            }
            /* hidden: a GT's replacement, or under a hidden parent (0x1009dc4d) */
            o->hidden = obj_hidden_at_start(&w->m, i) || (o->parent >= 0 && w->m.obj[o->parent].hidden);
            /* 'oc' objects (second letter 'c') are SDF collapse sequences (0x100c1152 loads their names as frames);
             * the port doesn't play them */
            if (o->model[0] == 'o' && o->model[1] == 'c') continue;
            int mi = load_mesh_s(w, o->model, o->scale);
            if (mi < 0) continue;
            w->obj_inst[i] = w->ninst;
            instance *in = &w->inst[w->ninst++];
            in->mesh = mi;
            in->hidden = o->hidden;
            load_lods(w, in, o->model, o->scale);
            in->x = o->x; in->y = o->y; in->z = o->z;
            in->ground = o->ground;
            in->solid = !o->ground;
            in->x = o->x; in->y = o->y; in->z = o->z;
            /* a turn about y alone keeps the yaw form; any pitch or roll (here or from the block) uses the matrix */
            if (fabsf(o->m[1]) > 1e-5f || fabsf(o->m[3]) > 1e-5f || fabsf(o->m[5]) > 1e-5f || fabsf(o->m[7]) > 1e-5f) {
                for (int k = 0; k < 9; k++) in->m[k] = o->m[k];
                in->has_m = 1;
            }
            in->rs = o->m[2]; in->rc = o->m[0];
            {
                float l = sqrtf(in->rs * in->rs + in->rc * in->rc);
                if (l > 1e-6f) { in->rs /= l; in->rc /= l; } else { in->rs = 0; in->rc = 1; }
            }
            if (o->ground) {
                mesh *ms = &w->meshes[mi];
                for (int k = 0; k < 3; k++) {
                    if (ms->min[k] < w->gmin[k]) w->gmin[k] = ms->min[k];
                    if (ms->max[k] > w->gmax[k]) w->gmax[k] = ms->max[k];
                }
            }
        }
    }
    /* palette: the PALG slot for the starting time of day (mission.c, 0x10030611 / 0x10110060) */
    snprintf(w->sky.hardwarehorizon, sizeof w->sky.hardwarehorizon, "%s", "");
    w->horizon_tex = -1;
    if (w->m.palette[0]) {
        size_t len;
        uint8_t *d = hga_load(a, w->m.palette, "COL", &len, NULL);
        if (d && len >= 768)
            for (int i = 0; i < 256; i++)
                for (int k = 0; k < 3; k++) w->pal[i][k] = (uint8_t)((d[i * 3 + k] << 2) | (d[i * 3 + k] >> 4));
        free(d);
    }
    /* the luma table named by the ART record's LTBL chunk */
    w->luma = NULL;
    if (w->m.luma[0]) {
        size_t len;
        uint8_t *d = hga_load(a, w->m.luma, "TBL", &len, NULL);
        if (d && len >= 32 * 256) w->luma = d; else free(d);
    }
    w->sw_horizon_tex = -1;
    w->sky_tex = -1;
    return w;
}

int world_set_skygnd(world *w, const char *path)
{
    if (skygnd_load(path, w->m.scn, &w->sky)) return -1;
    if (w->sky.hardwarehorizon[0]) w->horizon_tex = load_texture(w, w->sky.hardwarehorizon);
    w->sw_horizon_tex = w->sky.horizontexture[0] ? load_texture(w, w->sky.horizontexture) : -1;
    w->sky_tex = w->sky.skytexture[0] ? load_texture(w, w->sky.skytexture) : -1;
    return 0;
}

const uint8_t (*world_palette(const world *w))[3]
{
    return (const uint8_t (*)[3])w->pal;
}

void world_set_time(world *w, float seconds)
{
    w->sky_ticks = seconds * 200.0f;   /* the sim's tick is 5 ms (0x1001fe62, 0x1001fd80) */
}

void world_set_options(world *w, int sky, int filter)
{
    w->no_sky = !sky;
    w->no_filter = !filter;
}

int world_set_software(world *w, int on)
{
    w->software = on && w->luma;
    return w->software;
}

void world_stats(const world *w, int *objects, int *models, int *textures, int *missing_tex)
{
    *objects = w->ninst; *models = w->nmeshes; *textures = w->ntexs; *missing_tex = w->missing;
}

void world_start_camera(const world *w, camera *cam)
{
    int f;
    /* Horizontal field of view: the eyepoint's focal length is half the view width times the zoom (+0x18, 1.0
     * unzoomed; HGSIM 0x100b8496), so the cockpit view is 90 degrees across. */
    cam->pitch = 0; cam->fov = 90; cam->roll = 0;
    mission_start st = mission_group_start(&w->m, 0);
    if (st.found) {
        /* The player's squad is group 0 (HGSHELL 0x100ff1e6 writes the player's GPS with group 0, leader 1, role 0).
         * SimMain's DoFirstObjtv puts the group's leader on its start navpoint, facing the navpoint's heading
         * (0x10066a68 -> 0x1004271a); the gear then stands on the ground. */
        float h = st.heading * 3.14159265f / 180.0f;
        cam->yaw = st.heading;
        /* The gear's origin stands 245.3 above the ground (its skeleton's foot depth, gear +0x24 -> +0x53c, set by the
         * GPS loader 0x100c4b3a from node positions), and the eye rides on the skeleton 171.3 above the origin and
         * 48.3 ahead of it. Both are read from the running original at mission 1's start (the Hunter at rest:
         * ground 73.0, gear 318.3, eyepoint 0x1011f908 at 489.6). The skeleton maths behind them is not traced yet,
         * so these are MEASURED for the Hunter, not traced. */
        cam->x = st.x + sinf(h) * 48.3f;
        cam->z = st.z + cosf(h) * 48.3f;
        cam->y = world_ground_height(w, st.x, st.z, &f) + 245.3f + 171.3f;
        if (f) return;
    }
    cam->x = (w->gmin[0] + w->gmax[0]) * 0.5f;
    cam->z = (w->gmin[2] + w->gmax[2]) * 0.5f;
    cam->y = world_ground_height(w, cam->x, cam->z, &f) + 600.0f;
    cam->yaw = 0; cam->pitch = -5; cam->fov = 75; cam->roll = 0;
}

void world_free(world *w)
{
    if (!w) return;
    for (int i = 0; i < w->nmeshes; i++) { free(w->meshes[i].v); free(w->meshes[i].p); }
    for (int i = 0; i < w->ntexs; i++) { free(w->texs[i].px); free(w->texs[i].xel); }
    free(w->luma);
    free(w->meshes); free(w->texs); free(w->inst); free(w->obj_inst); free(w->anims);
    for (int k = 0; k < WORLD_SHOT_KINDS; k++) free(w->shot_pool[k]);
    mission_free(&w->m);
    free(w);
}

/* ---- drawing ---- */

typedef struct { float x, y, z, u, v, lr, lg, lb, f, lum; } vview;   /* view space + light + fog + luma row */

/* The near plane (camera +0x3c, set by 0x100b8496): half the view's width x 0.5 + 1, 161 for the 640-wide view,
 * whatever the window's size; 8 in the track view (0x1003719d's caller, view mode 1) and for the player's own arm in
 * the cockpit (0x10090d53). Polygons are clipped there (0x10092667: z >= +0x3c). */
#define NEAR_640 161.0f
static float g_near = NEAR_640;
#define NEAR_Z g_near

static int clip_near(const vview *in, int n, vview *out)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const vview *a = &in[i], *b = &in[(i + 1) % n];
        int ain = a->z >= NEAR_Z, bin = b->z >= NEAR_Z;
        if (ain) out[m++] = *a;
        if (ain != bin) {
            float t = (NEAR_Z - a->z) / (b->z - a->z);
            vview v = { a->x + t * (b->x - a->x), a->y + t * (b->y - a->y), NEAR_Z,
                        a->u + t * (b->u - a->u), a->v + t * (b->v - a->v), a->lr + t * (b->lr - a->lr),
                        a->lg + t * (b->lg - a->lg), a->lb + t * (b->lb - a->lb), a->f + t * (b->f - a->f),
                        a->lum + t * (b->lum - a->lum) };
            out[m++] = v;
        }
    }
    return m;
}

/* The 3D renderers' vertex light (HGSIM 0x1004bad6; the intensity is 0x1009250c: i = clamp(dot(normal, light) /
 * |light| * 128, -127, 127), the 128 at 0x101035e8):
 *   start from hambient;
 *   the ART record's LITE light (on unless its direction is 999999 x3) adds gl * j >> 7, j = i * hardwareintensity / 128;
 *   SKYGND's l1 and l2 add colour * j >> 7, j = i * 255 / 128;
 * each only when j > 0, then each channel is clamped to 0..255. The normal is the vertex's (stored negated, as loaded). */
static void light_vertex(const world *w, const instance *in, const wtb_vertex *v, vview *o)
{
    const skygnd *s = &w->sky;
    const mission *m = &w->m;
    if (!s->found) { o->lr = o->lg = o->lb = 1.0f; return; }
    float nx = v->nx * in->rc + v->nz * in->rs, nz = -v->nx * in->rs + v->nz * in->rc, ny = v->ny;
    if (in->has_m) {
        nx = in->m[0] * v->nx + in->m[1] * v->ny + in->m[2] * v->nz;
        ny = in->m[3] * v->nx + in->m[4] * v->ny + in->m[5] * v->nz;
        nz = in->m[6] * v->nx + in->m[7] * v->ny + in->m[8] * v->nz;
    }
    int c[3] = {s->hambient[0], s->hambient[1], s->hambient[2]};
    if (m->has_lite && !(m->lite_dir[0] == 999999 && m->lite_dir[1] == 999999 && m->lite_dir[2] == 999999)) {
        float lx = (float)m->lite_dir[0], ly = (float)m->lite_dir[1], lz = (float)m->lite_dir[2];
        float len = sqrtf(lx * lx + ly * ly + lz * lz);
        int hi = s->hardwareintensity < 0 ? 0 : s->hardwareintensity > 0x80 ? 0x80 : s->hardwareintensity;   /* 0x1004b249 */
        if (len > 0) {
            int i = (int)((nx * lx + ny * ly + nz * lz) / len * 128.0f);
            if (i > 127) i = 127;
            if (i < -127) i = -127;
            int j = (int)((float)(i * hi) / 128.0f);
            if (j > 0) for (int k = 0; k < 3; k++) c[k] += s->gl[k] * j >> 7;
        }
    }
    const float *dirs[2] = {s->l1dir, s->l2dir};
    const int *cols[2] = {s->l1, s->l2};
    for (int l = 0; l < 2; l++) {
        float d = nx * dirs[l][0] + ny * dirs[l][1] + nz * dirs[l][2];
        int i = (int)(d * 128.0f);
        if (i > 127) i = 127;
        if (i < -127) i = -127;
        int j = (int)((float)(i * 255) / 128.0f);
        if (j > 0) for (int k = 0; k < 3; k++) c[k] += cols[l][k] * j >> 7;
    }
    for (int k = 0; k < 3; k++) c[k] = c[k] < 0 ? 0 : c[k] > 255 ? 255 : c[k];
    o->lr = (float)c[0] / 255.0f; o->lg = (float)c[1] / 255.0f; o->lb = (float)c[2] / 255.0f;
}

/* 0x1009250c with 0x107c276c clear: d = p - pos; all |d| below 1e-7 (0x101035e0) gives 0x7f, else
 * ftol(dot(n, d) / |d| x 128) (the dot and the quotient held in floats), -127..127 */
static int point_i(const float p[3], float x, float y, float z, const wtb_vertex *v)
{
    float dx = p[0] - x, dy = p[1] - y, dz = p[2] - z;
    if (fabsf(dx) < 1e-7f && fabsf(dy) < 1e-7f && fabsf(dz) < 1e-7f) return 0x7f;
    float dot = v->nx * dx + v->ny * dy + v->nz * dz;
    float q = dot / sqrtf(dx * dx + dy * dy + dz * dz);
    int i = (int)(q * 128.0f);
    if (i < -0x7f) i = -0x7f;
    if (i > 0x7f) i = 0x7f;
    return i;
}

/* A polygon's point lights (0x1004bad6's last pass, 0x1004b7f3): for each light in the list whose box (its world
 * position +- radius) holds one of the polygon's vertices' world positions (+0xc..+0x14), 0x107c2f84.. = the light's
 * model-space position (0x1004b44a: M^T (light - the object's position)) minus the polygon's first vertex's model
 * position (+0..+8); "near" when that is under 1e-7 on every axis (0x1010113c). Each vertex then takes 0x80 when near,
 * else 0x1009250c(its world position, its model normal +0x30) - the spaces mixed as the original mixes them. */
typedef struct { int n; float d0[8][3]; int near_[8]; } poly_lights;

/* The Direct3D look's vertex shade (DX5DRAW). 0x1006fffd sets 0x1014b954 only for a "3DFX" or "POWERVR" plugin, so
 * with DX5DRAW the sim hands each vertex the view's tint (0x10118198: white) as its colour (0x100012c5), not the
 * coloured light sums above, and DX5DRAW's colour is alpha | tint byte x I >> 8 per channel (0x10004360), I the
 * vertex intensity: 0x1004bad6's 3D branch - the LITE ambient (+0x2a, 0x1004b43d) plus the LITE light's i x
 * hardwareintensity / 128 (0x1004b249) when that is > 0, doubled (DAT_1014a5e8), held to 0x80, times the draw word's
 * scale (255 for textured kinds) >> 10, 4..31 (0x10026610) - handed over as row << 3 | 7 (0x100012c5). The fog is
 * D3D's (the vertex's +0x2b), not in I. Returns I x 255 >> 8 / 255, the texel's multiplier (MODULATE). */
static float d3d_shade(const world *w, const instance *in, const wtb_vertex *v, int scale, const poly_lights *pl,
                       const float *wp)
{
    const skygnd *s = &w->sky;
    const mission *m = &w->m;
    int L = m->has_lite ? m->lite_ambient : 0;
    if (m->has_lite && !(m->lite_dir[0] == 999999 && m->lite_dir[1] == 999999 && m->lite_dir[2] == 999999)) {
        float lx = (float)m->lite_dir[0], ly = (float)m->lite_dir[1], lz = (float)m->lite_dir[2];
        float len = sqrtf(lx * lx + ly * ly + lz * lz);
        int hi = s->found ? s->hardwareintensity : 0x80;
        if (hi < 0) hi = 0;
        if (hi > 0x80) hi = 0x80;
        if (len > 0) {
            float nx = v->nx * in->rc + v->nz * in->rs, nz = -v->nx * in->rs + v->nz * in->rc, ny = v->ny;
            if (in->has_m) {
                nx = in->m[0] * v->nx + in->m[1] * v->ny + in->m[2] * v->nz;
                ny = in->m[3] * v->nx + in->m[4] * v->ny + in->m[5] * v->nz;
                nz = in->m[6] * v->nx + in->m[7] * v->ny + in->m[8] * v->nz;
            }
            int i = (int)((nx * lx + ny * ly + nz * lz) / len * 128.0f);
            if (i > 127) i = 127;
            if (i < -127) i = -127;
            int li = (int)((float)(i * hi) / 128.0f);
            if (li > 0) L += li;
        }
    }
    if (L < 0) L = 0;
    for (int k = 0; pl && k < pl->n; k++) {   /* the point lights (0x1004bad6), each while under 0x80 */
        if (L >= 0x80) break;
        int i = pl->near_[k] ? 0x80 : point_i(pl->d0[k], wp[0], wp[1], wp[2], v);
        if (i > 0) L += i;
    }
    L <<= 1;
    if (L > 0x80) L = 0x80;
    int row = L * scale >> 10;
    if (row < 4) row = 4;
    if (row > 31) row = 31;
    int I = row << 3 | 7;
    return (float)(0xff * I >> 8) / 255.0f;
}

/* Draw a world-space polygon (the sky): view transform, near clip, raster. Unlit, unfogged. */
static void draw_world_poly(const world *w, raster_target *t, const camera *cam, float f, const float (*pos)[3],
                            const float (*uv)[2], int n, const raster_texture *tx)
{
    (void)w;
    float cy = cosf(cam->yaw * PI_F / 180), sy = sinf(cam->yaw * PI_F / 180);
    float cp = cosf(cam->pitch * PI_F / 180), sp = sinf(cam->pitch * PI_F / 180);
    vview pin[8], pout[16];
    for (int k = 0; k < n; k++) {
        float px = pos[k][0] - cam->x, py = pos[k][1] - cam->y, pz = pos[k][2] - cam->z;
        float x1 = px * cy - pz * sy, z1 = px * sy + pz * cy;
        vview v = { x1, py * cp - z1 * sp, py * sp + z1 * cp, uv[k][0], uv[k][1], 1, 1, 1, 1, 0 };
        view_roll(cam, &v.x, &v.y);
        pin[k] = v;
    }
    int m = clip_near(pin, n, pout);
    if (m < 3) return;
    raster_vertex rv[16];
    for (int k = 0; k < m; k++) {
        float iz = 1.0f / pout[k].z;
        rv[k].x = (float)t->w * 0.5f + pout[k].x * f * iz;
        rv[k].y = (float)t->h * 0.5f - pout[k].y * f * iz;
        rv[k].iw = iz; rv[k].u = pout[k].u; rv[k].v = pout[k].v;
        rv[k].r = rv[k].g = rv[k].b = 1.0f; rv[k].fog = 1.0f; rv[k].lum = 0.0f;
    }
    for (int k = 1; k + 1 < m; k++) raster_triangle(t, &rv[0], &rv[k], &rv[k + 1], tx, 0xff000000u);
}

/* The horizon (HGSIM 0x1008dff3), screen space, for both looks:
 *   - the colour above the horizon line (skycolor[0] in software, [4] in 3D) and below it ([2] / [5]), palette
 *     indices into the mission palette (the PALG slot for the time of day, mission.c);
 *   - a band of the horizon texture (horizontexture in software, hardwarehorizon in 3D: the SKYGND parser copies one
 *     or the other into the same slot) standing on the horizon line. Its height is DAT_1011fa58: 36, scaled to the
 *     screen by the view transform (0x100763fb / 0x1007602d) - 128 at 640 x 480 in the running original. U runs from
 *     1 - heading / 360 * 8 (0 when above 4) across `horizontile` units over the screen width, V 0.01 .. 0.99.
 *     In software its vertices carry intensity 0x80 (table row 16, the identity row).
 * Then, 3D only, the cloud layer (0x1008da61): a quad in world space around the eye, 80000 wide, its near edge
 * 500 / tan(45 - pitch) ahead and 500 above the eye, its far edge 40000 ahead and 100 below the eye. The skytexture
 * repeats every 40000 / skycolor[3] units (texel coordinates are taken relative to the eye, so the layer moves with
 * the viewer) and drifts along u and v by skyspeed / 46336 per 5 ms tick (0x1001fe62), wrapping at 256. */
static void draw_horizon(const world *w, raster_target *t, const camera *cam, float f)
{
    const skygnd *s = &w->sky;
    uint32_t above = 0xff8fb4d8u, below = 0xff8fb4d8u;
    int sw = w->software;
    if (s->found) {
        const uint8_t *a = w->pal[s->skycolor[sw ? 0 : 4] & 255], *b = w->pal[s->skycolor[sw ? 2 : 5] & 255];
        above = 0xff000000u | ((uint32_t)a[0] << 16) | ((uint32_t)a[1] << 8) | a[2];
        below = 0xff000000u | ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    }
    float hy = (float)t->h * 0.5f - tanf(cam->pitch * PI_F / 180) * f;   /* the horizon's screen row */
    float band = 128.0f * (float)t->h / 480.0f;
    int ht = sw ? w->sw_horizon_tex : w->horizon_tex;
    const raster_texture *tx = ht < 0 ? NULL : sw ? (w->texs[ht].xel ? &w->texs[ht].srt : NULL)
                                              : (w->texs[ht].px ? &w->texs[ht].rt : NULL);
    float heading = fmodf(cam->yaw, 360.0f);
    if (heading < 0) heading += 360.0f;
    float u0 = 1.0f - heading / 360.0f * 8.0f;
    if (u0 > 4.0f) u0 = 0.0f;
    float tile = s->horizontile > 0 ? s->horizontile : 1.0f;
    for (int y = 0; y < t->h; y++) {
        uint32_t *row = t->color + (size_t)y * t->w;
        float fy = (float)y + 0.5f;
        if (tx && fy >= hy - band && fy < hy) {
            float v = 0.01f + 0.98f * (fy - (hy - band)) / band;
            int ty = (int)(v * (float)tx->h);
            for (int x = 0; x < t->w; x++) {
                float u = u0 + tile * ((float)x + 0.5f) / (float)t->w;
                int tx_ = (int)floorf(u * (float)tx->w) % tx->w;
                if (tx_ < 0) tx_ += tx->w;
                uint32_t c;
                if (sw && tx->idx) {
                    uint8_t ix = tx->idx[(size_t)ty * tx->w + tx_];
                    const uint8_t *p = w->pal[w->luma ? w->luma[16 * 256 + ix] : ix];
                    c = ix == 0xff ? 0 : 0xff000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
                } else {
                    c = tx->texels[(size_t)ty * tx->w + tx_];
                }
                row[x] = (c >> 24) ? c : above;
            }
        } else {
            uint32_t c = fy < hy ? above : below;
            for (int x = 0; x < t->w; x++) row[x] = c;
        }
    }
    if (!sw && !w->no_sky && w->sky_tex >= 0 && w->texs[w->sky_tex].px && s->found) {   /* 0x1008dff3: + 0x38 */
        const raster_texture *st = &w->texs[w->sky_tex].rt;
        float h = cam->yaw * PI_F / 180.0f, sh = sinf(h), ch = cosf(h);
        float p = (45.0f + cam->pitch) * PI_F / 180.0f;   /* the engine's pitch is the port's negated */
        float near = 500.0f / tanf(p);
        float rel[4][3] = {
            { near * sh - 40000.0f * ch,     500.0f, near * ch + 40000.0f * sh },
            { near * sh + 40000.0f * ch,     500.0f, near * ch - 40000.0f * sh },
            { 40000.0f * sh + 40000.0f * ch, -100.0f, 40000.0f * ch - 40000.0f * sh },
            { 40000.0f * sh - 40000.0f * ch, -100.0f, 40000.0f * ch + 40000.0f * sh },
        };
        int rate = s->skycolor[3] > 0x100 ? 0x100 : s->skycolor[3];
        float period = 40000.0f / (float)(rate > 0 ? rate : 1);
        float scroll = fmodf(w->sky_ticks * s->skyspeed / 46336.0f, 256.0f);
        float pos[4][3], uv[4][2];
        for (int k = 0; k < 4; k++) {
            pos[k][0] = rel[k][0] + cam->x; pos[k][1] = rel[k][1] + cam->y; pos[k][2] = rel[k][2] + cam->z;
            uv[k][0] = rel[k][0] / period + scroll;
            uv[k][1] = rel[k][2] / period + scroll;
        }
        draw_world_poly(w, t, cam, f, (const float (*)[3])pos, (const float (*)[2])uv, 4, st);
    }
    for (size_t i = 0; i < (size_t)t->w * t->h; i++) t->depth[i] = 0.0f;
}

/* SOFTWARE look: a textured vertex's luma row (HGSIM 0x1004bad6, the I76DRAW path; DAT_1014a5e8 == 0).
 *   L = the ART record's LITE ambient (+0x14)
 *   + the LITE light: i = clamp(dot(normal, dir) / |dir| * 128, -127, 127) (0x1009250c), L += i * globalintensity / 128
 *     when that is > 0. A LITE direction of 999999 x3 turns it off (0x100c1152).
 *   + fog, when fogswitch is on (the software path clamps a negative fogswitch to 0, 0x1008ceab):
 *     L += 128 - visibility * 128 (0x1004b93a: 1 up to density Z, 0 at the far distance, the VIEW chunk's * 2.5)
 *   - or, when the LITE shadedist is set (it replaces fog): beyond it, L -= 128 - shadedist / z * globalintensity
 *   then L is clamped to 0..128 and the polygon's draw word scales it: row = L * 255 >> 10 for textured kinds
 *   (0x10026610: 0x2000 / 0x5000-0x7000 / 0xb000-0xe000), clamped to 4..31. z is the vertex's view depth (+8).
 * The sim hands the plugin row << 3 as the vertex intensity. I76DRAW scales it by 1/255, and its rasterizers index
 * their table with (1 - that) * 7936 (31 * 256, 8.8 fixed point), interpolated across the polygon; the sim hands the
 * table over upside down (0x1000103f), so table row 31 - index is the TBL row. Returns that index (the rasterizer flips
 * it). Point lights are not done yet. */
/* the fog's visibility at view depth z of a vertex at model height y (0x1004b93a): 1 up to density Z (near), 0 at
 * the far distance; then the height term, density X (H) and Y (m): at y <= 0 times m when H < 0, else by
 * f = H > 0 ? H / y : -y / H, times m below m, times f below 1 */
/* the map's draw without fog (world_set_nofog) */
static int g_nofog;

void world_set_nofog(int on) { g_nofog = on; }

static float fog_vis(const world *w, float z, float y)
{
    const skygnd *s = &w->sky;
    float H = s->density[0], mn = s->density[1], fnear = s->density[2];
    float ffar = (float)(w->m.view_distance > 0 ? w->m.view_distance : 50000) * 2.5f, c;
    if (fnear == 0.0f) c = 1.0f;
    else {
        if (z < fnear || ffar <= fnear) c = 1.0f;
        else if (ffar <= z) c = 0.0f;
        else c = 1.0f - (z - fnear) / (ffar - fnear);
        if (c > 1.0f) c = 1.0f;
        if (c < 0.0f) c = 0.0f;
    }
    if (H != 0.0f) {
        if (y <= 0.0f) { if (H < 0.0f) c *= mn; }
        else {
            float f = H > 0.0f ? H / y : -y / H;
            if (f < mn) c *= mn;
            else if (f < 1.0f) c *= f;
        }
    }
    return c;
}

/* the fog's eye factor (0x100b8120, per frame: 0x10118188): 1, or by the eye's height as the height term (there
 * f = H > 0 ? H / eye_y : -H * eye_y), times 128 in the software look (255 in 3D) */
static float fog_eye(const world *w, float eye_y, float scale)
{
    const skygnd *s = &w->sky;
    float H = s->density[0], mn = s->density[1], e = 1.0f;
    if (H != 0.0f) {
        if (eye_y <= 0.0f) { if (H < 0.0f) e = mn; }
        else {
            float f = H > 0.0f ? H / eye_y : -H * eye_y;
            if (f < mn) e = mn;
            else if (f < 1.0f) e = f;
        }
    }
    return e * scale;
}

static int software_L(const world *w, const instance *in, const wtb_vertex *v, float z, float eye_y,
                      const poly_lights *pl, const float *wp)
{
    const mission *m = &w->m;
    const skygnd *s = &w->sky;
    int gi = s->found ? s->globalintensity : 0x80;
    if (gi < 0) gi = 0;
    if (gi > 0x80) gi = 0x80;   /* 0x1004b249 */
    int L = m->has_lite ? m->lite_ambient : 0;
    if (m->has_lite && !(m->lite_dir[0] == 999999 && m->lite_dir[1] == 999999 && m->lite_dir[2] == 999999)) {
        float lx = (float)m->lite_dir[0], ly = (float)m->lite_dir[1], lz = (float)m->lite_dir[2];
        float len = sqrtf(lx * lx + ly * ly + lz * lz);
        if (len > 0) {
            float nx = v->nx * in->rc + v->nz * in->rs, nz = -v->nx * in->rs + v->nz * in->rc, ny = v->ny;
            int i = (int)((nx * lx + ny * ly + nz * lz) / len * 128.0f);
            if (i > 127) i = 127;
            if (i < -127) i = -127;
            int li = (int)((float)(i * gi) / 128.0f);
            if (li > 0) L += li;
        }
    }
    int shadedist = m->has_lite ? m->lite_shadedist : 0;
    if (shadedist < 1) {
        int fs = s->found && !g_nofog ? s->fogswitch : 0;
        if (fs > 0) {
            /* vertex +0x2b = (byte)ftol(vis(z, model y) x the eye factor); L += 0x80 - it (0x1004bad6) */
            int b = (int)(fog_vis(w, z, v->y) * fog_eye(w, eye_y, 128.0f)) & 0xff;
            L += 0x80 - b;
            if (L < 0) L = 0;
        }
    } else if ((float)shadedist < z) {
        L -= 0x80 - (int)((float)shadedist / z * (float)gi);
        if (L < 0) L = 0;
    }
    /* the point lights, each while the vertex is under 0x80 */
    for (int k = 0; pl && k < pl->n; k++) {
        if (L >= 0x80) break;
        int i = pl->near_[k] ? 0x80 : point_i(pl->d0[k], wp[0], wp[1], wp[2], v);
        if (i > 0) L += i;
    }
    if (L > 0x80) L = 0x80;
    return L;
}

static float software_luma(const world *w, const instance *in, const wtb_vertex *v, float z, float eye_y,
                           const poly_lights *pl, const float *wp)
{
    int L = software_L(w, in, v, z, eye_y, pl, wp);
    int row = L * 255 >> 10;
    if (row < 4) row = 4;
    if (row > 31) row = 31;
    /* The intensity's low three bits. Traced: the software branch of 0x100012c5 (0x1014a5e8 == 0) hands I76DRAW
     * row << 3 with them clear (the 3D branch sets them: << 3 | 7), and I76DRAW's mode-2 rasterizer (0x10002650)
     * rounds (1 - i / 255) * 7936 to nearest and takes the high byte - so row 18 gives index 13, table row 18. But the
     * original's frame showed mission 1's flat ground (row 18) at table row 19, which | 7 reproduces; the port keeps
     * | 7 (CALIBRATED against that frame; something between the two isn't traced yet - KNOWN_ISSUES). */
    float f = (float)(row << 3 | 7) / 255.0f;
    return (1.0f - f) * 31.0f;
}

void world_draw(world *w, raster_target *t, const camera *cam)
{
    float cy = cosf(cam->yaw * PI_F / 180), sy = sinf(cam->yaw * PI_F / 180);
    float cp = cosf(cam->pitch * PI_F / 180), sp = sinf(cam->pitch * PI_F / 180);
    float f = (float)t->w * 0.5f / tanf(cam->fov * 0.5f * PI_F / 180);
    /* the far distance: the VIEW chunk's distance * 2.5 (0x100c1152 -> eyepoint +0x40, e.g. 125000 in mission 1) */
    float far2 = (float)(w->m.view_distance > 0 ? w->m.view_distance : 50000) * 2.5f;
    t->pal = w->software ? (const uint8_t (*)[3])w->pal : NULL;
    t->luma = w->software ? w->luma : NULL;
    t->bilinear = !w->software && !w->no_filter;   /* hgsnd +0x3c: the render state's 0x200 (0x1006fae1) */
    int sw_look = w->software;
    /* fog (0x1004b93a, called from 0x1004bad6 with the vertex's view depth, +8): visibility 1 up to density Z, falling
     * linearly to 0 at the far distance, times the height terms (fog_vis, fog_eye); only with fogswitch on */
    const skygnd *sg = &w->sky;
    float fog_near = sg->density[2], fog_far = far2;
    int fog_on = sg->found && sg->fogswitch && fog_near > 0 && fog_far > fog_near && !g_nofog;
    t->fog_rgb = 0xff000000u | ((uint32_t)sg->fogcolor[0] << 16) | ((uint32_t)sg->fogcolor[1] << 8) | (uint32_t)sg->fogcolor[2];
    g_near = w->near_z > 0 ? w->near_z : NEAR_640;
    draw_horizon(w, t, cam, f);
    for (int i = 0; i < w->ninst; i++) {
        const instance *in = &w->inst[i];
        if (in->hidden) continue;
        g_near = in->close ? 8.0f : w->near_z > 0 ? w->near_z : NEAR_640;
        float dx = in->x - cam->x, dz = in->z - cam->z;
        if (!in->ground && dx * dx + dz * dz > far2 * far2) continue;
        /* detail level (0x10090750): the last level whose switch distance, times the view's detail scale, the
         * object's depth reaches. The depth is the object's bounding-box centre along the view direction (0x10078797:
         * (centre - eye) . forward); the scale is (focal length) / (160 * detail), detail 1 (high, 0x101244f8) or
         * 2 (0x100b8496: +0xb8 = half width * zoom / (detail * 160), half width * zoom being +0x94, the focal length). A first level with distance 0 never switches. */
        int mi = in->mesh;
        if (in->nlod > 1 && in->mesh >= 0 && in->lod_dist[0] != 0.0f) {
            const mesh *m0 = &w->meshes[in->mesh];
            float c[3] = {(m0->min[0] + m0->max[0]) * 0.5f, (m0->min[1] + m0->max[1]) * 0.5f,
                          (m0->min[2] + m0->max[2]) * 0.5f}, cw[3];
            if (in->has_m) {
                for (int a = 0; a < 3; a++) cw[a] = in->m[a * 3] * c[0] + in->m[a * 3 + 1] * c[1] + in->m[a * 3 + 2] * c[2];
            } else {
                cw[0] = c[0] * in->rc + c[2] * in->rs; cw[2] = -c[0] * in->rs + c[2] * in->rc; cw[1] = c[1];
            }
            float px = cw[0] + in->x - cam->x, py = cw[1] + in->y - cam->y, pz = cw[2] + in->z - cam->z;
            float depth = py * sp + (px * sy + pz * cy) * cp, scale = f / 160.0f;
            for (int k = 1; k < in->nlod; k++)
                if (depth >= scale * in->lod_dist[k]) mi = in->lod_mesh[k];
                else break;
        }
        if (mi < 0) continue;
        const mesh *ms = &w->meshes[mi];
        vview *vv = malloc((size_t)ms->nverts * sizeof *vv);
        float (*wpos)[3] = malloc((size_t)(ms->nverts ? ms->nverts : 1) * sizeof *wpos);
        /* effects: the models are flat quads (x, y) facing +z. Every frame the sim turns each visible effect (and
         * the other objects of render types 0x10 / 0x60) about y towards the eye (0x10037fcc: yaw =
         * atan2(eye x - x, eye z - z), 0x1006f4ee; pitch 0), so the quad's face points at the camera's position. (In
         * view mode 4 they get yaw 180, pitch -45 instead; the port has no such view.) */
        float irc = in->rc, irs = in->rs;
        if (in->fx) {
            float ex = cam->x - in->x, ez = cam->z - in->z, r = sqrtf(ex * ex + ez * ez);
            if (r > 0.0f) { irs = ex / r; irc = ez / r; } else { irs = 0.0f; irc = 1.0f; }
        }
        for (int j = 0; j < ms->nverts; j++) {
            const wtb_vertex *v = &ms->v[j];
            /* model -> world: yaw about y (or the full turn), then translate */
            float wx, wy, wz;
            if (in->has_m) {
                wx = in->m[0] * v->x + in->m[1] * v->y + in->m[2] * v->z + in->x;
                wy = in->m[3] * v->x + in->m[4] * v->y + in->m[5] * v->z + in->y;
                wz = in->m[6] * v->x + in->m[7] * v->y + in->m[8] * v->z + in->z;
            } else {
                wx = v->x * irc + v->z * irs + in->x;
                wz = -v->x * irs + v->z * irc + in->z;
                wy = v->y + in->y;
            }
            /* world -> view */
            float px = wx - cam->x, py = wy - cam->y, pz = wz - cam->z;
            float x1 = px * cy - pz * sy, z1 = px * sy + pz * cy;
            float y2 = py * cp - z1 * sp, z2 = py * sp + z1 * cp;
            view_roll(cam, &x1, &y2);
            vv[j].x = x1; vv[j].y = y2; vv[j].z = z2; vv[j].u = v->u; vv[j].v = v->v;
            wpos[j][0] = wx; wpos[j][1] = wy; wpos[j][2] = wz;
        }
        /* the point lights in the object's model space (0x1004b44a: M^T (light - the object's position)); they light
         * a polygon in the SOFTWARE look with the fog off (0x1014b9b0, SKYGND's fogswitch), in 3D every polygon but
         * the sequences' (0x1004bad6) */
        int plit = w->dyn_lights && w->nlight > 0 &&
                   (sw_look ? !(sg->found && sg->fogswitch > 0) : (w->d3d_grey && sg->found));
        float lm[WORLD_LIGHTS][3];
        for (int k = 0; plit && k < w->nlight; k++) {
            const float dx = w->light[w->light_order[k]].x - in->x, dy = w->light[w->light_order[k]].y - in->y,
                        dz = w->light[w->light_order[k]].z - in->z;
            if (in->has_m) {
                lm[k][0] = in->m[0] * dx + in->m[3] * dy + in->m[6] * dz;
                lm[k][1] = in->m[1] * dx + in->m[4] * dy + in->m[7] * dz;
                lm[k][2] = in->m[2] * dx + in->m[5] * dy + in->m[8] * dz;
            } else {
                lm[k][0] = irc * dx - irs * dz; lm[k][1] = dy; lm[k][2] = irs * dx + irc * dz;
            }
        }
        for (int j = 0; j < ms->npolys; j++) {
            const mesh_poly *q = &ms->p[j];
            vview pin[WTB_MAX_POLY], pout[WTB_MAX_POLY * 2];
            poly_lights pl;
            pl.n = 0;
            for (int k = 0; plit && !q->seq && k < w->nlight && pl.n < 8; k++) {
                /* 0x1004b7f3: a vertex within the light's box */
                const int L = w->light_order[k];
                const float r = w->light[L].radius;
                int in_box = 0;
                for (int e = 0; e < q->n && !in_box; e++) {
                    const float *pw = wpos[q->idx[e]];
                    in_box = pw[0] <= w->light[L].x + r && w->light[L].x - r <= pw[0] &&
                             pw[1] <= w->light[L].y + r && w->light[L].y - r <= pw[1] &&
                             pw[2] <= w->light[L].z + r && w->light[L].z - r <= pw[2];
                }
                if (!in_box) continue;
                const wtb_vertex *v0 = &ms->v[q->idx[0]];
                pl.d0[pl.n][0] = lm[k][0] - v0->x; pl.d0[pl.n][1] = lm[k][1] - v0->y; pl.d0[pl.n][2] = lm[k][2] - v0->z;
                pl.near_[pl.n] = fabsf(pl.d0[pl.n][0]) < 1e-7f && fabsf(pl.d0[pl.n][1]) < 1e-7f &&
                                 fabsf(pl.d0[pl.n][2]) < 1e-7f;
                pl.n++;
            }
            for (int k = 0; k < q->n; k++) {
                const wtb_vertex *mv = &ms->v[q->idx[k]];
                pin[k] = vv[q->idx[k]];
                if (q->seq) {
                    /* an animated sequence's polygon (MLT type 3, draw kind 0x3000): the shading step passes its
                     * word through unlit (0x10026610), and it is drawn by 0x1008700c: the vertex colour the view's
                     * tint (0x10118198: white, green in night vision), intensity 0x80 in software (table row 16,
                     * the identity row) and 0xff in 3D, no fog (+0x18 = 0xff000000), each u / v put at 0.01 or
                     * 0.99 by which side of 0.5 it is (so a frame is drawn whole). Its vertex alpha byte is 200 (+0x17),
                     * but DX5DRAW never sets the texture blend (render state 0x15), so Direct3D 5's default,
                     * MODULATE, takes the alpha from the texture when it has one - and every texture is a 4444
                     * surface there (0x10005690): the vertex's 200 has no effect, and the texel's alpha blends as
                     * for any polygon (render mode 8). */
                    pin[k].lr = pin[k].lg = pin[k].lb = 1.0f;
                    pin[k].lum = (1.0f - 128.0f / 255.0f) * 31.0f;
                    pin[k].u = pin[k].u <= 0.5f ? 0.01f : 0.99f;
                    pin[k].v = pin[k].v <= 0.5f ? 0.01f : 0.99f;
                    pin[k].f = 1.0f;
                    continue;
                }
                light_vertex(w, in, mv, &pin[k]);
                if (!sw_look && w->d3d_grey && (q->tex >= 0 || q->anim >= 0) && w->sky.found)
                    pin[k].lr = pin[k].lg = pin[k].lb = d3d_shade(w, in, mv, 255, &pl, wpos[q->idx[k]]);
                pin[k].lum = sw_look ? software_luma(w, in, mv, pin[k].z, cam->y, &pl, wpos[q->idx[k]]) : 0.0f;
                if (fog_on) {
                    /* +0x2b = (byte)ftol(vis(z, model y) x the eye factor x 255) (0x1004bad6, 0x100b8120) */
                    pin[k].f = (float)((int)(fog_vis(w, pin[k].z, mv->y) * fog_eye(w, cam->y, 255.0f)) & 0xff) / 255.0f;
                } else {
                    pin[k].f = 1.0f;
                }
            }
            int m = clip_near(pin, q->n, pout);
            if (m < 3) continue;
            raster_vertex rv[WTB_MAX_POLY * 2];
            for (int k = 0; k < m; k++) {
                float iz = 1.0f / pout[k].z;
                rv[k].x = (float)t->w * 0.5f + pout[k].x * f * iz;
                rv[k].y = (float)t->h * 0.5f - pout[k].y * f * iz;
                rv[k].iw = iz; rv[k].u = pout[k].u; rv[k].v = pout[k].v;
                rv[k].r = pout[k].lr; rv[k].g = pout[k].lg; rv[k].b = pout[k].lb; rv[k].fog = pout[k].f;
                rv[k].lum = pout[k].lum;
            }
            int ti = q->tex;
            if (q->anim >= 0) {
                const world_anim *a = &w->anims[q->anim];
                /* An effect restarts its record's sequence at frame 0 when it starts (0x1001b3a1: 0x1000204c, then
                 * 0x10001ffe with its type's mode); the sequences step a frame every 0xb5 / rate ticks
                 * (0x10001725). Mode 1 loops; mode 2 makes one pass and then stops (mode 0), and a stopped
                 * sequence's polygons aren't drawn (0x100021e1 hands back no texture), so the effect vanishes. Other
                 * sequences loop by the clock (0x100bef4c starts them in mode 1), their start the clock at the first
                 * tick that sees them (0x10001725: +0x6a set when 0), i.e. the mission's start. All effects of a type
                 * share their record's sequence, so a new one restarts the others' too: every instance of the
                 * sequence shows the latest-started one's frame (`lead`). */
                const instance *src = in;
                if (in->fx && a->lead >= 0 && a->lead < w->ninst && w->inst[a->lead].fx && !w->inst[a->lead].hidden)
                    src = &w->inst[a->lead];
                long fr = (long)((in->fx ? src->fx_age : w->sky_ticks) / a->ticks_per_frame);
                if (in->fx && (src->fx_seq == 0 || (src->fx_seq == 2 && fr >= a->n))) continue;
                ti = a->tex[fr % a->n];
            }
            const raster_texture *tx = NULL;
            if (ti >= 0) {
                if (sw_look) {
                    /* No XEL: the software renderer draws nothing (0x100021e1 loads type "XEL" only). Patch 1.2
                     * retextured the effects (gearxpl1.mlt: FXBOOM, FXFIRE ... as TEX only, no XEL shipped; its
                     * .mlu / .tlu files are read by nothing in the binaries), so in the original's software look the
                     * patched effects aren't drawn, and neither are they here. */
                    tx = w->texs[ti].xel ? &w->texs[ti].srt : NULL;
                    if (!tx) continue;
                } else {
                    tx = w->texs[ti].px ? &w->texs[ti].rt : NULL;
                }
            }
            uint32_t col = q->color;
            if (sw_look && !tx && q->word) {
                /* SOFTWARE look, a colour material (0x10026610): kind 0 is unlit, palette index +0x44 << 4 | +0x48;
                 * kinds 0x1000 / 0x4000 are lit by 0x1004bad6 with the word's low byte (+0x48 << 4) as the scale -
                 * row = L x it >> 10, 4..31 - and the first vertex's row picks the shade, ftol(row / 31 x 7 + 0.5):
                 * palette index +0x44 << 4 | shade, the polygon drawn flat in it (the fog is in L) */
                int ix = (q->word >> 8 & 15) << 4;
                if ((q->word & 0xf000) == 0) ix |= q->word >> 4 & 15;
                else {
                    const wtb_vertex *mv0 = &ms->v[q->idx[0]];
                    int row = software_L(w, in, mv0, vv[q->idx[0]].z, cam->y, &pl, wpos[q->idx[0]]) * (q->word & 0xff) >> 10;
                    if (row < 4) row = 4;
                    if (row > 31) row = 31;
                    ix |= (int)((float)row / 31.0f * 7.0f + 0.5f);
                }
                col = 0xff000000u | (uint32_t)w->pal[ix][0] << 16 | (uint32_t)w->pal[ix][1] << 8 | w->pal[ix][2];
                for (int k = 0; k < m; k++) { rv[k].r = rv[k].g = rv[k].b = 1.0f; rv[k].fog = 1.0f; }
            }
            for (int k = 1; k + 1 < m; k++) raster_triangle(t, &rv[0], &rv[k], &rv[k + 1], tx, col);
        }
        free(vv);
        free(wpos);
    }
}

int world_music(const world *w)
{
    /* the sim reads the number after the name's first five characters ("track") with %d */
    if (strlen(w->m.music) <= 5) return -1;
    return atoi(w->m.music + 5);
}

int world_add_object(world *w, const char *model, float x, float y, float z, float yaw)
{
    int mi = load_mesh(w, model);
    if (mi < 0) return -1;
    instance *ni = realloc(w->inst, (size_t)(w->ninst + 1) * sizeof *w->inst);
    if (!ni) return -1;
    w->inst = ni;
    instance *in = &w->inst[w->ninst];
    memset(in, 0, sizeof *in);
    in->mesh = mi;
    {
        static const float one[3] = {1, 1, 1};
        load_lods(w, in, model, one);
    }
    in->x = x; in->y = y; in->z = z;
    in->rs = sinf(yaw * PI_F / 180.0f); in->rc = cosf(yaw * PI_F / 180.0f);
    return w->ninst++;
}

void world_move_object(world *w, int id, float x, float y, float z, float yaw)
{
    if (id < 0 || id >= w->ninst) return;
    instance *in = &w->inst[id];
    in->x = x; in->y = y; in->z = z;
    in->rs = sinf(yaw * PI_F / 180.0f); in->rc = cosf(yaw * PI_F / 180.0f);
}

int world_add_effect(world *w, const char *model)
{
    int id = world_add_object(w, model, 0, 0, 0, 0);
    if (id < 0) return -1;
    w->inst[id].fx = 1;
    w->inst[id].hidden = 1;
    return id;
}

void world_set_effect(world *w, int id, int on, float x, float y, float z, float age_ticks, int seq)
{
    if (id < 0 || id >= w->ninst) return;
    instance *in = &w->inst[id];
    if (on && (in->hidden || age_ticks < in->fx_age)) {
        /* a (re)start: its sequences restart for every effect sharing them (0x1000204c) */
        const mesh *ms = in->mesh >= 0 && in->mesh < w->nmeshes ? &w->meshes[in->mesh] : NULL;
        for (int k = 0; ms && k < ms->npolys; k++)
            if (ms->p[k].anim >= 0) w->anims[ms->p[k].anim].lead = id;
    }
    in->hidden = !on;
    in->x = x; in->y = y; in->z = z;
    in->fx_age = age_ticks;
    in->fx_seq = seq;
}

void world_set_object_matrix(world *w, int id, const float m[9])
{
    if (id < 0 || id >= w->ninst) return;
    instance *in = &w->inst[id];
    for (int k = 0; k < 9; k++) in->m[k] = m[k];
    in->has_m = 1;
    in->fx = 0;   /* a solid object: lit, not turned to the view */
}

void world_show_object(world *w, int id, int on)
{
    if (id < 0 || id >= w->ninst) return;
    w->inst[id].hidden = !on;
}

const mission *world_mission(const world *w) { return &w->m; }

int world_object_bounds(const world *w, int id, float mn[3], float mx[3])
{
    if (id < 0 || id >= w->ninst) return -1;
    const mesh *ms = &w->meshes[w->inst[id].mesh];
    for (int k = 0; k < 3; k++) { mn[k] = ms->min[k]; mx[k] = ms->max[k]; }
    return 0;
}

static float mesh_radius(const world *w, int mi, float centre[3])
{
    const mesh *ms = &w->meshes[mi];
    float c[3], r2 = 0;
    for (int k = 0; k < 3; k++) c[k] = (ms->min[k] + ms->max[k]) * 0.5f;
    for (int j = 0; j < ms->nverts; j++) {
        float dx = ms->v[j].x - c[0], dy = ms->v[j].y - c[1], dz = ms->v[j].z - c[2], d = dx * dx + dy * dy + dz * dz;
        if (d > r2) r2 = d;
    }
    float r = sqrtf(r2);
    if (ms->name[0] == 'w' || ms->name[0] == 'W') r /= 2.0f;
    else {
        char up[sizeof ms->name];
        size_t k = 0;
        for (; ms->name[k] && k + 1 < sizeof up; k++) up[k] = (char)toupper((unsigned char)ms->name[k]);
        up[k] = 0;
        if (strstr(up, "ENBP")) r /= 1.5f;
    }
    if (centre) for (int k = 0; k < 3; k++) centre[k] = c[k];
    return r;
}

float world_object_radius(const world *w, int id, float centre[3])
{
    if (id < 0 || id >= w->ninst || w->inst[id].mesh < 0) return 0;
    return mesh_radius(w, w->inst[id].mesh, centre);
}

const raster_texture *world_texture(world *w, const char *name)
{
    int i = load_texture(w, name);
    return i >= 0 && w->texs[i].rt.texels ? &w->texs[i].rt : NULL;
}

float world_far(const world *w)
{
    return (float)(w->m.view_distance > 0 ? w->m.view_distance : 50000) * 2.5f;
}

/* Shots in flight are objects (HGSIM 0x100c1152's HGBT branch, 0x100bb425, 0x100bd1e7, 0x1001aec9). The HGBT chunks
 * (gearsht1, in every mission's INCL tree) give a shot kind (+2, the weapon's +0x10: 0x38 .. 0x48) its model (the
 * name 29 bytes from the chunk's end: wstracer, wsbtracr, wssshell ... wshbazok) and a list of OBJ indices: a pool of
 * that many objects (40 tracers, 20 of the others) - a weapon whose kind has no free one doesn't fire. A shot's
 * object is turned once as it is fired (0x100bd1e7 -> 0x1009556a order 1: Z(0) * Y(yaw) * X(-pitch), yaw =
 * atan2(dx, dz), pitch = asin(dy) in degrees) and only moved after (0x100951d7); a guided one is turned again every
 * tick along its motion (0x1001aec9: order 0, Z(0) * X(-pitch) * Y(yaw); a lofted one's (+0x86 bits 0x20 and 1) with
 * yaw 0). In matrices, quaternion a * b is Ra Rb. */
static void shot_models_load(world *w)
{
    w->shot_loaded = 1;
    size_t len;
    uint8_t *d = hga_load(w->a, "gearsht1", "BWD", &len, NULL);
    if (!d) return;
    static bwd_chunk ch[256];
    int n = bwd_chunks(d, len, ch, 256);
    for (int i = 0; i < n && i < 256; i++) {
        if (strcmp(ch[i].tag, "HGBT") || ch[i].size < 40) continue;
        const uint8_t *q = ch[i].data;
        int kind = (int16_t)(q[2] | q[3] << 8);
        int last = (int16_t)(q[0] | q[1] << 8), first = (int16_t)(q[6] | q[7] << 8);
        if (kind < 0 || kind >= WORLD_SHOT_KINDS) continue;
        snprintf(w->shot_model[kind], sizeof w->shot_model[kind], "%.16s", (const char *)q + ch[i].size - 29);
        w->shot_cap[kind] = last >= first ? last - first + 1 : 0;
    }
    free(d);
}

/* the radius of the kind's model (the mesh's +0x50, the sim's sphere: 0x10003326), 0 if it has none */
float world_shot_radius(world *w, int kind)
{
    if (!w->shot_loaded) shot_models_load(w);
    if (kind < 0 || kind >= WORLD_SHOT_KINDS || !w->shot_model[kind][0]) return 0;
    int mi = load_mesh(w, w->shot_model[kind]);
    return mi >= 0 ? mesh_radius(w, mi, NULL) : 0;
}

int world_shot_capacity(world *w, int kind)
{
    if (!w->shot_loaded) shot_models_load(w);
    return kind >= 0 && kind < WORLD_SHOT_KINDS && w->shot_model[kind][0] ? w->shot_cap[kind] : 0;
}

void world_shot_matrix(const float dir[3], int guided, float m[9])
{
    const float d2r = PI_F / 180.0f;
    float l = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    float dy = l > 0 ? dir[1] / l : 0;
    if (dy > 1) dy = 1;
    if (dy < -1) dy = -1;
    float yaw = atan2f(dir[0], dir[2]) / d2r, pitch = -asinf(dy) / d2r;
    float cx = cosf(pitch * d2r), sx = sinf(pitch * d2r), cy = cosf(yaw * d2r), sy = sinf(yaw * d2r);
    const float rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx}, ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
    const float *a = guided ? rx : ry, *b = guided ? ry : rx;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
}

void world_set_shots(world *w, const world_shot *s, int n)
{
    if (!w->shot_loaded) shot_models_load(w);
    int used[WORLD_SHOT_KINDS] = {0};
    for (int i = 0; i < n; i++) {
        int k = s[i].kind;
        if (k < 0 || k >= WORLD_SHOT_KINDS || !w->shot_model[k][0]) continue;
        if (used[k] == w->shot_npool[k]) {
            int id = world_add_object(w, w->shot_model[k], 0, 0, 0, 0);
            if (id < 0) { w->shot_model[k][0] = 0; continue; }
            int *np = realloc(w->shot_pool[k], (size_t)(w->shot_npool[k] + 1) * sizeof *np);
            if (!np) continue;
            w->shot_pool[k] = np;
            np[w->shot_npool[k]++] = id;
        }
        int id = w->shot_pool[k][used[k]++];
        instance *in = &w->inst[id];
        in->hidden = 0;
        in->x = s[i].x; in->y = s[i].y; in->z = s[i].z;
        for (int a = 0; a < 9; a++) in->m[a] = s[i].m[a];
        in->has_m = 1;
        in->rs = s[i].m[2]; in->rc = s[i].m[0];
        float l = sqrtf(in->rs * in->rs + in->rc * in->rc);
        if (l > 1e-6f) { in->rs /= l; in->rc /= l; } else { in->rs = 0; in->rc = 1; }
    }
    for (int k = 0; k < WORLD_SHOT_KINDS; k++)
        for (int j = used[k]; j < w->shot_npool[k]; j++) w->inst[w->shot_pool[k][j]].hidden = 1;
}

/* 0x100275db / 0x100274d8 / 0x10027234: the cover points - scene objects whose flags (the OBJ's +0x34, & 0xf0) are
 * 0xd0 (kind 1) or 0xe0 (kind 2), the first 200 in scene order; each one's point is the object's position plus the
 * mean of its model's points as stored (not turned) */
int world_cover_points(const world *w, float (*out)[3], int *kind, int max)
{
    int n = 0;
    for (int o = 0; o < w->m.nobj && n < max && n < 200; o++) {
        unsigned f = w->m.obj[o].flags & 0xf0;
        if (f != 0xd0 && f != 0xe0) continue;
        int id = w->obj_inst[o];
        if (id < 0 || id >= w->ninst) continue;
        const instance *in = &w->inst[id];
        const mesh *ms = &w->meshes[in->mesh];
        float c[3] = {0, 0, 0};
        for (int i = 0; i < ms->nverts; i++) { c[0] += ms->v[i].x; c[1] += ms->v[i].y; c[2] += ms->v[i].z; }
        float k = ms->nverts > 0 ? 1.0f / (float)ms->nverts : 0;
        out[n][0] = in->x + c[0] * k; out[n][1] = in->y + c[1] * k; out[n][2] = in->z + c[2] * k;
        if (kind) kind[n] = f == 0xd0 ? 1 : 2;
        n++;
    }
    return n;
}

int world_cover_boxes(const world *w, float (*box)[6], int max)
{
    int n = 0;
    for (int o = 0; o < w->m.nobj && n < max && n < 200; o++) {
        unsigned f = w->m.obj[o].flags & 0xf0;
        if (f != 0xd0 && f != 0xe0) continue;
        int id = w->obj_inst[o];
        if (id < 0 || id >= w->ninst) continue;
        const instance *in = &w->inst[id];
        const mesh *ms = &w->meshes[in->mesh];
        float *b = box[n];
        b[0] = b[2] = b[4] = 3.4e38f;
        b[1] = b[3] = b[5] = -3.4e38f;
        for (int i = 0; i < ms->nverts; i++) {
            float x = ms->v[i].x, y = ms->v[i].y, z = ms->v[i].z, q[3];
            if (in->has_m) {
                q[0] = in->m[0] * x + in->m[1] * y + in->m[2] * z;
                q[1] = in->m[3] * x + in->m[4] * y + in->m[5] * z;
                q[2] = in->m[6] * x + in->m[7] * y + in->m[8] * z;
            } else {
                q[0] = x * in->rc + z * in->rs; q[1] = y; q[2] = -x * in->rs + z * in->rc;
            }
            q[0] += in->x; q[1] += in->y; q[2] += in->z;
            for (int k = 0; k < 3; k++) {
                if (q[k] < b[2 * k]) b[2 * k] = q[k];
                if (q[k] > b[2 * k + 1]) b[2 * k + 1] = q[k];
            }
        }
        n++;
    }
    return n;
}

int world_has_shot_model(world *w, int kind)
{
    if (!w->shot_loaded) shot_models_load(w);
    return kind >= 0 && kind < WORLD_SHOT_KINDS && w->shot_model[kind][0];
}

void world_show_obj(world *w, int obj, int on)
{
    if (obj < 0 || obj >= w->m.nobj) return;
    w->m.obj[obj].hidden = !on;
    if (w->obj_inst[obj] >= 0) w->inst[w->obj_inst[obj]].hidden = !on;
    for (int i = obj + 1; i < w->m.nobj; i++)   /* children come after their parent */
        if (w->m.obj[i].parent == obj) world_show_obj(w, i, on);
}

int world_obj_instance(const world *w, int obj)
{
    return obj >= 0 && obj < w->m.nobj ? w->obj_inst[obj] : -1;
}

unsigned world_object_flags(const world *w, int id)
{
    if (id < 0 || id >= w->ninst) return 0;
    for (int o = 0; o < w->m.nobj; o++) if (w->obj_inst[o] == id) return w->m.obj[o].flags;
    return 0;
}

int world_object_kind(const world *w, int id)
{
    if (id < 0 || id >= w->ninst) return -1;
    for (int o = 0; o < w->m.nobj; o++) {
        if (w->obj_inst[o] != id) continue;
        const mission_obj *ob = &w->m.obj[o];
        int k = ob->kind;
        if (k < 0 || k > 8) k = 4;
        if (!strncmp(ob->model, "tl1m", 4)) k = 8;
        return k;
    }
    return -1;
}

void world_set_solid(world *w, int id, int on)
{
    if (id >= 0 && id < w->ninst) w->inst[id].solid = on;
}

/* Moller-Trumbore: the segment o + t d, t in [0, 1] */
static int ray_tri(const float o[3], const float d[3], const float *a, const float *b, const float *c, float *t)
{
    float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (fabsf(det) < 1e-9f) return 0;
    float inv = 1.0f / det, s[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
    float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0 || u > 1) return 0;
    float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (v < 0 || u + v > 1) return 0;
    float tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    if (tt < 0 || tt > 1) return 0;
    *t = tt;
    return 1;
}

int world_ray(const world *w, const float p0[3], const float p1[3], float *t)
{
    return world_ray_n(w, p0, p1, t, NULL);
}

int world_ray_n(const world *w, const float p0[3], const float p1[3], float *t, float *ny)
{
    int best = -1;
    float bt = 2, bny = 1;
    for (int i = 0; i < w->ninst; i++) {
        const instance *in = &w->inst[i];
        if (!in->solid || in->hidden || in->fx) continue;
        const mesh *ms = &w->meshes[in->mesh];
        /* a quick reject on the model's reach about its origin */
        float r = 0;
        for (int k = 0; k < 3; k++) r = fmaxf(r, fmaxf(fabsf(ms->min[k]), fabsf(ms->max[k])));
        r *= 1.7321f;
        float cx = 0, cz = 0, cy = 0;
        {
            /* the segment's closest approach to the origin */
            float d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            float u = l2 > 0 ? ((in->x - p0[0]) * d[0] + (in->y - p0[1]) * d[1] + (in->z - p0[2]) * d[2]) / l2 : 0;
            if (u < 0) u = 0;
            if (u > 1) u = 1;
            cx = p0[0] + d[0] * u - in->x; cy = p0[1] + d[1] * u - in->y; cz = p0[2] + d[2] * u - in->z;
        }
        if (cx * cx + cy * cy + cz * cz > r * r) continue;
        /* into the model's frame (the inverse of the yaw in world_draw) */
        float o[3], e[3];
        const float *q[2] = {p0, p1};
        float *out[2] = {o, e};
        for (int k = 0; k < 2; k++) {
            float dx = q[k][0] - in->x, dy = q[k][1] - in->y, dz = q[k][2] - in->z;
            if (in->has_m) {   /* the full turn's transpose */
                out[k][0] = in->m[0] * dx + in->m[3] * dy + in->m[6] * dz;
                out[k][1] = in->m[1] * dx + in->m[4] * dy + in->m[7] * dz;
                out[k][2] = in->m[2] * dx + in->m[5] * dy + in->m[8] * dz;
                continue;
            }
            out[k][0] = dx * in->rc - dz * in->rs;
            out[k][1] = dy;
            out[k][2] = dx * in->rs + dz * in->rc;
        }
        float d[3] = {e[0] - o[0], e[1] - o[1], e[2] - o[2]};
        for (int j = 0; j < ms->npolys; j++) {
            const mesh_poly *pq = &ms->p[j];
            for (int k = 1; k + 1 < pq->n; k++) {
                const wtb_vertex *a = &ms->v[pq->idx[0]], *b = &ms->v[pq->idx[k]], *c = &ms->v[pq->idx[k + 1]];
                float A[3] = {a->x, a->y, a->z}, B[3] = {b->x, b->y, b->z}, C[3] = {c->x, c->y, c->z}, tt;
                if (ray_tri(o, d, A, B, C, &tt) && tt < bt) {
                    bt = tt; best = i;
                    /* the struck polygon's normal's world y (0x100229d9 keeps it in 0x10130828) */
                    bny = in->has_m ? in->m[3] * pq->nx + in->m[4] * pq->ny + in->m[5] * pq->nz : pq->ny;
                }
            }
        }
    }
    if (best >= 0) { *t = bt; if (ny) *ny = bny; }
    return best;
}

int world_object_pose(const world *w, int id, float *x, float *y, float *z, float *s, float *c)
{
    if (id < 0 || id >= w->ninst) return -1;
    const instance *in = &w->inst[id];
    *x = in->x; *y = in->y; *z = in->z; *s = in->rs; *c = in->rc;
    return in->hidden;
}

int world_project(const camera *cam, int w, int h, float x, float y, float z, float *sx, float *sy)
{
    float f = (float)w * 0.5f / tanf(cam->fov * 0.5f * PI_F / 180);
    float cy = cosf(cam->yaw * PI_F / 180), syw = sinf(cam->yaw * PI_F / 180);
    float cp = cosf(cam->pitch * PI_F / 180), sp = sinf(cam->pitch * PI_F / 180);
    float px = x - cam->x, py = y - cam->y, pz = z - cam->z;
    float x1 = px * cy - pz * syw, z1 = px * syw + pz * cy;
    float y2 = py * cp - z1 * sp, z2 = py * sp + z1 * cp;
    view_roll(cam, &x1, &y2);
    *sx = (float)w * 0.5f + x1 * f / (z2 > 1e-3f ? z2 : 1e-3f);
    *sy = (float)h * 0.5f - y2 * f / (z2 > 1e-3f ? z2 : 1e-3f);
    if (z2 <= 1.0f) {   /* behind: push it off screen on its side */
        *sx = x1 >= 0 ? (float)w * 4 : -(float)w * 3;
        *sy = (float)h * 0.5f;
        return 0;
    }
    return *sx >= 0 && *sy >= 0 && *sx < (float)w && *sy < (float)h;
}

void world_set_dynamic_lights(world *w, int on)
{
    if (w) w->dyn_lights = on != 0;
}

int world_add_light(world *w, float x, float y, float z, int life)
{
    /* 0x1004b275: none with 0x10118194 off; 0x1004b03d takes a free record (the list's tail), age 0; 0x1004b2e1(slot,
     * 0xf, ...): the position, type 1, radius 20000.0 (0x469c4000), the life; 0x1004b7a6: on */
    if (!w || !w->dyn_lights || w->nlight >= WORLD_LIGHTS) return -1;
    int k = 0;
    while (k < WORLD_LIGHTS && w->light[k].on) k++;
    if (k == WORLD_LIGHTS) return -1;
    w->light[k].on = 1;
    w->light[k].x = x; w->light[k].y = y; w->light[k].z = z;
    w->light[k].radius = 20000.0f;
    w->light[k].life = life;
    w->light[k].age = 0;
    w->light_order[w->nlight++] = k;
    return k;
}

void world_lights_tick(world *w, int ticks)
{
    /* 0x1004b193, once a frame: each light whose life isn't -1 ages by the frame's ticks (0x10130770); past its life
     * it goes back to the pool (0x1004b2b3 -> 0x1004afd2) */
    if (!w) return;
    int n = 0;
    for (int i = 0; i < w->nlight; i++) {
        int k = w->light_order[i];
        if (w->light[k].life != -1 && w->light[k].life < (w->light[k].age += ticks)) { w->light[k].on = 0; continue; }
        w->light_order[n++] = k;
    }
    w->nlight = n;
}

void world_set_coloured_light(world *w, int on)
{
    if (w) w->d3d_grey = !on;
}

/* The target display (F9, window 11's draw 0x100ad8cb -> 0x10053140): the target's object alone, drawn again with the
 * main view's camera and viewport after the scene (its own render list, flushed by 0x100020d8 over the frame), in the
 * polygon-colour mode 4 of 0x10026610: every polygon one flat palette colour by its object. The port clears the depth
 * so the object draws whole over what is in front of it, as the later list does. */
void world_draw_flat(world *w, raster_target *t, const camera *cam, const int *ids, const unsigned char *colour, int n)
{
    float cy = cosf(cam->yaw * PI_F / 180), sy = sinf(cam->yaw * PI_F / 180);
    float cp = cosf(cam->pitch * PI_F / 180), sp = sinf(cam->pitch * PI_F / 180);
    float f = (float)t->w * 0.5f / tanf(cam->fov * 0.5f * PI_F / 180);
    g_near = w->near_z > 0 ? w->near_z : NEAR_640;
    for (int i = 0; i < t->w * t->h; i++) t->depth[i] = 0.0f;
    for (int i = 0; i < n; i++) {
        if (ids[i] < 0 || ids[i] >= w->ninst) continue;
        const instance *in = &w->inst[ids[i]];
        if (in->hidden || in->mesh < 0) continue;
        const uint8_t *pc = w->pal[colour[i]];
        uint32_t argb = 0xff000000u | ((uint32_t)pc[0] << 16) | ((uint32_t)pc[1] << 8) | pc[2];
        const mesh *ms = &w->meshes[in->mesh];
        vview *vv = malloc((size_t)ms->nverts * sizeof *vv);
        if (!vv) continue;
        for (int j = 0; j < ms->nverts; j++) {
            const wtb_vertex *v = &ms->v[j];
            float wx, wy, wz;
            if (in->has_m) {
                wx = in->m[0] * v->x + in->m[1] * v->y + in->m[2] * v->z + in->x;
                wy = in->m[3] * v->x + in->m[4] * v->y + in->m[5] * v->z + in->y;
                wz = in->m[6] * v->x + in->m[7] * v->y + in->m[8] * v->z + in->z;
            } else {
                wx = v->x * in->rc + v->z * in->rs + in->x;
                wz = -v->x * in->rs + v->z * in->rc + in->z;
                wy = v->y + in->y;
            }
            float px = wx - cam->x, py = wy - cam->y, pz = wz - cam->z;
            float x1 = px * cy - pz * sy, z1 = px * sy + pz * cy;
            memset(&vv[j], 0, sizeof vv[j]);
            vv[j].x = x1; vv[j].y = py * cp - z1 * sp; vv[j].z = py * sp + z1 * cp;
            vv[j].lr = vv[j].lg = vv[j].lb = vv[j].f = 1.0f;
        }
        for (int j = 0; j < ms->npolys; j++) {
            const mesh_poly *q = &ms->p[j];
            vview pin[WTB_MAX_POLY], pout[WTB_MAX_POLY * 2];
            for (int k = 0; k < q->n; k++) pin[k] = vv[q->idx[k]];
            int m = clip_near(pin, q->n, pout);
            if (m < 3) continue;
            raster_vertex rv[WTB_MAX_POLY * 2];
            for (int k = 0; k < m; k++) {
                float iz = 1.0f / pout[k].z;
                memset(&rv[k], 0, sizeof rv[k]);
                rv[k].x = (float)t->w * 0.5f + pout[k].x * f * iz;
                rv[k].y = (float)t->h * 0.5f - pout[k].y * f * iz;
                rv[k].iw = iz;
                rv[k].r = rv[k].g = rv[k].b = rv[k].fog = 1.0f;
                rv[k].lum = 15.0f;
            }
            for (int k = 1; k + 1 < m; k++) raster_triangle(t, &rv[0], &rv[k], &rv[k + 1], NULL, argb);
        }
        free(vv);
    }
}

void world_set_near(world *w, float near_z)
{
    w->near_z = near_z;
}

void world_set_close(world *w, int id, int on)
{
    if (id >= 0 && id < w->ninst) w->inst[id].close = on;
}
