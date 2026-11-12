#include "shell.h"

#include "modelview.h"

#include "brf.h"
#include "pcx.h"
#include "shell_picture.h"
#include "shell_tables.h"
#include "shp.h"
#include "smk.h"
#include "fsutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int      rec;           /* picture table index */
    int      x, y;          /* absolute top-left (640 x 480) */
    int      w, h;          /* the record's rectangle (0 x 0: the art's own size) */
    int      kind;
    char     stem[9];       /* button: the name without its state letter */
    int      is_pcx, loaded;
    hg_pcx   pcx;
    hg_shape shp;
} element;

/* one screen's (or popup's) elements: every record whose parent chain leads to the root, at its absolute place; a
 * popup's records sit relative to its placement record (B19LOCXY, B72_LOC_: FUN_10028f08's rectangle) */
typedef struct {
    int      root;
    element *el;
    int      nel;
} layer;

/* a list (FUN_10002f9f): the list's picture (B20CLIST), its inner area (*INNR), the selection bar (*SLCT) and its
 * up / down buttons; the selected item sits in the bar, the others above and below it */
#define SHELL_MAX_LISTS 4
typedef struct {
    int    rec;
    char   up[9], down[9];
    int    ix0, iy0, ix1, iy1;      /* inner, absolute */
    int    bx0, by0, bx1, by1;      /* selection bar, absolute */
    char **item;
    int    n, sel;
} shell_list;

/* the briefing map: the base map with the north and south maps above and below the campaign's front line */
typedef struct {
    int      loaded, ok;
    hg_pcx   base, north, south;
    uint8_t *px;            /* the composite, base.w x base.h */
    int      front;         /* the campaign entry the composite was made for */
} front_map;

/* one Smacker animation of the screen */
typedef struct { smk *m; int x, y, mode, state, frame, shown; double next; uint64_t paused_at; char name[9]; } shell_anim;

struct shell {
    hg_archive *a;
    hg_font     font[SHELL_FONTS];
    int         font_tried[SHELL_FONTS];
    int         mission;    /* campaign table entry */
    hg_brf      brf;
    int         has_brf;
    int         outcome;        /* the last mission's: 2 accomplished, 1 failed, -1 none */
    front_map   map5, map62;    /* B05's (B05_BASE ...) and the situation screen's (B62_BASE ...) */
    hg_shape    mapov;          /* B05MAPOV, the map's frame */
    int         mapov_tried;
    layer       lay[2];         /* the screen and a popup over it */
    int         nlay;
    shell_list  list[SHELL_MAX_LISTS];
    int         nlist;
    struct { int on, x0, y0, x1, y1, max, cursor; char text[64]; } edit;
    struct { int on, x0, y0, x1, y1; char text[512]; } box;
    char        hidden[4][9];   /* buttons not shown (FUN_1000fdec: the button's +0x88) */
    int         nhidden;
    char        disabled[16][9];  /* buttons disabled (the control's +0x48 / +0x4c): their D art, no clicks */
    int         ndisabled;
    uint32_t    seed;             /* the C library's rand (MSVC), seeded with time() as the shell does */
    uint8_t     pal[256][3];
    int         mx, my, down;
    char        pressed[9];
    char        clicked[9];
    /* the screen's Smacker animations (FUN_10031e3d / FUN_10032469) */
    shell_anim  anim[8];
    char        stats[256];
    char        objtext[2048];
    /* the briefing's squad panel (shell_set_squad) */
    char        sq_name[2][32], sq_gear[3][32];
    int         sq_face[2];
    hg_pcx      sq_pcx[2];
    int         sq_pcx_for[2];
    int         nanim;
    uint64_t    now;
    /* the gear editor (shell_set_garage) */
    char        g_cat[9];
    char        g_item[40][256];
    int         g_n;
    char        g_gear[32];
    char        g_gdf[2];
    modelview  *g_view;
    float       g_place[3], g_turn;
    modelview  *b_view;               /* the briefing's gear (FUN_1008f28f's view) */
    int         b_has;
    float       b_place[3], b_turn;
    char        b_weap[16][12];
    int         b_nweap;
};

/* the screens' animations (the builders' tables for FUN_100158a0: picture record, mode, buffered): B02 (FUN_1013a75a),
 * B09 (FUN_10145eff), B15 the landship (FUN_1014765e). Modes (FUN_100323c3): 0 once, 1 loop, 2 loop with rests, 3 once
 * for an action that waits for it, 4 still until an action plays it */
static const struct { const char *root; int n; int rec[5], mode[5]; } screen_anims[] = {
    {"B02UNDER", 5, {0x45, 0x46, 0x48, 0x49, 0x47}, {1, 1, 4, 4, 4}},
    {"B09UNDER", 5, {0x21f, 0x222, 0x21e, 0x220, 0x221}, {2, 1, 4, 4, 4}},
    {"B15UNDER", 5, {0x241, 0x242, 0x23e, 0x23f, 0x240}, {1, 1, 4, 4, 4}},
};

static void anim_mode(shell *s, int i, int mode)
{
    if (i < 0 || i >= s->nanim) return;
    s->anim[i].mode = mode;
    /* FUN_100323c3: 0, 1 and 3 play; 2 and 4 wait */
    s->anim[i].state = mode == 2 || mode == 4 ? 1 : 0;
    s->anim[i].paused_at = s->now;
}

static void anims_free(shell *s)
{
    for (int i = 0; i < s->nanim; i++) smk_free(s->anim[i].m);
    s->nanim = 0;
}

static void anims_load(shell *s, const char *root)
{
    anims_free(s);
    for (size_t t = 0; t < sizeof screen_anims / sizeof screen_anims[0]; t++) {
        if (strcmp(screen_anims[t].root, root)) continue;
        char dir[1200], fn[32], path[1300];
        if (fs_find_ci(hga_dir(s->a), "SMK", dir, sizeof dir)) return;
        for (int k = 0; k < screen_anims[t].n && s->nanim < 8; k++) {
            const shell_picture *p = &shell_pictures[screen_anims[t].rec[k]];
            snprintf(fn, sizeof fn, "%s.SMK", p->name);
            size_t len;
            uint8_t *d = fs_find_ci(dir, fn, path, sizeof path) ? NULL : fs_read_all(path, &len);
            smk *m = d ? smk_open(d, len) : NULL;
            /* a missing file keeps the slot (the actions name slots by number) */
            int i = s->nanim++;
            memset(&s->anim[i], 0, sizeof s->anim[i]);
            s->anim[i].m = m;
            s->anim[i].x = p->x0; s->anim[i].y = p->y0;
            snprintf(s->anim[i].name, sizeof s->anim[i].name, "%s", p->name);
            anim_mode(s, i, screen_anims[t].mode[k]);
            s->anim[i].next = (double)s->now;
        }
    }
}

void shell_anim_play(shell *s, int slot, int mode) { anim_mode(s, slot, mode); }

const char *shell_anim_name(const shell *s, int slot)
{
    return slot >= 0 && slot < s->nanim ? s->anim[slot].name : NULL;
}

