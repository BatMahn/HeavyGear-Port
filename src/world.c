#include "world.h"

#include "skygnd.h"
#include "tex.h"
#include "wtb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265f

typedef struct {
    char           name[33];
    int            ok;
    raster_texture rt;
    uint32_t      *px;
    uint8_t       *xel;     /* the software renderer's texels (XEL), or NULL */
} tex_entry;

typedef struct {
    int      n;
    uint16_t idx[WTB_MAX_POLY];
    float    nx, ny, nz;
    int      tex;      /* tex_entry index or -1 */
    int      anim;     /* an animated material (MLT type 3): its world anim index, -1 none */
    uint32_t color;    /* when untextured */
} mesh_poly;

/* an MLT type 3 sequence: frames first..last, each shown 0xb5 / divisor ticks (0x100bef4c) */
typedef struct { char key[40]; int n; int tex[64]; float ticks_per_frame; } world_anim;

typedef struct {
    char        name[11];
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
    int   fx;          /* an effect (world_add_effect): turned to face the camera, unlit, its animation from fx_age */
    float fx_age;      /* ticks since the effect started */
    int   solid;       /* a mission object that stops shells (world_ray) */
    int   has_m;       /* a full turn (debris): m, rows of model -> world, instead of the yaw */
    float m[9];
} instance;

struct world {
    hg_archive *a;
    mission     m;
    mesh       *meshes;  int nmeshes;
    tex_entry  *texs;    int ntexs, missing;
    world_anim *anims;   int nanims;
    instance   *inst;    int ninst;
    int        *obj_inst;         /* mission obj slot -> instance, -1 none */
    float       gmin[3], gmax[3];
    skygnd      sky;
    uint8_t     pal[256][3];      /* PALG[0]'s COL palette, 8-bit */
    int         horizon_tex;      /* tex_entry index or -1 */
    int         sw_horizon_tex;   /* SOFTWARE look: horizontexture */
    int         sky_tex;          /* 3D look: skytexture (the cloud layer) */
    float       sky_ticks;        /* 5 ms ticks since the start, for the cloud layer's drift */
    uint8_t    *luma;             /* the mission's LTBL table (32 x 256), NULL if none */
    int         software;         /* 1: SOFTWARE look */
};

/* TEX -> ARGB. Alpha (INFERRED from the Titanium edition's dx5draw, 0x10006713, until HG's plugins are traced):
 * type 0 opaque; other types alpha = 255 - stored alpha. */
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
            uint32_t al = t.type == 0 ? 255u : 255u - p[3];
            e->px[i] = (al << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
        }
        e->rt.w = t.w; e->rt.h = t.h; e->rt.texels = e->px;
        e->ok = 1;
        tex_free(&t);
        /* the SOFTWARE look's copy: XEL of the same name, used when it has the TEX's size */
        size_t xl;
        uint8_t *xd = hga_load(w->a, name, "XEL", &xl, NULL);
        hg_texture x;
        if (xd && !tex_parse_xel(xd, xl, &x)) {
            if (x.w == e->rt.w && x.h == e->rt.h) { e->xel = x.idx; e->rt.idx = e->xel; }
            else tex_free(&x);
        }
        free(xd);
    } else {
        w->missing++;
    }
    free(d);
    w->ntexs++;
    return e->ok ? w->ntexs - 1 : -1;
}

