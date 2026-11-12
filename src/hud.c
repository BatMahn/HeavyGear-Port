#include "hud.h"

#include "shp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The HUD is a set of 30 windows (HGSIM 0x10020fc0). Each has a rectangle in a 320 x 200 layout (table 0x1010b800,
 * one per faction and resolution), scaled to the screen by the view transform, a hot spot (0x1010c160) and a draw
 * function. The shapes come from a name table (0x1010c520, 119 names per faction and resolution; the high-resolution
 * North set: 0 HNCOMPS0, 1 HNCOMPS1, 2 HCMPSWPT, 3 HCMPSTGT, 4 / 5 HNMSGBX0 / 1, 6 HNDAMGRD, 7-13 the damage parts,
 * 14 HNWEPNBX, 15-50 HNSPED00-35, 51-62 the reticles, 63-71 HWAYPNT0-8, 72-75 HOFF*, 76-79 HNTARGT1-4, 82 HCMPSFRN).
 * The rectangles and hot spots below are the high-resolution North set as the running original holds them at
 * 640 x 480 (window table 0x107f2040 / 0x107f1d60, read from the process); text positions are the window objects'.
 */
enum { N_SHAPES = 83 };

/* the radar's contact shapes (the radar mode's name table, 0x1011f110: vehicles by side, navs, the target) */
static const char *const radar_names[] = { "HRHFRGPN", "HRHENGPN", "HRHNEGPN", "HRHNAVPN", "HRHNAVPR",
                                           "HRHFRGPT", "HRHENGPT", "HRHNEGPT", "HRHNAVPT" };
enum { R_COUNT = 9 };

/* the window table (0x1010b800, 30 x 0x14 bytes per set: a pointer, then x0, y0, x1, y1 of the 320 x 200 layout)
 * and the hot spots (0x1010c160, 16.16 fractions of the window's size), sets 2 (North) and 3 (South) of the high
 * resolution, the one the port's 640 x 480 sim screen picks (0x100966ba: the nearest of 319 x 199, 639 x 479,
 * 1023 x 767; set = faction + resolution x 2, 0x10020d48) */