int shell_anim_busy(const shell *s)
{
    for (int i = 0; i < s->nanim; i++)
        if (s->anim[i].m && s->anim[i].mode == 3 && s->anim[i].state == 0) return 1;
    return 0;
}

/* FUN_10032469, at the files' 15 frames a second (SmackWait) */
void shell_anim_tick(shell *s, uint64_t ms)
{
    s->now = ms;
    for (int i = 0; i < s->nanim; i++) {
        shell_anim *a = &s->anim[i];
        if (!a->m) continue;
        if (a->mode == 2 && a->state == 1) {
            /* resting: once more than 5 whole seconds have passed (time()), a roll each pass of the loop:
             * (rand() & 0xfffff) == 0, MSVC's rand being 15 bits: 1 in 32768 (FUN_10032469, its C library's rand
             * seeded with time() at the start, FUN_1002fb63) */
            if ((int64_t)(ms / 1000) - (int64_t)(a->paused_at / 1000) > 5) {
                s->seed = s->seed * 214013u + 2531011u;
                if (((s->seed >> 16) & 0x7fff & 0xfffff) == 0) a->state = 0;
            }
            continue;
        }
        if (a->state != 0 || (double)ms < a->next) continue;
        if (smk_frame(a->m, a->frame)) { a->state = 2; continue; }
        a->shown = 1;
        a->next = (a->next < (double)ms - 500 ? (double)ms : a->next) + smk_frame_ms(a->m);
        if (a->frame < smk_frames(a->m) - 1) { a->frame++; continue; }
        switch (a->mode) {
        case 0: a->state = 2; break;
        case 1: a->frame = 0; break;
        case 2: a->paused_at = ms; a->state = 1; a->frame = 0; break;
        case 3: a->state = 2; break;
        default: a->state = 2;
        }
    }
}

static void draw_anims(shell *s, uint32_t *out)
{
    for (int i = 0; i < s->nanim; i++) {
        shell_anim *a = &s->anim[i];
        if (!a->m || !a->shown) continue;
        int w = smk_width(a->m), h = smk_height(a->m);
        const uint8_t *px = smk_pixels(a->m);
        for (int y = 0; y < h; y++) {
            int yy = a->y + y;
            if (yy < 0 || yy >= 480) continue;
            for (int x = 0; x < w; x++) {
                int xx = a->x + x;
                if (xx < 0 || xx >= 640) continue;
                /* the indices go into the screen's buffer: the screen's palette */
                const uint8_t *c = s->pal[px[y * w + x]];
                out[yy * 640 + xx] = 0xff000000u | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
            }
        }
    }
}

static int root_of(int i)
{
    for (int guard = 0; guard < 16; guard++) {
        int p = shell_pictures[i].parent;
        if (p < 0 || p >= SHELL_PICTURES) return -1;
        if (p == i) return i;
        i = p;
    }
    return -1;
}

static void abs_pos(int i, int *x, int *y)
{
    *x = 0; *y = 0;
    for (int guard = 0; guard < 16; guard++) {
        int p = shell_pictures[i].parent;
        if (p < 0 || p == i) return;
        *x += shell_pictures[i].x0; *y += shell_pictures[i].y0;
        i = p;
    }
}

static void load_art(shell *s, element *e)
{
    const shell_picture *p = &shell_pictures[e->rec];
    if (e->loaded || !p->name[0]) return;
    e->loaded = 1;
    size_t len;
    if (!strcmp(p->ext, "PCX")) {
        uint8_t *d = hga_load(s->a, p->name, "PCX", &len, NULL);
        if (d && !pcx_parse(d, len, &e->pcx)) e->is_pcx = 1;
        free(d);
    } else if (!strcmp(p->ext, "SHP")) {
        uint8_t *d = hga_load(s->a, p->name, "SHP", &len, NULL);
        if (d) shp_parse(d, len, 0, &e->shp);
        free(d);
    }
}

static void free_layer(layer *l)
{
    for (int i = 0; i < l->nel; i++) { pcx_free(&l->el[i].pcx); shp_free(&l->el[i].shp); }
    free(l->el);
    memset(l, 0, sizeof *l);
    l->root = -1;
}

static void free_widgets(shell *s)
{
    for (int i = 0; i < s->nlist; i++) {
        for (int j = 0; j < s->list[i].n; j++) free(s->list[i].item[j]);
        free(s->list[i].item);
    }
    memset(s->list, 0, sizeof s->list);
    s->nlist = 0;
    s->edit.on = 0;
    s->box.on = 0;
}

static void free_screen(shell *s)
{
    s->nhidden = 0;
    s->ndisabled = 0;
    for (int i = 0; i < 2; i++) free_layer(&s->lay[i]);
    s->nlay = 0;
    free_widgets(s);
}

shell *shell_open(hg_archive *a)
{
    shell *s = calloc(1, sizeof *s);
    if (s) { s->a = a; s->lay[0].root = s->lay[1].root = -1; s->mission = 1; s->outcome = -1; s->seed = (uint32_t)time(NULL); }
    return s;
}

static void map_free(front_map *m)
{
    pcx_free(&m->base); pcx_free(&m->north); pcx_free(&m->south);
    free(m->px);
    memset(m, 0, sizeof *m);
}

void shell_close(shell *s)
{
    if (!s) return;
    free_screen(s);
    anims_free(s);
    for (int i = 0; i < SHELL_FONTS; i++) fnt_free(&s->font[i]);
    for (int i = 0; i < 2; i++) if (s->sq_pcx_for[i]) pcx_free(&s->sq_pcx[i]);
    modelview_close(s->g_view);
    modelview_close(s->b_view);
    map_free(&s->map5);
    map_free(&s->map62);
    shp_free(&s->mapov);
    free(s);
}

/* ---- text (HGSHELL FUN_10032e5f / FUN_101833ab) ---- */

static const hg_font *font(shell *s, int i)
{
    if (i < 0 || i >= SHELL_FONTS) return NULL;
    if (!s->font_tried[i]) {
        s->font_tried[i] = 1;
        size_t len;
        uint8_t *d = hga_load(s->a, shell_fonts[i], "FNT", &len, NULL);
        if (d && fnt_parse(d, len, &s->font[i])) memset(&s->font[i], 0, sizeof s->font[i]);
    }
    return s->font[i].glyph ? &s->font[i] : NULL;
}

static int glyph_w(const hg_font *f, unsigned char c)
{
    return c < f->count && f->glyph[c] ? f->width[c] : 0;
}

static void put(const shell *s, uint32_t *out, int x, int y, uint8_t v)
{
    if (x < 0 || y < 0 || x >= 640 || y >= 480) return;
    const uint8_t *c = s->pal[v];
    out[(size_t)y * 640 + x] = 0xff000000u | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | c[2];
}

/* one string at (x, y), clipped to the rectangle: glyph bytes go through the window's 256-entry colour table, the
 * identity but for entry 0x0e, the text colour (FUN_10030099, FUN_10034a24); 0xff is not drawn */