static int load_mesh(world *w, const char *name)
{
    for (int i = 0; i < w->nmeshes; i++)
        if (!strcmp(w->meshes[i].name, name)) return w->meshes[i].ok ? i : -1;
    w->meshes = realloc(w->meshes, (size_t)(w->nmeshes + 1) * sizeof *w->meshes);
    mesh *ms = &w->meshes[w->nmeshes++];
    memset(ms, 0, sizeof *ms);
    snprintf(ms->name, sizeof ms->name, "%s", name);
    size_t len;
    uint8_t *d = hga_load(w->a, name, "WTB", &len, NULL);
    wtb_model m;
    if (!d || wtb_parse(d, len, &m)) { free(d); return -1; }
    free(d);
    /* all objects of the record merged (multi-object records: INFERRED that they draw together) */
    int nv = 0, np = 0;
    for (int i = 0; i < m.count; i++) { nv += m.obj[i].nverts; np += m.obj[i].npolys; }
    ms->v = malloc((size_t)(nv ? nv : 1) * sizeof *ms->v);
    ms->p = malloc((size_t)(np ? np : 1) * sizeof *ms->p);
    for (int k = 0; k < 3; k++) { ms->min[k] = 1e30f; ms->max[k] = -1e30f; }
    int vb = 0;
    for (int i = 0; i < m.count; i++) {
        wtb_object *o = &m.obj[i];
        for (int j = 0; j < o->nverts; j++) {
            wtb_vertex *v = &ms->v[ms->nverts++];
            *v = o->v[j];
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
            q->color = 0xff808080u;
            /* label (upper-cased, 0x100ea690) -> TLT -> key -> MLT[key] -> texture = MLT name (+0x20, 0x100bef4c).
             * MLT types 0, 1, 4 are untextured (colour); 3 is an animated sequence (first frame drawn for now). */
            const tlt_entry *te = o->legacy ? NULL : material_find_label(&w->m.mat, g->label);
            const mlt_entry *me = te ? material_find_texture(&w->m.mat, te->texture) : NULL;
            if (me) {
                q->color = 0xff000000u | ((uint32_t)me->rgb[0] << 16) | ((uint32_t)me->rgb[1] << 8) | me->rgb[2];
                if (me->type != 0 && me->type != 1 && me->type != 4) {
                    char nm[40];
                    if (me->type == 3) snprintf(nm, sizeof nm, "%s%s%u", me->name, me->p4c < 10 ? "0" : "", me->p4c);
                    else snprintf(nm, sizeof nm, "%s", me->name);
                    q->tex = load_texture(w, nm);
                    if (me->type == 3 && me->p50 > me->p4c) q->anim = load_anim(w, me);
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
    for (uint32_t f = me->p4c; f <= me->p50 && a->n < 64; f++) {
        char nm[40];
        snprintf(nm, sizeof nm, "%s%s%u", me->name, f < 10 ? "0" : "", f);
        a->tex[a->n++] = load_texture(w, nm);
    }
    a->ticks_per_frame = me->p54 ? (float)0xb5 / (float)me->p54 : (float)0xb5;
    return w->nanims++;
}

/* height of the ground at (x, z): the highest ground polygon over the point */
float world_ground_height(const world *w, float x, float z, int *found)
{
    float best = -1e30f;
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
                if (h > best) best = h;
            }
        }
    }
    if (found) *found = best > -1e29f;
    return best > -1e29f ? best : 0.0f;
}

/* Objects named o...: their SDF record (HGSIM 0x100c1152, the "SDF" lookup at 0x10122d50) lists up to 10 model names at
 * +0x0c + n*0x20. For o_ objects these are detail levels with a float switch distance each at +0x14c + n*4; for oc
 * objects (second letter 'c', special-cased by the loader) they are the collapse animation's frames. */
static void load_lods(world *w, instance *in, const char *model)
{
    in->nlod = 1;
    in->lod_mesh[0] = in->mesh;
    in->lod_dist[0] = 0;
    if (model[0] != 'o' || model[1] == 'c') return;
    size_t len;
    uint8_t *d = hga_load(w->a, model, "SDF", &len, NULL);
    if (!d || len < 0x14c + 4 * MAX_LOD) { free(d); return; }
    for (int n = 1; n < MAX_LOD; n++) {
        char nm[33];
        memcpy(nm, d + 0x0c + n * 0x20, 32);
        nm[32] = 0;
        if (!nm[0]) break;
        for (char *c = nm; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        int mi = load_mesh(w, nm);
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

/* INFERRED from the names and the SDF loader: oa... (remains) and oc... (collapse frames) only show once the object is
 * destroyed; the o_ object is the standing one. */
static int destroyed_state(const char *model)
{
    return model[0] == 'o' && (model[1] == 'a' || model[1] == 'c');
}

world *world_load(hg_archive *a, const char *mission_name, char *err, size_t errlen)
{
    world *w = calloc(1, sizeof *w);
    w->a = a;
    if (mission_load(&w->m, a, mission_name, err, errlen)) { free(w); return NULL; }
    for (int k = 0; k < 3; k++) { w->gmin[k] = 1e30f; w->gmax[k] = -1e30f; }
    w->inst = calloc((size_t)(w->m.nobj ? w->m.nobj : 1), sizeof *w->inst);
    w->obj_inst = malloc((size_t)(w->m.nobj ? w->m.nobj : 1) * sizeof *w->obj_inst);
    for (int i = 0; i < w->m.nobj; i++) w->obj_inst[i] = -1;
    for (int pass = 0; pass < 2; pass++) {   /* ground first, so objects can be dropped onto it */
        for (int i = 0; i < w->m.nobj; i++) {
            const mission_obj *o = &w->m.obj[i];
            if (o->ground != (pass == 0)) continue;
            /* the 'oc' collapse frames aren't drawn; the 'oa' (destroyed) models wait hidden for their structure */
            if (destroyed_state(o->model) && o->model[1] == 'c') continue;
            int mi = load_mesh(w, o->model);
            if (mi < 0) continue;
            w->obj_inst[i] = w->ninst;
            instance *in = &w->inst[w->ninst++];
            in->mesh = mi;
            in->hidden = destroyed_state(o->model);
            load_lods(w, in, o->model);
            in->x = o->x; in->y = o->y; in->z = o->z;
            in->ground = o->ground;
            in->solid = !o->ground;
            float yaw = o->rot[1] * PI_F / 180.0f;
            in->rs = sinf(yaw); in->rc = cosf(yaw);
            if (o->ground) {
                mesh *ms = &w->meshes[mi];
                for (int k = 0; k < 3; k++) {
                    if (ms->min[k] < w->gmin[k]) w->gmin[k] = ms->min[k];
                    if (ms->max[k] > w->gmax[k]) w->gmax[k] = ms->max[k];
                }
            } else if (o->child && o->parent >= 0) {
                /* a child follows its parent, which was stood on the ground before it */
                int f;
                const mission_obj *p = &w->m.obj[o->parent];
                float h = world_ground_height(w, p->x, p->z, &f);
                if (f && p->in_block) in->y = h + p->oy + (o->y - p->y);
            } else if (o->in_block) {
                /* INFERRED: block objects stand on the ground; their own y is an offset from it */
                int f;
                float h = world_ground_height(w, in->x, in->z, &f);
                if (f) in->y = h + o->oy;
            }
        }
    }
    /* palette: the ART record's PALG lists the palettes; the first is the mission's normal one (INFERRED) */
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
    cam->pitch = 0; cam->fov = 90;
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
    cam->yaw = 0; cam->pitch = -5; cam->fov = 75;
}

void world_free(world *w)
{
    if (!w) return;
    for (int i = 0; i < w->nmeshes; i++) { free(w->meshes[i].v); free(w->meshes[i].p); }
    for (int i = 0; i < w->ntexs; i++) { free(w->texs[i].px); free(w->texs[i].xel); }
    free(w->luma);
    free(w->meshes); free(w->texs); free(w->inst); free(w->obj_inst); free(w->anims);
    mission_free(&w->m);
    free(w);
}

/* ---- drawing ---- */

typedef struct { float x, y, z, u, v, lr, lg, lb, f, lum; } vview;   /* view space + light + fog + luma row */

#define NEAR_Z 20.0f

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
 *     indices into the mission palette (which palette: INFERRED, PALG's first);
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
    const raster_texture *tx = ht >= 0 ? &w->texs[ht].rt : NULL;
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
    if (!sw && w->sky_tex >= 0 && s->found) {
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
static float software_luma(const world *w, const instance *in, const wtb_vertex *v, float z)
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
        int fs = s->found ? s->fogswitch : 0;
        if (fs > 0) {
            float fnear = s->density[2], ffar = (float)(m->view_distance > 0 ? m->view_distance : 50000) * 2.5f, vis;
            if (fnear == 0.0f || z < fnear || ffar <= fnear) vis = 1.0f;
            else if (z >= ffar) vis = 0.0f;
            else vis = 1.0f - (z - fnear) / (ffar - fnear);
            L += 0x80 - (int)(vis * 128.0f);
            if (L < 0) L = 0;
        }
    } else if ((float)shadedist < z) {
        L -= 0x80 - (int)((float)shadedist / z * (float)gi);
        if (L < 0) L = 0;
    }
    if (L > 0x80) L = 0x80;
    int row = L * 255 >> 10;
    if (row < 4) row = 4;
    if (row > 31) row = 31;
    /* The intensity's low three bits: set as in the hardware path (0x100012c5: << 3 | 7). INFERRED for this path; with
     * them mission 1's flat ground (row 18) reaches table row 19, as on the original's frame. */
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
    t->bilinear = !w->software;
    int sw_look = w->software;
    /* fog (0x1004b93a, called from 0x1004bad6 with the vertex's view depth, +8): visibility 1 up to density Z, falling
     * linearly to 0 at the far distance; only with fogswitch on. (The density Y height term is not done yet.) */
    const skygnd *sg = &w->sky;
    float fog_near = sg->density[2], fog_far = far2;
    int fog_on = sg->found && sg->fogswitch && fog_near > 0 && fog_far > fog_near;
    t->fog_rgb = 0xff000000u | ((uint32_t)sg->fogcolor[0] << 16) | ((uint32_t)sg->fogcolor[1] << 8) | (uint32_t)sg->fogcolor[2];
    draw_horizon(w, t, cam, f);
    for (int i = 0; i < w->ninst; i++) {
        const instance *in = &w->inst[i];
        if (in->hidden) continue;
        float dx = in->x - cam->x, dz = in->z - cam->z;
        if (!in->ground && dx * dx + dz * dz > far2 * far2) continue;
        /* detail level: the last whose switch distance the camera is beyond (distance measure INFERRED: ground plane) */
        int mi = in->mesh;
        float dist = sqrtf(dx * dx + dz * dz);
        for (int k = 1; k < in->nlod; k++)
            if (dist >= in->lod_dist[k]) mi = in->lod_mesh[k];
        if (mi < 0) continue;
        const mesh *ms = &w->meshes[mi];
        vview *vv = malloc((size_t)ms->nverts * sizeof *vv);
        /* effects: the models are flat quads (x, y) - turned about y to face the view (INFERRED: how the original
         * keeps them facing the camera isn't traced) */
        float irc = in->fx ? cy : in->rc, irs = in->fx ? sy : in->rs;
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
            vv[j].x = x1; vv[j].y = y2; vv[j].z = z2; vv[j].u = v->u; vv[j].v = v->v;
        }
        for (int j = 0; j < ms->npolys; j++) {
            const mesh_poly *q = &ms->p[j];
            vview pin[WTB_MAX_POLY], pout[WTB_MAX_POLY * 2];
            for (int k = 0; k < q->n; k++) {
                const wtb_vertex *mv = &ms->v[q->idx[k]];
                pin[k] = vv[q->idx[k]];
                if (in->fx) {   /* full bright (INFERRED) */
                    pin[k].lr = pin[k].lg = pin[k].lb = 1.0f;
                    pin[k].lum = 0.0f;
                } else {
                    light_vertex(w, in, mv, &pin[k]);
                    pin[k].lum = sw_look ? software_luma(w, in, mv, pin[k].z) : 0.0f;
                }
                if (fog_on) {
                    float dd = pin[k].z;
                    pin[k].f = dd <= fog_near ? 1.0f : dd >= fog_far ? 0.0f : 1.0f - (dd - fog_near) / (fog_far - fog_near);
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
                /* an effect's sequence from its start (INFERRED: its life is one pass, e.g. EXPLSION's 14 frames
                 * over the explosion's 181 ticks); others by the clock */
                long fr = (long)((in->fx ? in->fx_age : w->sky_ticks) / a->ticks_per_frame);
                ti = a->tex[fr % a->n];
            }
            const raster_texture *tx = ti >= 0 ? &w->texs[ti].rt : NULL;
            for (int k = 1; k + 1 < m; k++) raster_triangle(t, &rv[0], &rv[k], &rv[k + 1], tx, q->color);
        }
        free(vv);
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
    load_lods(w, in, model);
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

void world_set_effect(world *w, int id, int on, float x, float y, float z, float age_ticks)
{
    if (id < 0 || id >= w->ninst) return;
    instance *in = &w->inst[id];
    in->hidden = !on;
    in->x = x; in->y = y; in->z = z;
    in->fx_age = age_ticks;
}

void world_turn_object(world *w, int id, float yaw, float pitch, float roll)
{
    if (id < 0 || id >= w->ninst) return;
    instance *in = &w->inst[id];
    /* yaw about y, pitch about x, roll about z: R = Ry * Rx * Rz (INFERRED order) */
    float a = yaw * PI_F / 180.0f, b = pitch * PI_F / 180.0f, c = roll * PI_F / 180.0f;
    float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b), cc = cosf(c), sc = sinf(c);
    float ry[9] = {ca, 0, sa, 0, 1, 0, -sa, 0, ca};
    float rx[9] = {1, 0, 0, 0, cb, -sb, 0, sb, cb};
    float rz[9] = {cc, -sc, 0, sc, cc, 0, 0, 0, 1};
    float t[9];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        t[i * 3 + j] = 0;
        for (int k = 0; k < 3; k++) t[i * 3 + j] += ry[i * 3 + k] * rx[k * 3 + j];
    }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        in->m[i * 3 + j] = 0;
        for (int k = 0; k < 3; k++) in->m[i * 3 + j] += t[i * 3 + k] * rz[k * 3 + j];
    }
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

/* Shots in flight: a short streak from each point back along its motion, depth-tested (INFERRED look: the original
 * draws them through its effects code, 0x10084362 / 0x10082556, not traced) */
void world_draw_streaks(const world *w, raster_target *t, const camera *cam, const float (*p)[6], int n, uint32_t rgb)
{
    (void)w;
    float f = (float)t->w * 0.5f / tanf(cam->fov * 0.5f * PI_F / 180);
    float cy = cosf(cam->yaw * PI_F / 180), sy = sinf(cam->yaw * PI_F / 180);
    float cp = cosf(cam->pitch * PI_F / 180), sp = sinf(cam->pitch * PI_F / 180);
    for (int i = 0; i < n; i++) {
        float sx[2], sy2[2], iz[2];
        int ok = 1;
        for (int e = 0; e < 2; e++) {
            float px = p[i][e * 3] - cam->x, py = p[i][e * 3 + 1] - cam->y, pz = p[i][e * 3 + 2] - cam->z;
            float x1 = px * cy - pz * sy, z1 = px * sy + pz * cy;
            float y2 = py * cp - z1 * sp, z2 = py * sp + z1 * cp;
            if (z2 < 50.0f) { ok = 0; break; }
            iz[e] = 1.0f / z2;
            sx[e] = (float)t->w * 0.5f + x1 * f * iz[e];
            sy2[e] = (float)t->h * 0.5f - y2 * f * iz[e];
        }
        if (!ok) continue;
        float dx = sx[1] - sx[0], dy = sy2[1] - sy2[0];
        int steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
        if (steps > 400) steps = 400;
        for (int s = 0; s <= steps; s++) {
            float u = (float)s / (float)steps;
            int x = (int)(sx[0] + dx * u), y = (int)(sy2[0] + dy * u);
            float z = iz[0] + (iz[1] - iz[0]) * u;
            int r = s == 0 ? 1 : 0;   /* the head as a 3 x 3 dot */
            for (int yy = y - r; yy <= y + 1 + r; yy++)
                for (int xx = x - r; xx <= x + r; xx++) {
                    if (xx < 0 || xx >= t->w || yy < 0 || yy >= t->h) continue;
                    size_t o = (size_t)yy * (size_t)t->w + (size_t)xx;
                    if (t->depth[o] > z) continue;
                    t->color[o] = 0xff000000u | rgb;
                }
        }
    }
}

int world_obj_instance(const world *w, int obj)
{
    return obj >= 0 && obj < w->m.nobj ? w->obj_inst[obj] : -1;
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
    int best = -1;
    float bt = 2;
    for (int i = 0; i < w->ninst; i++) {
        const instance *in = &w->inst[i];
        if (!in->solid || in->hidden || in->fx || in->has_m) continue;
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
            float dx = q[k][0] - in->x, dz = q[k][2] - in->z;
            out[k][0] = dx * in->rc - dz * in->rs;
            out[k][1] = q[k][1] - in->y;
            out[k][2] = dx * in->rs + dz * in->rc;
        }
        float d[3] = {e[0] - o[0], e[1] - o[1], e[2] - o[2]};
        for (int j = 0; j < ms->npolys; j++) {
            const mesh_poly *pq = &ms->p[j];
            for (int k = 1; k + 1 < pq->n; k++) {
                const wtb_vertex *a = &ms->v[pq->idx[0]], *b = &ms->v[pq->idx[k]], *c = &ms->v[pq->idx[k + 1]];
                float A[3] = {a->x, a->y, a->z}, B[3] = {b->x, b->y, b->z}, C[3] = {c->x, c->y, c->z}, tt;
                if (ray_tri(o, d, A, B, C, &tt) && tt < bt) { bt = tt; best = i; }
            }
        }
    }
    if (best >= 0) *t = bt;
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
    *sx = (float)w * 0.5f + x1 * f / (z2 > 1e-3f ? z2 : 1e-3f);
    *sy = (float)h * 0.5f - y2 * f / (z2 > 1e-3f ? z2 : 1e-3f);
    if (z2 <= 1.0f) {   /* behind: push it off screen on its side */
        *sx = x1 >= 0 ? (float)w * 4 : -(float)w * 3;
        *sy = (float)h * 0.5f;
        return 0;
    }
    return *sx >= 0 && *sy >= 0 && *sx < (float)w && *sy < (float)h;
}
