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

struct hud {
    hg_shape s[N_SHAPES];
    hg_shape radar[R_COUNT];
    hg_font  font;          /* SERPDEUS, the high-resolution HUD font (0x1010e2e0 + 0x12) */
    gear     g;
    int      selected;
    hg_shape icon[GEAR_MAX_WEAPONS][2];   /* the weapon's icon and its damaged one (+0x10 in the weapon table) */
    uint8_t  remap[17][256];  /* 0x107c1100: the colour remap tables (0x10053946) */
    int      cx0, cy0, cx1, cy1;   /* the current window's rectangle: drawing is clipped to it */
    const uint8_t *rm;        /* the current remap table, or NULL */
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

hud *hud_load(hg_archive *a, int faction)
{
    hud *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    const char f = faction ? 'S' : 'N';
    char nm[16];
    for (int i = 0; i < N_SHAPES; i++) {
        const char *n = shape_name(i);
        if (i >= 15 && i <= 50) { snprintf(nm, sizeof nm, "H%cSPED%02d", f, i - 15); load_shape(a, nm, &h->s[i]); continue; }
        if (!n) continue;
        snprintf(nm, sizeof nm, n, f);
        load_shape(a, nm, &h->s[i]);
    }
    for (int i = 0; i < R_COUNT; i++) load_shape(a, radar_names[i], &h->radar[i]);
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
 * (20, 240) - (520, 477) here; INFERRED: its text position (11, 12) as the message box's (window 13's isn't read) */
static void objectives_list(hud *h, const hud_state *st)
{
    const int x0 = 20, x1 = 520, fh = h->font.height, half = fh / 2;
    int y = 240 + 12;
    window(h, x0, 240, x1, 477);
    int tx = text(h, x0 + 11, y, "MISSION OBJECTIVES", 0x14);
    for (int x = x0 + 11; x < tx; x++) put(h, x, y + fh, 0x14);   /* 0x10076a77: the title underlined (INFERRED) */
    y += fh + half;
    static const int cats[5] = {1, 2, 4, 0, 8};
    for (int c = 0; c < 5; c++) {
        for (int i = 0; i < st->nobjl; i++) {
            if (st->objl[i].order != cats[c] || !cats[c]) continue;   /* order 0 never shows (0x100b15fb) */
            const char *lab = cats[c] == 1 ? "Primary: " : cats[c] == 8 ? "Return: " : "Secondary: ";
            text(h, x0 + 11, y, lab, 0x14);
            /* the text after the width of "Secondary: " (0x10187344), wrapped at 32 characters */
            int x = x0 + 11 + text_width(h, "Secondary: ");
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
    if (st->time_line) text(h, x0 + 11, y, st->time_line, 0x14);
    window(h, 0, 0, 639, 479);
}

void hud_draw(const hud *hc, uint32_t *color, int w, int h_, const uint8_t (*pal)[3], const hud_state *st)
{
    hud *h = (hud *)hc;
    memset(h->on, 0, sizeof h->on);
    h->rm = NULL;
    window(h, 0, 0, 639, 479);
    char buf[64];
    /* the compass reads the heading the other way round: 360 - the engine's heading (mission 2 starts at -51 and reads
     * 50, mission 10 at 173 and reads 187, in the running original) */
    float hd = fmodf(-st->heading, 360.0f);
    if (hd < 0) hd += 360.0f;

    /* the radar (0x10087b59, window 0 at (44, 22) - (198, 166)): a top-down view centred on the gear, its heading up,
     * the mode's range (200000 in mode 1, the cockpit's) across the window's width. Its view lines (0x10088be7) run
     * from the centre at the view's half angle each side in colour 0x1c; then the player's group's shown navs
     * (0x100889c9: HRHNAVPN, HRHNAVPR once visited), the vehicles by side (0x100882b3: HRHFRGPN, HRHENGPN, HRHNEGPN),
     * and the target / the selected nav highlighted (0x100885f3). INFERRED: clipped to the compass ring (radius
     * 72), the window's own clip function isn't traced. */
    {
        int cx = (44 + 1 + 198) >> 1, cy = (22 + 1 + 166) >> 1;
        float scale = 155.0f / (st->radar_range > 0 ? st->radar_range : 200000.0f);
        window(h, 44, 22, 198, 166);
        for (int s = -1; s <= 1; s += 2) {
            float a = s * 45.0f * 3.14159265f / 180.0f;
            line(h, cx, cy, cx + (int)(sinf(a) * 72), cy - (int)(cosf(a) * 72), 0x1c);
        }
        for (int i = 0; i < st->ncontacts; i++) {
            float x = st->contact[i].x * scale, y = -st->contact[i].z * scale;
            int hl = i == st->tgt_contact || i == st->nav_contact;
            if (x * x + y * y > 72.0f * 72.0f) continue;
            int k = st->contact[i].kind;
            const hg_shape *s = &h->radar[k];
            if (hl) s = k <= HUD_RADAR_NEUTRAL ? &h->radar[5 + k] : &h->radar[8];
            blit(h, s, cx + (int)x, cy + (int)y);
        }
        window(h, 0, 0, 639, 479);
    }

    /* compass, window 24 at (26, 0), hot spot (94, 94) (0x10055924): the ring HNCOMPS1 turned clockwise by that reading
     * (so the letter of the direction faced is at the top), the frame HNCOMPS0, the selected nav's marker HCMPSWPT
     * and the target's (HCMPSTGT for an enemy, HCMPSFRN otherwise) turned to their bearings, then the heading in
     * colour 0x0c at (165, 13) */
    blit_rot(h, &h->s[1], 26 + 94, 0 + 94, hd);
    blit(h, &h->s[0], 26, 0);
    if (st->nav) blit_rot(h, &h->s[2], 26 + 94, 94, st->nav_bearing);
    if (st->tgt) blit_rot(h, &h->s[st->tgt_side == 1 ? 3 : 82], 26 + 94, 94, st->tgt_bearing);
    snprintf(buf, sizeof buf, "%d", (int)hd % 360);
    text(h, 26 + 165, 13, buf, 0x0c);

    /* message box (0x100b26e6): HNMSGBX1 in window 15 at (244, 31), slid up to y -195 while closed (DAT_1014cb04; it
     * slides down to half that when open), then HNMSGBX0 at window 14's corner (244, 0); the current objective at the
     * window's text position (11, 12) */
    blit(h, &h->s[5], 244, st->box_open ? 31 - 195 / 2 : 31 - 195);
    blit(h, &h->s[4], 244, 0);
    if (st->box_open) {
        /* INFERRED: the opened box's lines, one a font height apart from the text position, the label in colour 4 */
        window(h, 244, 0, 517, 233);
        for (int i = 0; i < st->nbox && i < HUD_BOX_LINES; i++) {
            int y = 12 + i * h->font.height, x = 244 + 11;
            if (y + h->font.height > 31 - 195 / 2 + 195) break;
            if (st->box[i].label) x = text(h, x, y, st->box[i].label, 4);
            if (st->box[i].text) text(h, x, y, st->box[i].text, (uint8_t)st->box[i].colour);
        }
        window(h, 0, 0, 639, 479);
    } else if (st->obj_label && st->obj_text) {
        window(h, 244, 0, 517, 233);
        objective_text(h, 244 + 11, 12, st->obj_label, st->obj_text);
        window(h, 0, 0, 639, 479);
    }
    if (st->msg && !st->box_open) {
        window(h, 244, 0, 517, 233);
        text(h, 244 + 11, 12 + 2 * h->font.height, st->msg, 0x0f);
        window(h, 0, 0, 639, 479);
    }

    if (st->objlist) objectives_list(h, st);

    /* the nav marker (0x100545f7): the selected nav's point, if in view, as HWAYPNT0..6 in turn (one frame a draw),
     * remap 5 (as drawn), 7 once visited; its label "Way Point:<name>" (colour 0x1c) and "Range:<m>m" or "<km>k"
     * (0x0f) in a box of colour 0x0c beside it (0x10054ae9). Out of view the label goes to window 20 at (10, 445).
     * INFERRED: the label's offset from the marker (its ftol arguments aren't readable in the decompile). */
    if (st->nav) {
        char l1[64], l2[64];
        snprintf(l1, sizeof l1, "Way Point:%s", st->nav_name ? st->nav_name : "");
        if (st->nav_range <= 1000.0f) snprintf(l2, sizeof l2, "Range: %3.0fm", st->nav_range);
        else snprintf(l2, sizeof l2, "Range: %2.2fk", st->nav_range / 1000.0f);
        if (st->nav_in_view) {
            int x = (int)st->nav_x, y = (int)st->nav_y;
            h->rm = h->remap[st->nav_seen ? 7 : 5];
            blit(h, &h->s[63 + st->frame % 7], x, y);
            h->rm = NULL;
            int tx = x + 12, ty = y - 12, fh = h->font.height;
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
        } else {
            window(h, 10, 445, 168, 479);
            text(h, 10, 445, l1, 0x0f);
            text(h, 10, 445 + 15, l2, 0x0f);
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
    blit(h, &h->s[6], 519, 0);
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
            blit(h, &h->s[7 + i], 519 + 29, 0 + 7);
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
        blit(h, &h->s[15], 0, 2);
        blit(h, &h->s[15 + lvl], 0, 2);
        int kph = (int)st->kph;
        snprintf(buf, sizeof buf, "%3d", st->backward ? -kph : kph);
        uint8_t c = st->backward ? 0x14 : 0x0c;
        text(h, 0, 2, buf, c);
        text(h, 0, 2 + h->font.height, "Kph", c);
    }

    /* weapon panels, windows 3..10 at (523, 135 + 29k): one per weapon slot (0x100b9e60). HNWEPNBX at the corner, the
     * weapon's icon at the hot spot (39, 4) (high resolution only; its damaged icon once its zone is destroyed), the
     * ammunition (when above 0) at (8, 0) and the short name at (8, 10) in colour 0x0f (a slot of kind 1, subkind 0);
     * the selected slot (gear block +0x4fc, 0 at the start) gets its window's border in that colour (0x10076871). */
    static const int wy[8] = { 135, 164, 193, 221, 250, 279, 308, 337 }, wy1[8] = { 159, 188, 217, 246, 274, 303, 332, 361 };
    for (int k = 0; k < h->g.nweapons && k < 8; k++) {
        const gear_weapon *wp = &h->g.weapon[k];
        int x = 523, y = wy[k];
        window(h, 523, y, 635, wy1[k]);   /* each window draws into its own rectangle */
        blit(h, &h->s[14], x, y);
        const hg_shape *ic = &h->icon[k][st->gone[k] && h->icon[k][1].px ? 1 : 0];
        if (ic->px) blit(h, ic, x + 39, y + 4);
        int ammo = st->ammo[k] >= 0 ? st->ammo[k] : wp->ammo;
        if (ammo > 0) { snprintf(buf, sizeof buf, "%d", ammo); text(h, x + 8, y + 0, buf, 0x0f); }
        if (wp->name) text(h, x + 8, y + 10, wp->name, 0x0f);
        if (k == st->selected) {
            for (int i = 523; i <= 635; i++) { put(h, i, y, 0x0f); put(h, i, wy1[k], 0x0f); }
            for (int j = y; j <= wy1[k]; j++) { put(h, 523, j, 0x0f); put(h, 635, j, 0x0f); }
        }
    }
    window(h, 0, 0, 639, 479);
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