static void draw_string(const shell *s, uint32_t *out, const hg_font *f, uint8_t colour, int x, int y,
                        int cx0, int cy0, int cx1, int cy1, const char *str)
{
    for (; *str; str++) {
        unsigned char ch = (unsigned char)*str;
        int w = glyph_w(f, ch);
        for (int j = 0; j < f->height && w; j++)
            for (int i = 0; i < w; i++) {
                uint8_t v = f->glyph[ch][j * w + i];
                if (v == 0x0e) v = colour;
                if (v == 0xff) continue;
                int xx = x + i, yy = y + j;
                if (xx < cx0 || yy < cy0 || xx > cx1 || yy > cy1) continue;
                put(s, out, xx, yy, v);
            }
        x += w;
    }
}

/* left-aligned text in a rectangle (inclusive), as FUN_10032e5f: lines break at '\n' and, past the width, at the
 * last space, tab or '-' (which is dropped; a line without one loses the character that overflowed). A tab becomes
 * 10 - x % 10 '{' glyphs (1 pixel wide and empty in the shell's fonts). Lines start at the top, a font height apart,
 * while they start inside the rectangle. */
static void text_left(shell *s, uint32_t *out, int fi, uint8_t colour, int x0, int y0, int x1, int y1, const char *str)
{
    const hg_font *f = font(s, fi);
    if (!f || !str) return;
    size_t n = strlen(str), cap = n + 1000;
    char *buf = malloc(cap + 1);
    if (!buf) return;
    memcpy(buf, str, n + 1);
    char *p = buf, *end = buf + n;
    int width = x1 - x0 + 1, height = y1 - y0 + 1, y = 0;
    while (p < end && y < height) {
        char *line = p;
        int x = 0;
        while (p < end && *p != '\n') {
            if (*p == '\t') {
                *p = '{';
                int k = 9 - x % 10;
                if (end + k > buf + cap) k = 0;
                memmove(p + 1 + k, p + 1, (size_t)(end - p));
                memset(p + 1, '{', (size_t)k);
                end += k;
                x += (k + 1) * glyph_w(f, '{');
                p += k;
            } else x += glyph_w(f, (unsigned char)*p);
            if (x > width) break;
            p++;
        }
        if (p < end) {
            if (*p == '\n') *p = 0;
            else {
                char *q = p;
                while (q >= line && *q != ' ' && *q != '\t' && *q != '-') q--;
                if (q < line) *p = 0;
                else { *q = 0; p = q; }
            }
        }
        if (*line) draw_string(s, out, f, colour, x0, y0 + y, x0, y0, x1, y1, line);
        y += f->height;
        p++;
    }
    free(buf);
}

/* centred text in a rectangle (FUN_10033a59, as FUN_1003420a): lines break as text_left's (no tab stops); each
 * starts at (width - the width up to the character that overflowed) / 2, so a wrapped line is centred with the
 * words that moved down still counted */
static void text_center(shell *s, uint32_t *out, int fi, uint8_t colour, int x0, int y0, int x1, int y1, const char *str)
{
    const hg_font *f = font(s, fi);
    if (!f || !str) return;
    size_t n = strlen(str);
    char *buf = malloc(n + 1);
    if (!buf) return;
    memcpy(buf, str, n + 1);
    char *p = buf, *end = buf + n;
    int width = x1 - x0 + 1, height = y1 - y0 + 1, y = 0;
    while (p < end && y < height) {
        char *line = p;
        int acc = 0, wl = 0;
        while (p < end && *p != '\n') {
            wl = glyph_w(f, (unsigned char)*p);
            acc += wl;
            if (acc > width) break;
            p++;
        }
        if (p >= end || *p == '\n') wl = 0;
        if (p < end) {
            if (*p == '\n') *p = 0;
            else {
                char *q = p;
                while (q >= line && *q != ' ' && *q != '\t' && *q != '-') q--;
                if (q < line) *p = 0;
                else { *q = 0; p = q; }
            }
        }
        if (*line) draw_string(s, out, f, colour, x0 + (width + wl - acc) / 2, y0 + y, x0, y0, x1, y1, line);
        y += f->height;
        p++;
    }
    free(buf);
}

/* the width of a string in pixels (FUN_10034898) */
static int text_width(shell *s, int fi, const char *str)
{
    const hg_font *f = font(s, fi);
    int x = 0;
    if (!f) return 0;
    for (; *str; str++) x += *str == '\t' ? (10 - x % 10) * glyph_w(f, '{') : glyph_w(f, (unsigned char)*str);
    return x;
}

/* ---- the campaign map (FUN_100af4c3 / FUN_100af954) ---- */

static int load_pcx(hg_archive *a, const char *name, hg_pcx *p)
{
    size_t len;
    uint8_t *d = hga_load(a, name, "PCX", &len, NULL);
    int r = d ? pcx_parse(d, len, p) : -1;
    free(d);
    return r;
}

static const uint8_t *map_pixels(shell *s, front_map *m, const char *base, const char *north, const char *south)
{
    if (!m->loaded) {
        m->loaded = 1;
        m->ok = !load_pcx(s->a, base, &m->base) && !load_pcx(s->a, north, &m->north) && !load_pcx(s->a, south, &m->south) &&
                m->north.w == m->base.w && m->south.w == m->base.w && m->north.h == m->base.h && m->south.h == m->base.h;
        m->front = -1;
    }
    if (!m->ok) return NULL;
    int e = s->mission >= 0 && s->mission < SHELL_CAMPAIGN ? s->mission : 0;
    if (m->px && m->front == e) return m->px;
    int W = m->base.w, H = m->base.h;
    free(m->px);
    m->px = malloc((size_t)W * H);
    if (!m->px) return NULL;
    memcpy(m->px, m->base.px, (size_t)W * H);
    m->front = e;
    /* the front line's heights, clamped to 0..1 */
    float ys[19];
    for (int i = 0; i < 19; i++) {
        float v = shell_front[e][i];
        ys[i] = v >= 0.0f ? (v <= 1.0f ? v : 1.0f) : 0.0f;
    }
    /* each column: t, the line's height there (linear between the table's x positions); the north map down to
     * (t - 0.15 t^2) H, the line in colour 7 at t H, the south map from (0.15 t^2 + 0.7 t + 0.15) H; the base map
     * shows in between */
    /* x87: intermediates in extended precision (double here), stored to floats where the original stores them */
    /* the columns run over the image's bytes per line (272 for the 271-wide B05 maps), which also scales x */
    float fw = (float)m->base.bpl, fh = (float)H;
    const double a = 0.15f, b = 0.7f;
    int k = 0;
    for (float fx = 0.0f; fx < fw; fx += 1.0f) {
        while (k < 18 && (double)shell_front_x[k] < (double)fx / fw) k++;
        float t = ys[0];
        if (fx != 0.0f && k > 0) {
            float slope = (float)(((double)ys[k] - ys[k - 1]) / ((double)shell_front_x[k] - shell_front_x[k - 1]));
            t = (float)(((double)fx / fw - shell_front_x[k - 1]) * slope + ys[k - 1]);
        }
        float top = (float)((t - a * t * t) * fh);
        float bottom = (float)((a * t * t + b * t + a) * fh);
        int x = (int)fx;
        if (x >= W) continue;
        for (int y = 0; y <= (int)top && y < H; y++) m->px[(size_t)y * W + x] = m->north.px[(size_t)y * W + x];
        int ly = (int)((double)fh * t);
        if (ly >= 0 && ly < H) m->px[(size_t)ly * W + x] = 7;
        for (int y = (int)bottom; y < H; y++) if (y >= 0) m->px[(size_t)y * W + x] = m->south.px[(size_t)y * W + x];
    }
    return m->px;
}