static const short win_tab[2][30][4] = {
    {{22, 9, 99, 69}, {259, 0, 319, 55}, {258, 145, 309, 179}, {261, 56, 317, 66}, {261, 68, 317, 78},
     {261, 80, 317, 90}, {261, 92, 317, 102}, {261, 104, 317, 114}, {261, 116, 317, 126}, {261, 128, 317, 138},
     {261, 140, 317, 150}, {25, 145, 52, 35}, {10, 183, 70, 199}, {10, 100, 260, 199}, {122, 0, 258, 97},
     {122, 13, 258, 69}, {30, 70, 300, 170}, {145, 190, 280, 199}, {0, 1, 35, 101}, {0, 188, 273, 199},
     {5, 185, 84, 199}, {0, 136, 177, 188}, {0, 95, 61, 129}, {0, 74, 80, 84}, {13, 0, 116, 92},
     {10, 136, 70, 145}, {0, 0, 20, 20}, {0, 145, 122, 179}, {0, 145, 183, 179}, {0, 145, 244, 179}},
    {{218, 9, 295, 69}, {0, 0, 60, 55}, {258, 145, 309, 179}, {2, 56, 58, 66}, {2, 68, 58, 78},
     {2, 80, 58, 90}, {2, 92, 58, 102}, {2, 104, 58, 114}, {2, 116, 58, 126}, {2, 128, 58, 138},
     {2, 140, 58, 150}, {25, 145, 52, 35}, {10, 183, 70, 199}, {10, 100, 260, 199}, {60, 0, 193, 97},
     {60, 15, 193, 71}, {30, 70, 300, 180}, {145, 190, 280, 199}, {282, 0, 319, 100}, {0, 188, 273, 199},
     {5, 185, 84, 199}, {0, 136, 177, 188}, {0, 95, 61, 129}, {0, 74, 80, 84}, {195, 2, 307, 92},
     {10, 136, 70, 145}, {0, 0, 20, 20}, {0, 145, 122, 179}, {0, 145, 183, 179}, {0, 145, 244, 179}},
};
static const unsigned short hot_tab[2][30][2] = {
    {{0, 0}, {0x3d71, 0xccd}, {0, 0}, {0x599a, 0x2666}, {0x599a, 0x2666}, {0x599a, 0x2666}, {0x599a, 0x2666},
     {0x599a, 0x2666}, {0x599a, 0x2666}, {0x599a, 0x2666}, {0x599a, 0x2666}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
     {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0x3333, 0x3333}, {0, 0x8000}, {0x3333, 0x4000}, {0, 0},
     {0x747b, 0x6ccd}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
    {{0, 0}, {0x3333, 0x199a}, {0, 0}, {0x599a, 0x2666}, {0x599a, 0x2666}, {0x599a, 0x2666}, {0x599a, 0x2666},
     {0x599a, 0x2666}, {0x599a, 0x2666}, {0x599a, 0x2666}, {0x599a, 0x2666}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
     {0, 0}, {0, 0}, {0, 0}, {0x4ccd, 0}, {0, 0}, {0x3333, 0x3333}, {0, 0x8000}, {0x3333, 0x4000}, {0, 0},
     {0x8ccd, 0x68f6}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
};

struct hud {
    int      wr[30][4];      /* the windows' rectangles at 640 x 480 (0x1007629f / 0x10075dd0) */
    int      hot[30][2];     /* their hot spots in pixels from the corner (0x10076149) */
    int      faction;        /* the player gear's +0x613: 0 North, 1 South */
    hg_shape s[N_SHAPES];
    hg_shape radar[R_COUNT];
    hg_shape bar;            /* HMESSBAR, the message bar (0x1011cf3c; HLESSBAR at low resolution) */
    hg_shape esc, slbar, slknb;   /* the escape screen's ESCAPSCR, the sliders' SLIDEBAR / SLIDEKNB (high resolution) */
    unsigned esc_cv, slbar_cv, slknb_cv;   /* their canvases (shape_canvas) */
    hg_font  font;          /* SERPDEUS, the high-resolution HUD font (0x1010e2e0 + 0x12) */
    gear     g;
    int      selected;
    hg_shape icon[GEAR_MAX_WEAPONS][2];   /* the weapon's icon and its damaged one (+0x10 in the weapon table) */
    uint8_t  remap[17][256];  /* 0x107c1100: the colour remap tables (0x10053946) */
    int      cx0, cy0, cx1, cy1;   /* the current window's rectangle: drawing is clipped to it */
    const uint8_t *rm;        /* the current remap table, or NULL */
    int      tpx, tpy;        /* 0x1014cab0 / 4: the target's projection as window 12's draw last left it (0, 0 at first) */
    uint8_t  idx[480][640];
    uint8_t  on[480][640];
};

static void load_shape(hg_archive *a, const char *name, hg_shape *s)
{
    size_t len;
    uint8_t *d = hga_load(a, name, "SHP", &len, NULL);
    if (!d || shp_parse(d, len, 0, s)) memset(s, 0, sizeof *s);
    free(d);
}

/* frame 0's first dword (0x100cf933): its canvas, width - 1 in the high word, height - 1 in the low */
static unsigned shape_canvas(hg_archive *a, const char *name)
{
    size_t len;
    uint8_t *d = hga_load(a, name, "SHP", &len, NULL);
    unsigned v = 0;
    if (d && len >= 16) {
        size_t o = (size_t)d[8] | (size_t)d[9] << 8 | (size_t)d[10] << 16 | (size_t)d[11] << 24;
        if (o + 4 <= len) v = (unsigned)d[o] | (unsigned)d[o + 1] << 8 | (unsigned)d[o + 2] << 16 | (unsigned)d[o + 3] << 24;
    }
    free(d);
    return v;
}

/* the name table's entries the port uses (index -> name; %c is the faction's letter) */
static const char *shape_name(int i)
{
    static const char *const fixed[N_SHAPES] = {
        [0] = "H%cCOMPS0", [1] = "H%cCOMPS1", [2] = "HCMPSWPT", [3] = "HCMPSTGT", [4] = "H%cMSGBX0", [5] = "H%cMSGBX1",
        [6] = "H%cDAMGRD", [7] = "H%cDBPACK", [8] = "H%cDDHEAD", [9] = "H%cDTORSO", [10] = "H%cDDRARM",
        [11] = "H%cDDLARM", [12] = "H%cDDRLEG", [13] = "H%cDDLLEG", [14] = "H%cWEPNBX",
        [51] = "H%cRPROJT", [53] = "H%cRPROBY", [55] = "H%cRLASER", [57] = "H%cRLABSY", [59] = "H%cRMISLE",
        [61] = "H%cRMISLK", [63] = "HWAYPNT0", [64] = "HWAYPNT1", [65] = "HWAYPNT2", [66] = "HWAYPNT3",
        [67] = "HWAYPNT4", [68] = "HWAYPNT5", [69] = "HWAYPNT6", [70] = "HWAYPNT7", [71] = "HWAYPNT8",
        [72] = "HOFFDOWN", [73] = "HOFFLEFT", [74] = "HOFFRIGH", [75] = "HOFFUPWD", [76] = "H%cTARGT1",
        [77] = "H%cTARGT2", [78] = "H%cTARGT3", [79] = "H%cTARGT4", [82] = "HCMPSFRN",
    };
    return fixed[i];
}

/* 0x1007629f / 0x10075dd0: frac = (v << 16) / 319 (x) or / 199 (y), pixel = frac x 639 (or 479) >> 16, rounded by
 * bit 15 */
static int lay_px(int v, int full, int div)
{
    int64_t p = (int64_t)(((int64_t)v << 16) / div) * full;
    return (int)(p >> 16) + (int)((p >> 15) & 1);
}

static int frac_of(int size, int frac);

hud *hud_load(hg_archive *a, int faction)
{
    hud *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    const char f = faction ? 'S' : 'N';
    const int set = faction ? 1 : 0;
    h->faction = set;
    for (int i = 0; i < 30; i++) {
        h->wr[i][0] = lay_px(win_tab[set][i][0], 639, 319);
        h->wr[i][1] = lay_px(win_tab[set][i][1], 479, 199);
        h->wr[i][2] = lay_px(win_tab[set][i][2], 639, 319);
        h->wr[i][3] = lay_px(win_tab[set][i][3], 479, 199);
        h->hot[i][0] = frac_of(h->wr[i][2] - h->wr[i][0], hot_tab[set][i][0]);
        h->hot[i][1] = frac_of(h->wr[i][3] - h->wr[i][1], hot_tab[set][i][1]);
    }
    char nm[16];
    for (int i = 0; i < N_SHAPES; i++) {
        const char *n = shape_name(i);
        if (i >= 15 && i <= 50) { snprintf(nm, sizeof nm, "H%cSPED%02d", f, i - 15); load_shape(a, nm, &h->s[i]); continue; }
        if (!n) continue;
        snprintf(nm, sizeof nm, n, f);
        load_shape(a, nm, &h->s[i]);
    }
    for (int i = 0; i < R_COUNT; i++) load_shape(a, radar_names[i], &h->radar[i]);
    load_shape(a, "HMESSBAR", &h->bar);
    load_shape(a, "ESCAPSCR", &h->esc);
    load_shape(a, "SLIDEBAR", &h->slbar);
    load_shape(a, "SLIDEKNB", &h->slknb);
    h->esc_cv = shape_canvas(a, "ESCAPSCR");
    h->slbar_cv = shape_canvas(a, "SLIDEBAR");
    h->slknb_cv = shape_canvas(a, "SLIDEKNB");
    size_t len;
    uint8_t *d = hga_load(a, "SERPDEUS", "FNT", &len, NULL);
    if (d) fnt_parse(d, len, &h->font);
    /* 0x10053946: tables 0..3 move the first colour group (0-7) onto groups 0..3; 4..8 the second (8-15) onto groups
     * 0..3 and colour 0; 9..12 the third (16-23); 13..16 the fourth (24-31). The rest of each table is 0. */
    for (int c = 0; c < 8; c++) for (int g = 0; g < 4; g++) h->remap[g][c] = (uint8_t)(c + 8 * g);
    for (int c = 8; c < 16; c++) { for (int g = 0; g < 4; g++) h->remap[4 + g][c] = (uint8_t)(c - 8 + 8 * g); h->remap[8][c] = 0; }
    for (int c = 16; c < 24; c++) for (int g = 0; g < 4; g++) h->remap[9 + g][c] = (uint8_t)(c - 16 + 8 * g);
    for (int c = 24; c < 32; c++) for (int g = 0; g < 4; g++) h->remap[13 + g][c] = (uint8_t)(c - 24 + 8 * g);
    return h;
}

void hud_set_gear(hud *h, hg_archive *a, const gear *g, int selected)
{
    for (int i = 0; i < GEAR_MAX_WEAPONS; i++) { shp_free(&h->icon[i][0]); shp_free(&h->icon[i][1]); }
    h->g = *g;
    h->selected = selected;
    for (int i = 0; i < g->nweapons; i++) {
        if (!g->weapon[i].icon) continue;
        load_shape(a, g->weapon[i].icon, &h->icon[i][0]);
        if (g->weapon[i].icon_hit) load_shape(a, g->weapon[i].icon_hit, &h->icon[i][1]);
    }
}

void hud_free(hud *h)
{
    if (!h) return;
    for (int i = 0; i < GEAR_MAX_WEAPONS; i++) { shp_free(&h->icon[i][0]); shp_free(&h->icon[i][1]); }
    for (int i = 0; i < N_SHAPES; i++) shp_free(&h->s[i]);
    for (int i = 0; i < R_COUNT; i++) shp_free(&h->radar[i]);
    shp_free(&h->bar);
    shp_free(&h->esc);
    shp_free(&h->slbar);
    shp_free(&h->slknb);
    fnt_free(&h->font);
    free(h);
}

void hud_state_simple(hud_state *st, float heading, float kph)
{
    memset(st, 0, sizeof *st);
    st->heading = heading;
    st->kph = kph;
    st->selected = 0;
    for (int i = 0; i < GEAR_MAX_WEAPONS; i++) st->ammo[i] = -1;
    st->ret_on = 1;
    st->ret_x = 320; st->ret_y = 239;
    st->ret_shape = HUD_RET_PROJ;
    st->tgt_contact = st->nav_contact = -1;
    st->radar_range = 200000;
    st->radar_mode = 1;
}

static void put(hud *h, int x, int y, uint8_t c)
{
    if (x < 0 || y < 0 || x >= 640 || y >= 480) return;
    if (x < h->cx0 || y < h->cy0 || x > h->cx1 || y > h->cy1) return;
    h->idx[y][x] = h->rm ? h->rm[c] : c;
    h->on[y][x] = 1;
}

static void window(hud *h, int x0, int y0, int x1, int y1) { h->cx0 = x0; h->cy0 = y0; h->cx1 = x1; h->cy1 = y1; }

/* a shape with its hot spot at (x, y) (0x100cb3b0: the shape's own x0 / y0 place it) */
static void blit(hud *h, const hg_shape *s, int x, int y)
{
    for (int j = 0; j < s->h; j++)
        for (int i = 0; i < s->w; i++)
            if (s->mask[(size_t)j * s->w + i]) put(h, x + s->x0 + i, y + s->y0 + j, s->px[(size_t)j * s->w + i]);
}

/* a shape turned clockwise by deg about its hot spot, placed at (x, y) (0x1005575e; nearest texel) */
static void blit_rot(hud *h, const hg_shape *s, int x, int y, float deg)
{
    if (!s->px) return;
    float r = deg * 3.14159265f / 180.0f, c = cosf(r), sn = sinf(r);
    int rad = (int)ceilf(sqrtf((float)((s->w + abs(s->x0)) * (s->w + abs(s->x0)) + (s->h + abs(s->y0)) * (s->h + abs(s->y0))))) + 2;
    for (int dy = -rad; dy <= rad; dy++)
        for (int dx = -rad; dx <= rad; dx++) {
            /* screen y grows downward: clockwise on screen; sample the source by the inverse turn */
            float sx = dx * c + dy * sn, sy = -dx * sn + dy * c;
            int i = (int)floorf(sx + 0.5f) - s->x0, j = (int)floorf(sy + 0.5f) - s->y0;
            if (i < 0 || j < 0 || i >= s->w || j >= s->h || !s->mask[(size_t)j * s->w + i]) continue;
            put(h, x + dx, y + dy, s->px[(size_t)j * s->w + i]);
        }
}

static void line(hud *h, int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        put(h, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

static int text_width(const hud *h, const char *str)
{
    int w = 0;
    for (; *str; str++) {
        int ch = (unsigned char)*str;
        if (ch < h->font.count && h->font.glyph && h->font.glyph[ch]) w += h->font.width[ch];
    }
    return w;
}

/* text through the font's colour table: glyph byte 0x0e is the colour, the rest map to themselves (DAT_107ca800) */
static int text(hud *h, int x, int y, const char *str, uint8_t colour)
{
    const hg_font *f = &h->font;
    if (!f->glyph) return x;
    for (; *str; str++) {
        int ch = (unsigned char)*str;
        if (ch >= f->count || !f->glyph[ch]) continue;
        int w = f->width[ch];
        for (int j = 0; j < f->height; j++)
            for (int i = 0; i < w; i++) {
                uint8_t v = f->glyph[ch][j * w + i];
                if (v == 0xff) continue;
                put(h, x + i, y + j, v == 0x0e ? colour : v);
            }
        x += w;
    }
    return x;
}

/* the objective in the message box (0x100b1c8f): the label in colour 4, then the text in 0x0c; text longer than 20
 * characters (0x10123db8, high resolution) breaks at its last space within 21 and goes on below, from the same x */
static void objective_text(hud *h, int x, int y, const char *label, const char *body)
{
    x = text(h, x, y, label, 4);
    size_t n = strlen(body);
    if ((int)n <= 20) { text(h, x, y, body, 0x0c); return; }
    char a[256], b[256];
    int cut = 21;
    if (body[cut] != ' ') {
        int sp = -1;
        for (int i = 0; i < cut && body[i]; i++) if (body[i] == ' ') sp = i;
        if (sp >= 0) cut = sp;
    }
    snprintf(a, sizeof a, "%.*s", cut + 1, body);
    snprintf(b, sizeof b, "%s", body + cut + 1);
    text(h, x, y, a, 0x0c);
    text(h, x, y + h->font.height, b, 0x0c);
}

/* DISPLAY_OBJECTIVES (0x100b21ce / 0x100b15fb) in window 13: (10, 100) - (260, 199) of the 320 x 200 layout, so
 * (20, 241) - (521, 479) here (0x1007629f / 0x10075dd0: v << 16 / 319 or / 199, times 639 or 479, rounded). Its text
 * position is the window's hot spot (0x10020d48 copies the hot spots to 0x107f1d60, 0x1001e5c5 keeps them as the
 * window's +0xf0): window 13's is (0, 0), so the text starts at the window's corner */
static void objectives_list(hud *h, const hud_state *st)
{
    const int x0 = h->wr[13][0] + h->hot[13][0], y0 = h->wr[13][1] + h->hot[13][1], x1 = h->wr[13][2];
    const int fh = h->font.height, half = fh / 2;
    int y = y0;
    window(h, h->wr[13][0], h->wr[13][1], h->wr[13][2], h->wr[13][3]);
    int tx = text(h, x0, y, "MISSION OBJECTIVES", 0x14);
    /* 0x10076a77: a line one font height down, from x to x - 1 + the glyphs' widths, in the title's colour */
    line(h, x0, y + fh, tx - 1, y + fh, 0x14);
    y += fh + half;
    static const int cats[5] = {1, 2, 4, 0, 8};
    for (int c = 0; c < 5; c++) {
        for (int i = 0; i < st->nobjl; i++) {
            if (st->objl[i].order != cats[c] || !cats[c]) continue;   /* order 0 never shows (0x100b15fb) */
            const char *lab = cats[c] == 1 ? "Primary: " : cats[c] == 8 ? "Return: " : "Secondary: ";
            text(h, x0, y, lab, 0x14);
            /* the text after the width of "Secondary: " (0x10187344), wrapped at 32 characters */
            int x = x0 + text_width(h, "Secondary: ");
            const char *t = st->objl[i].text;
            int wrapped = 0;
            if (strlen(t) < 0x20) text(h, x, y, t, 0x0c);
            else {
                char a[64], b[256];
                int cut = 0x20;
                if (t[0x21] != ' ') { for (int k = 0x20; k > 0; k--) if (t[k] == ' ') { cut = k; break; } }
                snprintf(a, sizeof a, "%.*s", cut + 1, t);
                snprintf(b, sizeof b, "%s", t + cut + 1);
                text(h, x, y, a, 0x0c);
                text(h, x, y + fh, b, 0x0c);
                wrapped = 1;
            }
            const char *ss = st->objl[i].state == 5 ? "Complete" : st->objl[i].state == 6 ? "InComplete" : "In progress";
            int sc = st->objl[i].state == 5 ? 0x17 : st->objl[i].state == 6 ? 7 : 0x0c;
            text(h, x1 - text_width(h, ss), y, ss, (uint8_t)sc);
            y += fh * (1 + wrapped);
        }
        if (c != 2) y += half;   /* the gaps: after 1, 2, and 4 + 0 together */
    }
    y += fh;
    if (st->time_line) text(h, x0, y, st->time_line, 0x14);
    window(h, 0, 0, 639, 479);
}

/* a 16.16 fraction of a window's size, as 0x10076149 rounds it */
static int frac_of(int size, int frac)
{
    int64_t p = (int64_t)size * frac;
    return (int)(p >> 16) + (int)((p >> 15) & 1);
}

/* a radio menu (0x100636da; its layout set by 0x10062baa). The radio channels' menu records (0x1011cbe0: 1, 2, 7..10
 * the squad mates', 3 the mission channel) all draw in window 15 ((122, 13) - (258, 69): (244, 31) - (517, 166) here),
 * flags 0xa, 8 rows, colours 0x0f (+0x4c) and 0x0f selected (+0x50), no selection marker; positions as fractions of
 * the window: the title at (0.05, half a row), the numbers and the items' text from (0.05, two rows), a row being
 * 1 / (rows + 2) of the window's height. The title is underlined in 0x1c (0x10076a77). An item: its number (itoa: 1,
 * 2 ... counting the items that aren't spacers; the exit item 0, restarting the count) at the number column, its text
 * 2 x the width of '0' + the width of '.' to the right; a text of 27 characters or more (0x1011cbd8, high resolution)
 * breaks at character 28 if that is a space, else after the last space in the first 29, its rest (int)(fh / 1.5)
 * lower in the same row. Rows step by the row height (the exit item would move to the last row without flag 8). */
static void radio_menu(hud *h, const hud_state *st)
{
    const int wx0 = h->wr[15][0], wy0 = h->wr[15][1], ww = h->wr[15][2] - wx0, wh = h->wr[15][3] - wy0, rows = 8;
    const int fh = h->font.height;
    int rowf = 0x10000 / (rows + 2);
    int tx = wx0 + frac_of(ww, 0xccd), ty = wy0 + frac_of(wh, (int)(((int64_t)rowf << 16) / 0x20000));
    int nx = wx0 + frac_of(ww, 0xccd), iy = wy0 + frac_of(wh, rowf * 2), rowh = frac_of(wh, rowf);
    int textoff = 2 * text_width(h, "0") + text_width(h, ".");
    const char *title = st->menu_title;
    int n = st->nmenu;
    window(h, wx0, wy0, h->wr[15][2], h->wr[15][3]);
    if (title) {
        int e = text(h, tx, ty, title, 0x0f);
        line(h, tx, ty + fh, e - 1, ty + fh, 0x1c);
    }
    int count = 0, y = iy;
    for (int i = 0; i < n && i < HUD_MENU_MAX; i++, y += rowh) {
        int type = st->menu[i].type;
        const char *t = st->menu[i].text;
        int x = nx;
        if (type != HUD_MENU_SPACER) {
            char num[12];
            if (type == HUD_MENU_EXIT) count = 0; else count++;
            snprintf(num, sizeof num, "%d", count);
            text(h, nx, y, num, 0x0f);
        }
        if (!t) continue;
        x = nx + textoff;
        size_t len = strlen(t);
        if (len < 27) { text(h, x, y, t, 0x0f); continue; }
        int cut = 27, last = len > 28 ? 28 : (int)len - 1;
        if ((len > 28 ? t[28] : 0) != ' ') { for (int k = last; k >= 0; k--) if (t[k] == ' ') { cut = k; break; } }
        char a[64];
        snprintf(a, sizeof a, "%.*s", cut + 1, t);
        text(h, x, y, a, 0x0f);
        text(h, x, (int)((float)y + (float)fh / 1.5f), t + cut + 1, 0x0f);
    }
    window(h, 0, 0, 639, 479);
}

/* 16.16 x (rounded, as 0x10075dd0 / 0x10076501) */
static int fx_mul(int a, int b)
{
    int64_t p = (int64_t)a * b;
    return (int)(p >> 16) + (int)((p >> 15) & 1);
}

/* The escape screen (radio menu record 5, 0x10118bb0; laid out by 0x10062baa with its record number 5, drawn by
 * 0x100636da): the window (0x10118b80, fractions of the screen: (0x3ae1, 0x30a4) - (0x10000, 0xb333), 0x10075dd0 ->
 * (147, 91) - (639, 335)); its background ESCAPSCR, the rect (0, 0) - (1, 1) scaled about its centre by the shape's
 * canvas over the screen's size (0x10076501), made pixels, then moved across to the window's left (flag 0x10); rows
 * 6; the title at (0x2e14 of the width, half a row), the numbers at 0x170a, the texts 2 x width('0') + width('.')
 * further, the handler's column at 0x851f, every column's y two rows down (+0x7c: 0); colours 0x0c (+0x4c), the
 * selected item 0x07 (+0x50); flags 0x11: no 4 (the title underlined in 0x1c, 0x10076a77), no 8 (the exit item goes
 * to the last row). The font is the HUD's (0x10062e8e sets +0x44 = DAT_107ca7e0). A slider item (0x100645c0) draws
 * SLIDEBAR at (its column / 3, the row's y + the font height) and SLIDEKNB on it at + value x (the bar's canvas
 * width - 1) - (the knob's canvas width - 1) / 2; a choice item (0x10064deb) its string at the column. */
static void escape_menu(hud *h, const hud_state *st)
{
    const int sw = 639, sh = 479;   /* DAT_107ee494 / DAT_107ee460 */
    int wx0 = fx_mul(sw, 0x3ae1), wy0 = fx_mul(sh, 0x30a4), wx1 = fx_mul(sw, 0x10000), wy1 = fx_mul(sh, 0xb333);
    if (h->esc.px) {
        int cw = (int)(h->esc_cv >> 16), ch = (int)(h->esc_cv & 0xffff);
        int sx = (int)((((int64_t)cw << 16) / (sw + 1) << 16) / 0x10001);
        int sy = (int)((((int64_t)(ch << 16) & 0xffffffffLL) / (sh + 1) << 16) / 0x10001);
        int c = 0x10001 >> 1;
        int bx0 = fx_mul(sw, fx_mul(-c, sx) + c), by0 = fx_mul(sh, fx_mul(-c, sy) + c);
        bx0 -= bx0 - wx0;
        window(h, 0, 0, 639, 479);
        blit(h, &h->esc, bx0, by0);
    }
    const int rows = 6, fh = h->font.height, ww = wx1 - wx0, wh = wy1 - wy0;
    int rowf = 0x10000 / (rows + 2);
    int rowh = frac_of(wh, rowf);
    int tx = wx0 + frac_of(ww, 0x2e14), ty = wy0 + frac_of(wh, (int)(((int64_t)rowf << 16) / 0x20000));
    int nx = wx0 + frac_of(ww, 0x170a), iy = wy0 + frac_of(wh, rowf * 2), hx = frac_of(ww, 0x851f);
    int textoff = 2 * text_width(h, "0") + text_width(h, ".");
    window(h, wx0, wy0, wx1, wy1);
    if (st->menu_title) {
        int e = text(h, tx, ty, st->menu_title, 0x0c);
        line(h, tx, ty + fh, e - 1, ty + fh, 0x1c);
    }
    int count = 0, y = iy;
    for (int i = 0; i < st->nmenu && i < HUD_MENU_MAX; i++, y += rowh) {
        int type = st->menu[i].type;
        uint8_t col = i == st->menu_sel ? 0x07 : 0x0c;
        if (type == HUD_MENU_EXIT) y += (rows - i - 1) * rowh;   /* no flag 8: the exit item to the last row */
        if (type != HUD_MENU_SPACER) {
            char num[12];
            if (type == HUD_MENU_EXIT) count = 0; else count++;
            snprintf(num, sizeof num, "%d", count);
            text(h, nx, y, num, col);
        }
        if (st->menu[i].text) text(h, nx + textoff, y, st->menu[i].text, col);
        if (st->menu[i].kind == HUD_MENU_SLIDER && h->slbar.px) {
            int bx = wx0 + hx / 3, by = y + fh;
            blit(h, &h->slbar, bx, by);
            int v = st->menu[i].value < 0 ? 0 : st->menu[i].value > 0x10000 ? 0x10000 : st->menu[i].value;
            int kx = bx + fx_mul(v, (int)(h->slbar_cv >> 16)) - ((int)(h->slknb_cv >> 16) >> 1);
            blit(h, &h->slknb, kx, by);
        } else if (st->menu[i].kind == HUD_MENU_CHOICE && st->menu[i].vtext) {
            text(h, wx0 + hx, y, st->menu[i].vtext, col);
        }
    }
    window(h, 0, 0, 639, 479);
}

/* the message bar (0x10069f10 / 0x1006a27f): slot 0's rectangle is HMESSBAR's size (640 x 20) moved to the screen's
 * top-left; the bar drawn at its corner, then the text at (0x28f of its width: 6, (its height + 1 - the font's
 * height) / 2) in colour 0xfe. The sim shows a message for its ticks (0x1006a0ec: "Exiting Mission Area..." 0x1536). */
static void message_bar(hud *h, const char *msg)
{
    const int bw = 637, bhgt = 18;
    blit(h, &h->bar, 0, 0);
    window(h, 0, 0, bw, bhgt);
    text(h, frac_of(bw, 0x28f), (bhgt + 1 - h->font.height) / 2, msg, 0xfe);
    window(h, 0, 0, 639, 479);
}

void hud_draw(const hud *hc, uint32_t *color, int w, int h_, const uint8_t (*pal)[3], const hud_state *st)
{
    hud *h = (hud *)hc;
    memset(h->on, 0, sizeof h->on);
    h->rm = NULL;
    window(h, 0, 0, 639, 479);
    /* the windows draw before the nav marker (0x10021519: the windows' +0x13c, then 0x100538b0 -> 0x10054ae9); window
     * 12's (0x100acc00) keeps the target's projection, in or out of view, whenever it has a target; without one the
     * last stays */
    if (st->tgt) { h->tpx = (int)st->tgt_x; h->tpy = (int)st->tgt_y; }
    char buf[64];
    /* the compass reads the heading the other way round: 360 - the engine's heading (mission 2 starts at -51 and reads
     * 50, mission 10 at 173 and reads 187, in the running original) */
    float hd = fmodf(-st->heading, 360.0f);
    if (hd < 0) hd += 360.0f;

    /* the radar (0x10087b59, window 0 at (44, 22) - (198, 166)): a top-down view centred on the gear, its heading up,
     * the mode's range (200000 in mode 1, the cockpit's) across the window's width. Its view lines (0x10088be7) run
     * from the centre at the view's half angle each side in colour 0x1c; then the player's group's shown navs
     * (0x100889c9: HRHNAVPN, HRHNAVPR once visited), the vehicles by side (0x100882b3: HRHFRGPN, HRHENGPN, HRHNEGPN),
     * and the target / the selected nav highlighted (0x100885f3). Each point passes the mode's clip (the mode record's
     * +0x80, 0x1011eff8[4] = 0x10077c95): relative to the window, dx = x - ((w + 1) >> 1), dy = (int)((y - ((h + 1) >> 1))
     * / the camera's aspect (1.0)), kept while dx^2 + dy^2 <= ((w + 1) / 2 - 1)^2 (w = x1 - x0 = 154: radius 76). The view
     * lines end on the circle one smaller (0x10077e2f -> 0x10077fc6: (w + 1) / 2 - 2 = 75). */
    /* the large radar (mode 2, 0x1011f430): its rectangle 0x2148 .. 0xdeb8 of the screen's width (16.16), all its
     * height; the map (mode 4, 0x1011f4d8): the navs and the target / nav marks at their projections */
    int big[4] = {frac_of(640, 0x2148), 0, frac_of(640, 0xdeb8), 479};
    if (st->radar_mode == 4) {
        window(h, 0, 0, 639, 479);
        for (int i = 0; i < st->ncontacts; i++) {
            int k = st->contact[i].kind, hl = i == st->tgt_contact || i == st->nav_contact;
            if (!hl && k != HUD_RADAR_NAV && k != HUD_RADAR_NAV_SEEN) continue;   /* 0x100889c9, 0x100885f3 */
            const hg_shape *s = &h->radar[k];
            if (hl) s = k <= HUD_RADAR_NEUTRAL ? &h->radar[5 + k] : &h->radar[8];
            blit(h, s, (int)st->contact[i].x, (int)st->contact[i].z);
        }
    } else if (!st->hud_off && (st->radar_mode == 1 || st->radar_mode == 2)) {
        const int *r = st->radar_mode == 2 ? big : h->wr[0];
        int cx = (r[0] + 1 + r[2]) >> 1, cy = (r[1] + 1 + r[3]) >> 1;
        const int rclip = (r[2] - r[0] + 1) / 2 - 1, rline = rclip - 1;
        float scale = (float)(r[2] - r[0] + 1) / (st->radar_range > 0 ? st->radar_range : 200000.0f);
        window(h, r[0], r[1], r[2], r[3]);
        for (int s = -1; s <= 1; s += 2) {
            float a = s * 45.0f * 3.14159265f / 180.0f;
            line(h, cx, cy, cx + (int)(sinf(a) * rline), cy - (int)(cosf(a) * rline), 0x1c);
        }
        for (int i = 0; i < st->ncontacts; i++) {
            float x = st->contact[i].x * scale, y = -st->contact[i].z * scale;
            int hl = i == st->tgt_contact || i == st->nav_contact;
            int dx = (int)x, dy = (int)y;
            if (dx * dx + dy * dy > rclip * rclip) continue;
            int k = st->contact[i].kind;
            const hg_shape *s = &h->radar[k];
            if (hl) s = k <= HUD_RADAR_NEUTRAL ? &h->radar[5 + k] : &h->radar[8];
            blit(h, s, cx + (int)x, cy + (int)y);
        }
        window(h, 0, 0, 639, 479);
    }

    if (!st->hud_off) {
    /* compass, window 24 at (26, 0), hot spot (94, 94) (0x10055924): the ring HNCOMPS1 turned clockwise by that reading
     * (so the letter of the direction faced is at the top), the frame HNCOMPS0, the selected nav's marker HCMPSWPT
     * and the target's (HCMPSTGT for an enemy, HCMPSFRN otherwise) turned to their bearings, then the heading in
     * colour 0x0c at (165, 13) */
    {
        int x0 = h->wr[24][0], y0 = h->wr[24][1], hx = x0 + h->hot[24][0], hy = y0 + h->hot[24][1];
        blit_rot(h, &h->s[1], hx, hy, hd);
        blit(h, &h->s[0], x0, y0);
        if (st->nav) blit_rot(h, &h->s[2], hx, hy, st->nav_bearing);
        if (st->tgt) blit_rot(h, &h->s[st->tgt_side == 1 ? 3 : 82], hx, hy, st->tgt_bearing);
        snprintf(buf, sizeof buf, "%d", (int)hd % 360);
        /* the heading's place (0x10040b50: the window's +0xac / +0xb0, North 0xcccd / 0xf5c, South 0x11ec / 0xa3d) */
        int w = h->wr[24][2] - x0, ht = h->wr[24][3] - y0;
        text(h, x0 + frac_of(w, h->faction ? 0x11ec : 0xcccd), y0 + frac_of(ht, h->faction ? 0xa3d : 0xf5c), buf, 0x0c);
    }

    /* message box (0x100b26e6): HNMSGBX1 in window 15 at (244, 31), slid up to y -195 while closed (DAT_1014cb04; it
     * slides down to half that while a radio menu is open, 0x1014cb0c), then HNMSGBX0 at window 14's corner (244, 0);
     * the current objective at the window's text position (0x10040bd1: +0xb4 / +0xb8 = 0xa3d / 0xccd of the window's
     * 273 x 233, rounded: (11, 12)), drawn whether a menu is open or not */
    blit(h, &h->s[5], h->wr[15][0], st->box_open ? h->wr[15][1] - 195 / 2 : h->wr[15][1] - 195);
    blit(h, &h->s[4], h->wr[14][0], h->wr[14][1]);
    if (st->obj_label && st->obj_text) {
        const int *r = h->wr[14];
        window(h, r[0], r[1], r[2], r[3]);
        objective_text(h, r[0] + frac_of(r[2] - r[0], 0xa3d), r[1] + frac_of(r[3] - r[1], 0xccd), st->obj_label, st->obj_text);
        window(h, 0, 0, 639, 479);
    }
    if (st->box_open) { if (st->menu_style == 1) escape_menu(h, st); else radio_menu(h, st); }

    if (st->objlist) objectives_list(h, st);

    /* the nav marker (0x100545f7): the selected nav's point, if in view, as HWAYPNT0..6 in turn (one frame a draw),
     * remap 5 (as drawn), 7 once visited; its label "Way Point:<name>" (colour 0x1c) and "Range:<m>m" or "<km>k"
     * (0x0f) in a box of colour 0x0c beside it (0x10054ae9): the text at ((int)(x + 20), (int)(y - 45)) (0x101011fc /
     * 0x10101200), the second line a font height down; the box from 2 left / up of it, as wide as the longer line + 2
     * and two font heights + 2 high, and a line from its bottom-left corner to ((int)x, (int)y - 10). Out of view, or
     * within 30 pixels both ways of the target's projection (0x1014cab0 / 0x1014cab4), the label goes to window 20 at
     * (10, 445): the lines at its (0, 0) and (0, 15). */
    if (st->nav) {
        char l1[64], l2[64];
        snprintf(l1, sizeof l1, "Way Point:%s", st->nav_name ? st->nav_name : "");
        if (st->nav_range <= 1000.0f) snprintf(l2, sizeof l2, "Range: %3.0fm", st->nav_range);
        else snprintf(l2, sizeof l2, "Range: %2.2fk", st->nav_range / 1000.0f);
        /* 0x10054bff: against 0x1014cab0 / 4, the target's projection as window 12's draw (0x100acc00) last left it */
        int near_tgt = abs((int)(st->nav_x - (float)h->tpx)) <= 30 && abs((int)(st->nav_y - (float)h->tpy)) <= 30;
        if (st->nav_in_view && !near_tgt) {
            int x = (int)st->nav_x, y = (int)st->nav_y;
            h->rm = h->remap[st->nav_seen ? 7 : 5];
            blit(h, &h->s[63 + st->frame % 7], x, y);
            h->rm = NULL;
            int tx = (int)(st->nav_x + 20.0f), ty = (int)(st->nav_y - 45.0f), fh = h->font.height;
            text(h, tx, ty, l1, 0x1c);
            text(h, tx, ty + fh, l2, 0x0f);
            int bw = text_width(h, l1), b2 = text_width(h, l2);
            if (b2 > bw) bw = b2;
            bw += 2;
            int bh = fh * 2 + 2;
            line(h, tx - 2, ty - 2, tx - 2 + bw, ty - 2, 0x0c);
            line(h, tx - 2 + bw, ty - 2, tx - 2 + bw, ty - 2 + bh, 0x0c);
            line(h, tx - 2 + bw, ty - 2 + bh, tx - 2, ty - 2 + bh, 0x0c);
            line(h, tx - 2, ty - 2 + bh, tx - 2, ty - 2, 0x0c);
            line(h, tx - 2, ty - 2 + bh, x, y - 10, 0x0c);
        } else {
            const int *r = h->wr[20];
            window(h, r[0], r[1], r[2], r[3]);
            text(h, r[0], r[1], l1, 0x0f);
            text(h, r[0], r[1] + 15, l2, 0x0f);
            window(h, 0, 0, 639, 479);
        }
    }

    /* the target (0x10055e60): in view, the four corners HNTARGT1..4 above, left, right and below its middle at
     * the projected standing height (no less than 0x51f / 65536 of the view), through remap 0 / 1 / 2 by side (enemy
     * red, friend green, neutral blue); out of view an arrow at the screen's edge toward it (HOFFLEFT / HOFFRIGH /
     * HOFFUPWD / HOFFDOWN) through remap 9 / 10 / 11 */
    if (st->tgt) {
        if (st->tgt_in_view) {
            int x = (int)st->tgt_x, y = (int)st->tgt_y, s = (int)st->tgt_size;
            if (s < 8) s = 8;
            h->rm = h->remap[st->tgt_side == 0 ? 1 : st->tgt_side == 2 ? 2 : 0];
            blit(h, &h->s[76], x, y - s);
            blit(h, &h->s[79], x, y + s);
            blit(h, &h->s[78], x + s, y);
            blit(h, &h->s[77], x - s, y);
        } else {
            int x = (int)st->tgt_x, y = (int)st->tgt_y, sh;
            if (x < 0) { x = 0; sh = 73; }
            else if (x > 639) { x = 639; sh = 74; }
            else if (y < 0) { y = 0; sh = 75; }
            else { y = 479; sh = 72; }
            if (y < 0) y = 0;
            if (y > 479) y = 479;
            h->rm = h->remap[st->tgt_side == 0 ? 10 : st->tgt_side == 2 ? 11 : 9];
            blit(h, &h->s[sh], x, y);
        }
        h->rm = NULL;
    }

    /* the reticle (0x1005413c) at the aim point's projection (0x10055de4), its shape by the selected weapon */
    if (st->ret_on && st->ret_shape >= 51 && st->ret_shape <= 62) blit(h, &h->s[st->ret_shape], (int)st->ret_x, (int)st->ret_y);

    /* damage display, window 1 at (519, 0), hot spot (29, 7) (0x1009f290): the grid, then the seven parts, each
     * through a remap by its zone's damage level, 15 - (int)((internal / 2 + armor / 2) x 3 / zone +0x38): 0 -> table
     * 5 (as drawn), 1-7 -> 7, 8-14 -> 4, 15 -> 8 (a 15 with internal points left reads 14). Parts: backpack 7, head 1,
     * torso 2, right arm 3, left arm 4, right leg 5, left leg 6 (0x10122d88) */
    blit(h, &h->s[6], h->wr[1][0], h->wr[1][1]);
    {
        static const int zone_of[7] = {7, 1, 2, 3, 4, 5, 6};
        for (int i = 0; i < 7; i++) {
            int lvl = 0, z = zone_of[i] - 1;
            if (st->armor && st->internal && st->ztotal && st->ztotal[z] > 0) {
                lvl = 15 - (int)((st->internal[z] * 0.5f + st->armor[z] * 0.5f) * 3.0f / (float)st->ztotal[z]);
                if (lvl < 1) lvl = 0;
                if (lvl > 15) lvl = 15;
                if (lvl == 15 && st->internal[z] > 0.0f) lvl = 14;
            }
            h->rm = h->remap[lvl == 15 ? 8 : lvl == 0 ? 5 : lvl < 8 ? 7 : 4];
            blit(h, &h->s[7 + i], h->wr[1][0] + h->hot[1][0], h->wr[1][1] + h->hot[1][1]);
        }
        h->rm = NULL;
    }

    /* speed, window 18 at (0, 2) (0x100b336b -> 0x10041c28): HNSPED00, then the level shape for the throttle
     * setting, (int)(control +8 x 11 / (1/128)) = throttle x 22 (0..21); the speed "%3d" = |velocity| x 6 x 1.38
     * in colour 0x0c (0x14 going backward) at the window's hot spot, "Kph" below */
    {
        int lvl = (int)(st->throttle * 22.0f);
        if (lvl < 0) lvl = 0;
        if (lvl > 21) lvl = 21;
        int x0 = h->wr[18][0], y0 = h->wr[18][1], tx = x0 + h->hot[18][0], ty = y0 + h->hot[18][1];
        blit(h, &h->s[15], x0, y0);
        blit(h, &h->s[15 + lvl], x0, y0);
        int kph = (int)st->kph;
        snprintf(buf, sizeof buf, "%3d", st->backward ? -kph : kph);
        uint8_t c = st->backward ? 0x14 : 0x0c;
        text(h, tx, ty, buf, c);
        text(h, tx, ty + h->font.height, "Kph", c);
    }

    /* weapon panels, windows 3..10 at (523, 135 + 29k): one per weapon slot (0x100b9e60). HNWEPNBX at the corner, the
     * weapon's icon at the hot spot (39, 4) (high resolution only; its damaged icon once its zone is destroyed), the
     * ammunition (when above 0) at (8, 0) and the short name at (8, 10) in colour 0x0f (a slot of kind 1, subkind 0);
     * the selected slot (gear block +0x4fc, 0 at the start) gets its window's border in that colour (0x10076871). */
    for (int k = 0; k < h->g.nweapons && k < 8; k++) {
        const gear_weapon *wp = &h->g.weapon[k];
        const int *r = h->wr[3 + k];
        int x = r[0], y = r[1], x1 = r[2], y1 = r[3];
        window(h, x, y, x1, y1);   /* each window draws into its own rectangle */
        blit(h, &h->s[14], x, y);
        const hg_shape *ic = &h->icon[k][st->gone[k] && h->icon[k][1].px ? 1 : 0];
        if (ic->px) blit(h, ic, x + h->hot[3 + k][0], y + h->hot[3 + k][1]);
        int ammo = st->ammo[k] >= 0 ? st->ammo[k] : wp->ammo;
        if (ammo > 0) { snprintf(buf, sizeof buf, "%d", ammo); text(h, x + 8, y + 0, buf, 0x0f); }
        if (wp->name) text(h, x + 8, y + 10, wp->name, 0x0f);
        if (k == st->selected) {
            for (int i = x; i <= x1; i++) { put(h, i, y, 0x0f); put(h, i, y1, 0x0f); }
            for (int j = y; j <= y1; j++) { put(h, x, j, 0x0f); put(h, x1, j, 0x0f); }
        }
    }
    }
    window(h, 0, 0, 639, 479);
    if (st->msg && st->msg[0]) message_bar(h, st->msg);
    /* 0x10056944: the line centred (half the screen's width less half its width), three font heights above the
     * screen's bottom, colour 7 (the colour table's 0x0e) */
    if (st->warn && st->warn[0])
        text(h, 320 - (int)(text_width(h, st->warn) * 0.5), 479 - 3 * h->font.height, st->warn, 7);
    /* composite, scaled from 640 x 480 */
    for (int y = 0; y < h_; y++) {
        int sy = y * 480 / h_;
        for (int x = 0; x < w; x++) {
            int sx = x * 640 / w;
            if (!h->on[sy][sx]) continue;
            const uint8_t *p = pal[h->idx[sy][sx]];
            color[(size_t)y * w + x] = 0xff000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }
}