static void draw_map(shell *s, uint32_t *out, front_map *m, const char *base, const char *north, const char *south,
                     int x0, int y0)
{
    const uint8_t *px = map_pixels(s, m, base, north, south);
    if (!px) return;
    for (int y = 0; y < m->base.h; y++)
        for (int x = 0; x < m->base.w; x++) put(s, out, x0 + x, y0 + y, px[(size_t)y * m->base.w + x]);
}

static void draw_shape(shell *s, uint32_t *out, const hg_shape *sh, int x, int y)
{
    if (!sh->px) return;
    for (int j = 0; j < sh->h; j++)
        for (int i = 0; i < sh->w; i++)
            if (sh->mask[(size_t)j * sh->w + i]) put(s, out, x + sh->x0 + i, y + sh->y0 + j, sh->px[(size_t)j * sh->w + i]);
}

int shell_set_mission(shell *s, int entry)
{
    if (entry < 0 || entry >= SHELL_CAMPAIGN) return -1;
    s->mission = entry;
    s->has_brf = 0;
    const char *name = shell_campaign[entry].briefing;
    if (!name[0]) return -1;
    size_t len;
    uint8_t *d = hga_load(s->a, name, "BRF", &len, NULL);
    if (d && !brf_parse(d, len, &s->brf)) s->has_brf = 1;
    free(d);
    return s->has_brf ? 0 : -1;
}

const struct hg_brf_s *shell_brf(const shell *s)
{
    return s->has_brf ? &s->brf : NULL;
}

void shell_set_squad(shell *s, const char *name1, int face1, const char *gear1, const char *name2, int face2,
                     const char *gear2, const char *player_gear)
{
    snprintf(s->sq_name[0], sizeof s->sq_name[0], "%s", name1 ? name1 : "");
    snprintf(s->sq_name[1], sizeof s->sq_name[1], "%s", name2 ? name2 : "");
    snprintf(s->sq_gear[0], sizeof s->sq_gear[0], "%s", gear1 ? gear1 : "");
    snprintf(s->sq_gear[1], sizeof s->sq_gear[1], "%s", gear2 ? gear2 : "");
    snprintf(s->sq_gear[2], sizeof s->sq_gear[2], "%s", player_gear ? player_gear : "");
    s->sq_face[0] = face1;
    s->sq_face[1] = face2;
}

static int text_height(shell *s, int fi, int width, const char *str);

void shell_set_briefing_gear(shell *s, const unsigned char *gdf, size_t len, const char (*extra)[9], const float place[3],
                             const char *const *weapons, int nweapons)
{
    if (!s->b_view) s->b_view = modelview_open(s->a);
    s->b_has = s->b_view && gdf && len && !modelview_set_data(s->b_view, gdf, len, extra);
    for (int k = 0; k < 3; k++) s->b_place[k] = place ? place[k] : 0.0f;
    s->b_turn = 180.0f;
    s->b_nweap = 0;
    for (int i = 0; i < nweapons && i < 16; i++) snprintf(s->b_weap[s->b_nweap++], sizeof s->b_weap[0], "%s", weapons[i]);
}

void shell_briefing_gear_turn(shell *s, float turn)
{
    s->b_turn = turn;
}

void shell_set_garage(shell *s, const char *cat, const char *const *items, int n, const char *gear,
                      const unsigned char *gdf, size_t len, const char (*extra)[9])
{
    snprintf(s->g_cat, sizeof s->g_cat, "%s", cat ? cat : "");
    s->g_n = 0;
    for (int i = 0; i < n && i < 40; i++) snprintf(s->g_item[s->g_n++], sizeof s->g_item[0], "%s", items[i]);
    snprintf(s->g_gear, sizeof s->g_gear, "%s", gear ? gear : "");
    s->g_gdf[0] = gdf && len ? 1 : 0;
    if (!s->g_view) s->g_view = modelview_open(s->a);
    if (s->g_view && gdf) modelview_set_data(s->g_view, gdf, len, extra);
}

void shell_garage_view(shell *s, const float place[3], float turn)
{
    for (int k = 0; k < 3; k++) s->g_place[k] = place[k];
    s->g_turn = turn;
}

/* the left list's layout (as drawn below) */
#define GL_X0 (4 + 0x70)
#define GL_X1 (4 + 0xce)
#define GL_Y0 (79 + 0x19)
#define GL_Y1 (79 + 0x173)

static int item_h(shell *s, const char *t)
{
    int h = text_height(s, 9, GL_X1 - GL_X0 + 1, t);
    return h > ((0x65 - 5) + 1) * 3 / 2 ? h : ((0x65 - 5) + 1) * 3 / 2;
}

int shell_garage_screen(shell *s, int rec, float *x, float *y)
{
    return s->g_view && s->g_gdf[0] ? modelview_screen(s->g_view, rec, x, y) : -1;
}

int shell_garage_model_pick(shell *s, int x, int y)
{
    return s->g_view && s->g_gdf[0] ? modelview_pick(s->g_view, x, y) : -1;
}

int shell_garage_pick(shell *s, int x, int y)
{
    if (x < 4 + 5 || x > GL_X1 || y < GL_Y0 || y > GL_Y1) return -1;
    int yy = GL_Y0;
    for (int i = 0; i < s->g_n; i++) {
        int h = item_h(s, s->g_item[i]);
        if (y < yy + h) return i;
        yy += h;
    }
    return -1;
}

void shell_set_objectives(shell *s, const char *text)
{
    snprintf(s->objtext, sizeof s->objtext, "%s", text ? text : "");
}

void shell_set_stats(shell *s, const char *text)
{
    snprintf(s->stats, sizeof s->stats, "%s", text ? text : "");
}

void shell_set_outcome(shell *s, int outcome)
{
    s->outcome = outcome;
}

int shell_mission(const shell *s)
{
    return s->mission;
}

const char *shell_mission_scenario(const shell *s)
{
    return shell_campaign[s->mission].scenario;
}

static int find_root(const char *root)
{
    for (int i = 0; i < SHELL_PICTURES; i++)
        if (!strcmp(shell_pictures[i].name, root) && shell_pictures[i].parent == i) return i;
    return -1;
}

static int find_child(int parent, const char *suffix)
{
    size_t n = strlen(suffix);
    for (int i = 0; i < SHELL_PICTURES; i++) {
        size_t m = strlen(shell_pictures[i].name);
        if (i != parent && shell_pictures[i].parent == parent && m >= n && !strcmp(shell_pictures[i].name + m - n, suffix))
            return i;
    }
    return -1;
}

static int find_rec(const char *name)
{
    for (int i = 0; i < SHELL_PICTURES; i++) if (!strcmp(shell_pictures[i].name, name)) return i;
    return -1;
}

static void build_layer(shell *s, layer *l, int r, int ox, int oy)
{
    l->root = r;
    l->el = calloc(SHELL_PICTURES, sizeof *l->el);
    l->nel = 0;
    for (int i = 0; i < SHELL_PICTURES; i++) {
        if (root_of(i) != r) continue;
        const shell_picture *p = &shell_pictures[i];
        element *e = &l->el[l->nel++];
        e->rec = i;
        e->kind = p->kind;
        abs_pos(i, &e->x, &e->y);
        e->x += ox; e->y += oy;
        e->w = p->x1 - p->x0 + 1; e->h = p->y1 - p->y0 + 1;
        if (p->x1 == 0 && p->y1 == 0) e->w = e->h = 0;
        size_t n = strlen(p->name);
        if (p->kind >= 1 && p->kind <= 4 && n > 1) { memcpy(e->stem, p->name, n - 1); e->stem[n - 1] = 0; }
        load_art(s, e);
    }
}

int shell_goto(shell *s, const char *root)
{
    int r = find_root(root);
    if (r < 0) return -1;
    free_screen(s);
    build_layer(s, &s->lay[0], r, 0, 0);
    s->nlay = 1;
    memset(s->pal, 0, sizeof s->pal);
    const layer *l = &s->lay[0];
    if (l->nel && l->el[0].is_pcx && l->el[0].pcx.has_pal) memcpy(s->pal, l->el[0].pcx.pal, sizeof s->pal);
    s->pressed[0] = 0;
    anims_load(s, root);
    return 0;
}

int shell_popup(shell *s, const char *root)
{
    int r = find_root(root);
    if (r < 0 || !s->nlay) return -1;
    shell_popup_close(s);
    /* placed at its placement record: the root's kind-0 child named *LOC* */
    int ox = 0, oy = 0;
    for (int i = 0; i < SHELL_PICTURES; i++)
        if (shell_pictures[i].parent == r && i != r && shell_pictures[i].kind == 0 && strstr(shell_pictures[i].name, "LOC")) {
            ox = shell_pictures[i].x0; oy = shell_pictures[i].y0;
            break;
        }
    build_layer(s, &s->lay[1], r, ox, oy);
    /* the popup's place record itself is not a picture to draw */
    s->nlay = 2;
    s->pressed[0] = 0;
    return 0;
}

void shell_popup_close(shell *s)
{
    if (s->nlay < 2) return;
    free_layer(&s->lay[1]);
    s->nlay = 1;
    s->edit.on = 0;
    s->box.on = 0;
}

const char *shell_screen(const shell *s)
{
    return s->nlay && s->lay[0].root >= 0 ? shell_pictures[s->lay[0].root].name : "";
}

const char *shell_popup_name(const shell *s)
{
    return s->nlay > 1 ? shell_pictures[s->lay[1].root].name : NULL;
}

static const element *find_el(const shell *s, int rec)
{
    for (int k = s->nlay - 1; k >= 0; k--)
        for (int i = 0; i < s->lay[k].nel; i++) if (s->lay[k].el[i].rec == rec) return &s->lay[k].el[i];
    return NULL;
}

/* ---- lists ---- */

static shell_list *list_of(shell *s, const char *name)
{
    for (int i = 0; i < s->nlist; i++) if (!strcmp(shell_pictures[s->list[i].rec].name, name)) return &s->list[i];
    return NULL;
}

int shell_list_set(shell *s, const char *name, const char *const *items, int n, int sel)
{
    shell_list *l = list_of(s, name);
    if (!l) {
        int r = find_rec(name);
        const element *e = r >= 0 ? find_el(s, r) : NULL;
        int in = r >= 0 ? find_child(r, "INNR") : -1, bar = r >= 0 ? find_child(r, "SLCT") : -1;
        if (!e || in < 0 || bar < 0 || s->nlist >= SHELL_MAX_LISTS) return -1;
        l = &s->list[s->nlist++];
        memset(l, 0, sizeof *l);
        l->rec = r;
        const shell_picture *pi = &shell_pictures[in], *pb = &shell_pictures[bar];
        l->ix0 = e->x + pi->x0; l->iy0 = e->y + pi->y0; l->ix1 = e->x + pi->x1; l->iy1 = e->y + pi->y1;
        l->bx0 = e->x + pb->x0; l->by0 = e->y + pb->y0; l->bx1 = e->x + pb->x1; l->by1 = e->y + pb->y1;
        /* its buttons in table order: up, then down */
        for (int i = 0; i < SHELL_PICTURES; i++) {
            const shell_picture *p = &shell_pictures[i];
            if (p->parent != r || p->kind != 2) continue;
            size_t m = strlen(p->name);
            char *dst = !l->up[0] ? l->up : !l->down[0] ? l->down : NULL;
            if (dst && m > 1) { memcpy(dst, p->name, m - 1); dst[m - 1] = 0; }
        }
    }
    for (int j = 0; j < l->n; j++) free(l->item[j]);
    free(l->item);
    l->item = n ? calloc((size_t)n, sizeof *l->item) : NULL;
    l->n = 0;
    for (int j = 0; j < n; j++) {
        size_t m = strlen(items[j]);
        l->item[j] = malloc(m + 1);
        if (l->item[j]) { memcpy(l->item[j], items[j], m + 1); l->n++; }
    }
    l->sel = n ? (sel < 0 ? 0 : sel >= l->n ? l->n - 1 : sel) : -1;
    return 0;
}

int shell_list_selected(const shell *s, const char *name)
{
    const shell_list *l = list_of((shell *)s, name);
    return l ? l->sel : -1;
}

/* ---- text entry (FUN_10013137) and text box (FUN_100140a7) ---- */

int shell_edit_set(shell *s, int x0, int y0, int x1, int y1, int max, const char *text)
{
    s->edit.on = 1;
    s->edit.x0 = x0; s->edit.y0 = y0; s->edit.x1 = x1; s->edit.y1 = y1;
    s->edit.max = max < (int)sizeof s->edit.text - 1 ? max : (int)sizeof s->edit.text - 1;
    snprintf(s->edit.text, sizeof s->edit.text, "%s", text ? text : "");
    s->edit.cursor = (int)strlen(s->edit.text);
    return 0;
}

const char *shell_edit_text(const shell *s)
{
    return s->edit.on ? s->edit.text : "";
}

/* FUN_100132f0: Backspace deletes before the cursor, Left / Right move it, Delete deletes at it, a character goes
 * in at it while the text is shorter than the maximum (Tab, Return and line feed are not taken) */
void shell_key(shell *s, int key)
{
    if (!s->edit.on) return;
    char *t = s->edit.text;
    int n = (int)strlen(t), c = s->edit.cursor;
    if (key == SHELL_KEY_BACKSPACE) { if (c > 0) { memmove(t + c - 1, t + c, (size_t)(n - c + 1)); s->edit.cursor--; } }
    else if (key == SHELL_KEY_LEFT) { if (c > 0) s->edit.cursor--; }
    else if (key == SHELL_KEY_RIGHT) { if (c < n) s->edit.cursor++; }
    else if (key == SHELL_KEY_DELETE) { if (c < n) memmove(t + c, t + c + 1, (size_t)(n - c)); }
}

void shell_char(shell *s, int ch)
{
    if (!s->edit.on || ch == '\b' || ch == '\n' || ch == '\t' || ch == '\r' || ch < 32 || ch > 255) return;
    char *t = s->edit.text;
    int n = (int)strlen(t), c = s->edit.cursor;
    if (n >= s->edit.max) return;
    memmove(t + c + 1, t + c, (size_t)(n - c + 1));
    t[c] = (char)ch;
    s->edit.cursor++;
}

int shell_box_set(shell *s, int x0, int y0, int x1, int y1, const char *text)
{
    s->box.on = 1;
    s->box.x0 = x0; s->box.y0 = y0; s->box.x1 = x1; s->box.y1 = y1;
    snprintf(s->box.text, sizeof s->box.text, "%s", text ? text : "");
    return 0;
}

/* the button stem under (x, y): a rest (kind 2) record of a button group that contains the point, in the top
 * layer (a popup takes the mouse) */
static int is_hidden(const shell *s, const char *stem)
{
    for (int i = 0; i < s->nhidden; i++) if (!strcmp(s->hidden[i], stem)) return 1;
    return 0;
}

void shell_hide(shell *s, const char *stem)
{
    if (s->nhidden < 4 && !is_hidden(s, stem)) snprintf(s->hidden[s->nhidden++], sizeof s->hidden[0], "%s", stem);
}

static int is_disabled(const shell *s, const char *stem)
{
    for (int i = 0; i < s->ndisabled; i++) if (!strcmp(s->disabled[i], stem)) return 1;
    return 0;
}

void shell_disable(shell *s, const char *stem, int off)
{
    int i = 0;
    while (i < s->ndisabled && strcmp(s->disabled[i], stem)) i++;
    if (off && i == s->ndisabled && s->ndisabled < 16) snprintf(s->disabled[s->ndisabled++], sizeof s->disabled[0], "%s", stem);
    if (!off && i < s->ndisabled) { memmove(s->disabled[i], s->disabled[i + 1], (size_t)(s->ndisabled - i - 1) * sizeof s->disabled[0]); s->ndisabled--; }
}

static const char *hit(const shell *s, int x, int y)
{
    if (!s->nlay) return NULL;
    const layer *l = &s->lay[s->nlay - 1];
    for (int i = l->nel - 1; i >= 0; i--) {
        const element *e = &l->el[i];
        if (e->kind != 2 || !e->stem[0] || !e->w || is_hidden(s, e->stem)) continue;
        if (x >= e->x && y >= e->y && x < e->x + e->w && y < e->y + e->h) return is_disabled(s, e->stem) ? NULL : e->stem;
    }
    return NULL;
}

const char *shell_mouse(shell *s, int x, int y, int down)
{
    s->mx = x; s->my = y;
    const char *h = hit(s, x, y);
    const char *ret = NULL;
    if (down && !s->down) snprintf(s->pressed, sizeof s->pressed, "%s", h ? h : "");
    if (!down && s->down) {
        if (h && s->pressed[0] && !strcmp(h, s->pressed)) {
            snprintf(s->clicked, sizeof s->clicked, "%s", h);
            ret = s->clicked;
            /* a list's arrows move its selection (FUN_100030e6) */
            for (int i = 0; i < s->nlist; i++) {
                shell_list *l = &s->list[i];
                if (!strcmp(h, l->up) && l->sel > 0) l->sel--;
                if (!strcmp(h, l->down) && l->sel < l->n - 1) l->sel++;
            }
        }
        s->pressed[0] = 0;
    }
    s->down = down;
    return ret;
}

static void blit_idx(uint32_t *out, const uint8_t (*pal)[3], const uint8_t *px, const uint8_t *mask, int w, int h,
                     int x, int y, int clipw, int cliph)
{
    if (clipw <= 0 || clipw > w) clipw = w;
    if (cliph <= 0 || cliph > h) cliph = h;
    for (int j = 0; j < cliph; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= 480) continue;
        for (int i = 0; i < clipw; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= 640) continue;
            size_t o = (size_t)j * w + i;
            if (mask && !mask[o]) continue;
            const uint8_t *c = pal[px[o]];
            out[(size_t)yy * 640 + xx] = 0xff000000u | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | c[2];
        }
    }
}

static void draw_el(shell *s, uint32_t *out, const element *e)
{
    const uint8_t (*pal)[3] = (const uint8_t (*)[3])s->pal;
    if (e->is_pcx) blit_idx(out, pal, e->pcx.px, NULL, e->pcx.w, e->pcx.h, e->x, e->y, 0, 0);
    else if (e->shp.px) blit_idx(out, pal, e->shp.px, e->shp.mask, e->shp.w, e->shp.h, e->x + e->shp.x0, e->y + e->shp.y0, 0, 0);
}

static int has_state(const layer *l, const char *stem, int kind)
{
    for (int i = 0; i < l->nel; i++) if (l->el[i].kind == kind && !strcmp(l->el[i].stem, stem)) return 1;
    return 0;
}

/* pass 0 everything, 1 the pictures only, 2 the buttons only */
static void draw_layer(shell *s, uint32_t *out, const layer *l, const char *hover, int pass)
{
    for (int i = 0; i < l->nel; i++) {
        const element *e = &l->el[i];
        if (e->kind == 5) { if (pass != 2) draw_el(s, out, e); continue; }
        if (pass == 1) continue;
        if (e->kind < 1 || e->kind > 4 || !e->stem[0] || is_hidden(s, e->stem)) continue;
        /* a button: the state that applies, else its rest picture (a list's panel has only that) */
        int state = 2;
        if (is_disabled(s, e->stem) && has_state(l, e->stem, 1)) state = 1;
        else if (s->pressed[0] && !strcmp(s->pressed, e->stem) && hover && !strcmp(hover, e->stem)) state = 4;
        else if (s->g_cat[0] && !strcmp(s->g_cat, e->stem) && !strcmp(shell_screen(s), "B07UNDER")) state = 4;
        else if (hover && !strcmp(hover, e->stem)) state = 3;
        if (!has_state(l, e->stem, state)) state = 2;
        if (e->kind == state) draw_el(s, out, e);
    }
}

/* text from (x, y) on, clipped to a rectangle (FUN_10033e32: FUN_10032e5f with the start inside the rectangle) */
static void text_at(shell *s, uint32_t *out, int fi, uint8_t colour, int x, int y, int cx0, int cy0, int cx1, int cy1,
                    const char *str)
{
    const hg_font *f = font(s, fi);
    if (!f) return;
    if (y > cy1) return;
    char *buf = malloc(strlen(str) + 1000 + 1);
    if (!buf) return;
    size_t n = strlen(str), cap = n + 1000;
    memcpy(buf, str, n + 1);
    char *p = buf, *end = buf + n;
    int width = cx1 - cx0 + 1, height = cy1 - cy0 + 1, yy = y - cy0, x0 = x - cx0;
    while (p < end && yy < height) {
        char *line = p;
        int xx = x0;
        while (p < end && *p != '\n') {
            if (*p == '\t') {
                *p = '{';
                int k = 9 - xx % 10;
                if (end + k > buf + cap) k = 0;
                memmove(p + 1 + k, p + 1, (size_t)(end - p));
                memset(p + 1, '{', (size_t)k);
                end += k;
                xx += (k + 1) * glyph_w(f, '{');
                p += k;
            } else xx += glyph_w(f, (unsigned char)*p);
            if (xx > width) break;
            p++;
        }
        if (p < end) {
            if (*p == '\n') *p = 0;
            else {
                char *q = p;
                while (q >= line && *q != ' ' && *q != '\t' && *q != '-') q--;
                if (q < line) *p = 0;
                else { *q = 0; p = q; }
            }
        }
        if (*line) draw_string(s, out, f, colour, cx0 + x0, cy0 + yy, cx0, cy0, cx1, cy1, line);
        yy += f->height;
        p++;
    }
    free(buf);
}

/* the wrapped height of a string in a width (FUN_10034645): lines x the font's height, counting a line break where
 * the width is passed (carrying the current word, unless it is wider than the width) or at '\n' / '\r' */
static int text_height(shell *s, int fi, int width, const char *str)
{
    const hg_font *f = font(s, fi);
    if (!f) return 0;
    int lines = 1, x = 0, word = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        int w = *p == '\t' ? 10 - x % 10 : glyph_w(f, *p);
        if (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r' || *p == '-') word = 0;
        else word += w;
        x += w;
        if (width < x || *p == '\n' || *p == '\r') {
            if (width < word) word = 0;
            x = word;
            lines++;
        }
    }
    return lines * f->height;
}

/* FUN_1000364b: the selected item in the selection bar, centred in its height; the items before it upward from
 * the bar's top, the items after it downward from its bottom, all clipped to the inner area */
static void draw_list(shell *s, uint32_t *out, const shell_list *l)
{
    if (l->sel < 0 || l->sel >= l->n) return;
    const hg_font *f = font(s, 9);
    if (!f) return;
    int iw = l->ix1 - l->ix0 + 1, barh = l->by1 - l->by0 + 1;
    int h = text_height(s, 9, iw, l->item[l->sel]);
    text_at(s, out, 9, 0xfe, l->bx0, l->by0 + (barh - h) / 2, l->ix0, l->iy0, l->ix1, l->iy1, l->item[l->sel]);
    int y = l->by0;
    for (int i = l->sel - 1; i >= 0 && l->iy0 < y; i--) {
        y -= text_height(s, 9, iw, l->item[i]);
        text_at(s, out, 9, 0xfe, l->ix0, y, l->ix0, l->iy0, l->ix1, l->iy1, l->item[i]);
    }
    y = l->by1;
    for (int i = l->sel + 1; i < l->n && y < l->iy1; i++) {
        text_at(s, out, 9, 0xfe, l->ix0, y, l->ix0, l->iy0, l->ix1, l->iy1, l->item[i]);
        y += text_height(s, 9, iw, l->item[i]);
    }
}

void shell_draw(shell *s, uint32_t *out)
{
    for (int i = 0; i < 640 * 480; i++) out[i] = 0xff000000u;
    if (!s->nlay) return;
    const char *hover = hit(s, s->mx, s->my);
    const char *scr = shell_screen(s);
    if (!strcmp(scr, "B07UNDER")) {
        /* the gear editor: its 3D view (a control of its own, FUN_100a6bb5, its rectangle 0x101e175a) between the
         * screen's pictures and the buttons over it (details, delete piece: drawn over the model in the original) */
        draw_layer(s, out, &s->lay[0], NULL, 1);
        if (s->g_view && s->g_gdf[0])
            modelview_draw(s->g_view, out, (const uint8_t (*)[3])s->pal, 233, 88, 469, 461, s->g_place, s->g_turn);
        draw_layer(s, out, &s->lay[0], s->nlay == 1 ? hover : NULL, 2);
    } else {
        draw_layer(s, out, &s->lay[0], s->nlay == 1 ? hover : NULL, 0);
    }
    draw_anims(s, out);
    if (!strcmp(scr, "B05UNDER")) {
        /* the briefing (FUN_1013c185): the front-line map in (38, 191)-(308, 460) under its frame B05MAPOV
         * (FUN_1008994f, FUN_10089b18); the mission summary in DEVDDEUT (font 9), colour 0xfe, in (353, 195)-(619, 365)
         * (FUN_10072e2f, FUN_10072c27) */
        draw_map(s, out, &s->map5, "B05_BASE", "B05NORTH", "B05SOUTH", 38, 191);
        if (!s->mapov_tried) {
            s->mapov_tried = 1;
            size_t len;
            uint8_t *d = hga_load(s->a, "B05MAPOV", "SHP", &len, NULL);
            if (d) shp_parse(d, len, 0, &s->mapov);
            free(d);
        }
        draw_shape(s, out, &s->mapov, 38, 191);
        if (s->has_brf) text_left(s, out, 9, 0xfe, 353, 195, 619, 365, s->brf.summary);
        /* the squad panel (FUN_10076b6f lists at (22, 36) and (204, 36), FUN_1000f034 draws them): the pilot's
         * portrait (B05 picture 0x11d + k ..., src/squad.h) in (6, 4)-(138, 90) of the list, his name in DEVDDEUT
         * colour 0xfe from the text rectangle's corner (10, 4)-(131, 96). The gear lists (FUN_1009a07f) at
         * (13, 143)-(174, 167), (195, 142)-(355, 166) and the player's (379, 142)-(624, 166) */
        /* the player's gear (FUN_1008f28f's view at (383, 38)-(506, 127), FUN_1008f502): the model, then its weapons'
         * short names (0x101aff98 + id x 0xaa, through the language table 0x1018c0e5) for its equipment records of
         * types 5..10, from the rectangle's top left in font 9, colour 0xfe, a line each: the font's height, or the
         * rectangle's height / their count when they wouldn't fit. Not done: the gear above the game's level (its
         * tech, +0xb8, over +0xbc), drawn dark (ambient 5) under the message in 0x1024db40 */
        if (s->b_has) {
            modelview_draw(s->b_view, out, (const uint8_t (*)[3])s->pal, 383, 38, 506, 127, s->b_place, s->b_turn);
            const hg_font *bf = font(s, 9);
            if (bf && s->b_nweap > 0) {
                int lh = bf->height, h = 127 - 38 + 1;
                if (s->b_nweap * lh > h) lh = h / s->b_nweap;
                for (int i = 0, y = 38; i < s->b_nweap; i++, y += lh) text_left(s, out, 9, 0xfe, 383, y, 506, 127, s->b_weap[i]);
            }
        }
        static const int lx[2] = {22, 204}, gx0[3] = {13, 195, 379}, gy0[3] = {143, 142, 142}, gw[3] = {161, 160, 245};
        for (int i = 0; i < 2; i++) {
            int fp = s->sq_face[i];
            if (fp >= 0 && fp < SHELL_PICTURES) {
                if (s->sq_pcx_for[i] != fp + 1) {
                    if (s->sq_pcx_for[i]) pcx_free(&s->sq_pcx[i]);
                    s->sq_pcx_for[i] = load_pcx(s->a, shell_pictures[fp].name, &s->sq_pcx[i]) ? 0 : fp + 1;
                }
                if (s->sq_pcx_for[i] == fp + 1)
                    blit_idx(out, (const uint8_t (*)[3])s->pal, s->sq_pcx[i].px, NULL, s->sq_pcx[i].w, s->sq_pcx[i].h,
                             lx[i] + 6, 40, 133, 87);
            }
            if (s->sq_name[i][0]) text_left(s, out, 9, 0xfe, lx[i] + 10, 40, lx[i] + 131, 132, s->sq_name[i]);
        }
        /* the gear lists (FUN_1009a07f, drawn by 0x1009b0ca): the current gear's name centred (FUN_10033a59) in the text
         * rectangle (the builder's (0, 0)-(w, 24) moved to the list: (13, 143)-(174, 167), (195, 142)-(355, 166),
         * (379, 142)-(624, 166)), from (its height - the font's) / 2 down; font 9, colour 0xfe */
        const hg_font *gf = font(s, 9);
        for (int i = 0; i < 3; i++)
            if (s->sq_gear[i][0] && gf) {
                int off = (24 - gf->height) / 2;
                text_center(s, out, 9, 0xfe, gx0[i], gy0[i] + off, gx0[i] + gw[i], gy0[i] + 24, s->sq_gear[i]);
            }
    } else if (!strcmp(scr, "B62UNDER")) {
        /* the situation (FUN_10162f23): the front-line map over the screen; the situation text in SERPDEUS (font 7),
         * colour 0xfe, in the BRF's rectangle of the screen; each marker's icon at its place and its label right of
         * the icon, bottom-aligned with it (FUN_100e962d, FUN_100e97ee) */
        draw_map(s, out, &s->map62, "B62_BASE", "B62NORTH", "B62SOUTH", 0, 0);
        if (s->has_brf) {
            const hg_brf *b = &s->brf;
            int W = 640, H = 480;
            text_left(s, out, 7, 0xfe, (int)(W * b->sit_rect[0]), (int)(H * b->sit_rect[1]), (int)(W * b->sit_rect[2]),
                      (int)(H * b->sit_rect[3]), b->situation);
            const hg_font *f = font(s, 7);
            for (int i = 0; i < b->nmarker; i++) {
                int px = (int)(W * b->marker[i].x), py = (int)(H * b->marker[i].y);
                hg_shape ic = {0};
                size_t len;
                uint8_t *d = hga_load(s->a, b->marker[i].icon, "SHP", &len, NULL);
                if (d) shp_parse(d, len, 0, &ic);
                free(d);
                draw_shape(s, out, &ic, px, py);
                /* FUN_10034d31: the shape's first two words, its height - 1 and width - 1 */
                int dw = ic.w ? ic.w - 1 : 0, dh = ic.h ? ic.h - 1 : 0;
                shp_free(&ic);
                if (!f) continue;
                int tw = text_width(s, 7, b->marker[i].label);
                text_left(s, out, 7, 0xfe, px + dw + 1, py + dh - f->height, px + dw + 2 + tw, py + dh, b->marker[i].label);
            }
        }
    }
    if (!strcmp(scr, "B07UNDER")) {
        /* the gear editor's left list (FUN_100b2dac at B07BLIST (4, 79)): from (5, 0x19) to (0xce, 0x173) of it, each
         * item's picture in the column 5..0x65 and its text (DEVDDEUT, colour 0xfe) in 0x70..0xce, an item as tall as
         * its text or ((0x65 - 5) + 1) x 3 / 2 (+0x12d, FUN_100b327c). The text: the name and its statistics
         * (src/garage.c). Not drawn yet: the part's picture (rendered from its model, 0x1013804f) */
        int y = GL_Y0;
        for (int i = 0; i < s->g_n && y <= GL_Y1; i++) {
            text_left(s, out, 9, 0xfe, GL_X0, y, GL_X1, GL_Y1, s->g_item[i]);
            y += item_h(s, s->g_item[i]);
        }
        /* the gear name selector (FUN_100a041c at B07GLIST): the name in its inner area (34, 4)-(183, 21) */
        if (s->g_gear[0]) text_center(s, out, 9, 0xfe, 241 + 34, 91 + 4, 241 + 183, 91 + 21, s->g_gear);
    }
    if (!strcmp(scr, "B06UNDER") && s->has_brf && s->outcome >= 0) {
        /* the debriefing (FUN_100e2610 / FUN_100e27c9): the BRF's first debriefing on success (outcome 2), its second
         * otherwise; the title centred in (56, 47)-(293, 68), the text in (360, 53)-(612, 416), DEVDDEUT, colour
         * 0xfe. The objectives' list isn't drawn yet. */
        const brf_debrief *d = &s->brf.debrief[s->outcome == 2 ? 0 : 1];
        if (s->brf.ndebrief > (s->outcome == 2 ? 0 : 1)) {
            text_center(s, out, 9, 0xfe, 56, 47, 293, 68, d->title);
            text_left(s, out, 9, 0xfe, 360, 53, 612, 416, d->text);
        }
        /* the statistics (FUN_100e27c9, at 0x100e3497): one text, "Vehicle Kills: " n "\n" "Hit Rate: " n "%" "\n"
         * "Hits Taken: " n "\n" "Squad-mate Kills: " n "\n" "Squad-mates Killed: " n (the separator the string at
         * 0x10254f90, "\n" in the running original), drawn as the debriefing text (FUN_10032e5f) in its control's
         * second rectangle, (28, 344)-(320, 420) (FUN_100e2610's +0x20), font 9 (+0x44), colour 0xfe (+0x4c).
         * Not done: the objectives' score list (FUN_100b9a09 at (29, 95)-(321, 309), drawn by 0x100b9fad: the
         * objectives under "In Progress", "Complete" (100 each) and "Incomplete", then Mission Bonus, Kill Bonus (50 a
         * kill), Death Penalty (-50 a death) and Mission Total, filled by 0x100e35cc from the scoring 0x100df8e4); the
         * port lists the objectives in that rectangle, one a line */
        if (s->objtext[0]) text_left(s, out, 9, 0xfe, 29, 95, 321, 309, s->objtext);
        if (s->stats[0]) text_left(s, out, 9, 0xfe, 28, 344, 320, 420, s->stats);
    }
    for (int i = 0; i < s->nlist; i++) draw_list(s, out, &s->list[i]);
    if (s->nlay > 1) draw_layer(s, out, &s->lay[1], hover, 0);
    if (s->edit.on) {
        /* FUN_10013d9d: the text with '|' at the cursor */
        char t[80];
        snprintf(t, sizeof t, "%.*s|%s", s->edit.cursor, s->edit.text, s->edit.text + s->edit.cursor);
        text_left(s, out, 9, 0xfe, s->edit.x0, s->edit.y0, s->edit.x1, s->edit.y1, t);
    }
    if (s->box.on) text_left(s, out, 9, 0xfe, s->box.x0, s->box.y0, s->box.x1, s->box.y1, s->box.text);
}
