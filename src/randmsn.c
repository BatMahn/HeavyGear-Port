#include "randmsn.h"

#include "bytes.h"
#include "fsutil.h"
#include "rmg_tables.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void wr_u32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* ---- the random numbers ---- */

static unsigned rmg_seed_state = 1;

void rmg_srand(unsigned seed)
{
    rmg_seed_state = seed;
}

int rmg_rand(void)
{
    rmg_seed_state = rmg_seed_state * 214013u + 2531011u;
    return (int)((rmg_seed_state >> 16) & 0x7fff);
}

/* 0x10064248 (the generator's +0x124 object, vtable 0x101a9afc): below 2, 0; else k = rand() % n, then rand() k
 * more times, the last one's value % n (0 when k is 0) */
static int rng(int n)
{
    if (n < 2) return 0;
    int k = rmg_rand() % n, v = 0;
    for (int i = 0; i < k; i++) v = rmg_rand();
    return v % n;
}

/* a pick by the tables' probabilities (rand(10000) / 10000.0 against the running sum, 0x101a9af0); -1 if none */
static int pick(const rmg_pick *e, int n, int *out)
{
    double r = (double)rng(10000) / 10000.0, sum = 0;
    for (int i = 0; i < n; i++) {
        sum += e[i].p;
        if (r < sum) { *out = e[i].id; return 1; }
    }
    return 0;
}

const char *rmg_type_name(int t)
{
    return t >= 0 && t < 16 ? rmg_movie_name[t] : "";
}

/* ---- files ---- */

static void path_of(const hg_archive *a, const char *name, char *out, size_t n)
{
    fs_join(out, n, hga_dir(a), name);
}

static int write_file(const hg_archive *a, const char *name, const void *d, size_t len)
{
    char p[1024];
    path_of(a, name, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return 0;
    int ok = fwrite(d, 1, len, f) == len;
    fclose(f);
    return ok;
}

/* 0x10065ff7: a record (name, type) copied whole into a file of the game folder */
static int copy_record(hg_archive *a, const char *name, const char *type, const char *out)
{
    size_t len;
    uint8_t *d = hga_load(a, name, type, &len, NULL);
    if (!d) return 0;
    int ok = write_file(a, out, d, len);
    free(d);
    return ok;
}

/* the history files (0x10066d04 / 0x10066da3): i32 count, count x i32 */
static int hist_read(const hg_archive *a, const char *name, int *v, int max)
{
    char p[1024];
    path_of(a, name, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    int32_t n = 0;
    int got = 0;
    if (fread(&n, 4, 1, f) == 1 && n > 0) {
        if (n > max) n = max;
        for (int i = 0; i < n; i++) {
            int32_t x;
            if (fread(&x, 4, 1, f) != 1) break;
            v[got++] = x;
        }
    }
    fclose(f);
    return got;
}

static void hist_write(const hg_archive *a, const char *name, const int *v, int n)
{
    char p[1024];
    path_of(a, name, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    int32_t c = n;
    if (fwrite(&c, 4, 1, f) == 1)
        for (int i = 0; i < n; i++) { int32_t x = v[i]; fwrite(&x, 4, 1, f); }
    fclose(f);
}

static int in_list(const int *v, int n, int x)   /* 0x1006bdd0 */
{
    for (int i = 0; i < n; i++) if (v[i] == x) return 1;
    return 0;
}

/* ---- the steps of 0x1006f69d ---- */

static const char *letter(const rmg *r) { return rmg_mode_letter[r->mode >= 0 && r->mode < 3 ? r->mode : 0]; }

/* 0x10066e2e: the template "M" nn <mode letter> "SCN1" / "SCN2" (by +0x6c), M99MSCN1 for free for all */
static int step_template(rmg *r, char *scn, size_t n)
{
    if (r->mode < 0 || r->type < 0) return 0;
    if (r->type == 0xd) { r->tmpl = 99; snprintf(scn, n, "M99MSCN1"); return 1; }
    if (r->type > 9 || r->side < 0 || r->side > 1) return 0;
    if (!pick(rmg_tmpl[r->side][r->type].e, rmg_tmpl[r->side][r->type].n, &r->tmpl)) return 0;
    if (r->p5 != 0 && r->p5 != 1) return 0;
    snprintf(scn, n, "M%2.2d%.1s%s", r->tmpl % 100, letter(r), r->p5 ? "SCN2" : "SCN1");
    return 1;
}

/* 0x10067752: the ground: geo set rand(3), a mesh of it; in the tour not one of the last two (meshrcrd.rmg, ten
 * tries) */
static void step_mesh(rmg *r, hg_archive *a, char *mesh, size_t n)
{
    if (r->mode == 0) {
        int rec[3] = {0};
        int cnt = hist_read(a, "meshrcrd.rmg", rec, 2), tries = 10;
        do {
            r->geoset = rng(3);
            r->mesh = rmg_mesh[r->geoset].id[rng(rmg_mesh[r->geoset].n)];
        } while (in_list(rec, cnt, r->mesh) && --tries);
        if (cnt < 2) rec[cnt++] = r->mesh;
        else { rec[0] = rec[1]; rec[1] = r->mesh; }
        hist_write(a, "meshrcrd.rmg", rec, cnt);
    } else {
        r->geoset = rng(3);
        r->mesh = rmg_mesh[r->geoset].id[rng(rmg_mesh[r->geoset].n)];
    }
    snprintf(mesh, n, "M%2.2d_GRND", r->mesh);
}

/* 0x10067a69: the art: (time of day, art set) by rand(4) (0x10067a10), then art 1 .. 20; in the tour not one of the
 * last two (artrecrd.rmg). As in the original the record's shift writes the new art into the mesh history's place
 * (0x10067c3e: +0x210), so the art history keeps its second entry twice. */
static void step_art(rmg *r, hg_archive *a, char *art, size_t n)
{
    int k = rng(4);
    r->tod = rmg_art[k][0];
    r->artset = rmg_art[k][1];
    if (r->mode == 0) {
        int rec[3] = {0};
        int cnt = hist_read(a, "artrecrd.rmg", rec, 2), tries = 10;
        do {
            r->art = rng(0x14) + 1;
        } while (in_list(rec, cnt, r->art) && --tries);
        if (cnt < 2) rec[cnt++] = r->art;
        else rec[0] = rec[1];
        hist_write(a, "artrecrd.rmg", rec, cnt);
    } else {
        r->art = rng(0x14) + 1;
    }
    snprintf(art, n, "M%2.2dART%2.2d", r->artset, r->art);
}

static int nobj_count(int n) { return n == -1 ? 1 : n == -3 ? 3 : n; }

/* 0x1006aef0: the objective count by the template's picks */
static int step_nobj(rmg *r)
{
    if (r->tmpl == 99) return 1;
    if (r->tmpl < 0x33 || r->tmpl > 0x53) return 0;
    const struct rmg_obj_s *e = &rmg_obj[r->tmpl - 0x33];
    if (e->id != r->tmpl || e->n < 1) return 0;
    if (e->n == 1) { r->nobj = e->e[0].id; return 1; }
    return pick(e->e, e->n, &r->nobj);
}

/* 0x1006b292: each objective's geo from its slot's list, its index not one of the ids already taken (as the
 * original compares) */
static int step_objectives(rmg *r, char obj[3][24])
{
    if (r->tmpl == 99) return 1;
    const struct rmg_obj_s *e = &rmg_obj[r->tmpl - 0x33];
    if (!e->n) return 0;
    int n = nobj_count(r->nobj);
    if (n < 0 || n > 3) return 0;
    for (int i = 0; i < n; i++) {
        if (!e->ngeo[i]) return 0;
        int k;
        do k = rng(e->ngeo[i]); while (in_list(r->obj, i, k));
        r->obj[i] = e->geo[i][k];
        snprintf(obj[i], 24, "M51GEO%2.2d", r->obj[i]);
    }
    return 1;
}

/* 0x1006b765: the objectives' location, "M51LOC" + a suffix by geo set and objective count (-1 / -3: 0) */
static int step_loc(rmg *r, char *loc, size_t n)
{
    int c = r->nobj == -1 || r->nobj == -3 ? 0 : r->nobj;
    if (r->geoset < 0 || r->geoset > 2 || c < 0 || c > 3) return 0;
    snprintf(loc, n, "M51LOC%s", rmg_loc[r->geoset][c].s[rng(rmg_loc[r->geoset][c].n)]);
    return 1;
}

/* 0x1006b9a8: the waypoints, "M51WAY" + a suffix by template (M99: 0x33's), mode and geo set */
static int step_way(rmg *r, char *way, size_t n)
{
    int t = r->tmpl == 99 ? 0x33 : r->tmpl;
    if (t < 0x33 || t > 0x53 || r->geoset < 0 || r->geoset > 2 || r->mode < 0 || r->mode > 2) return 0;
    const struct rmg_way_s *e = &rmg_way[t - 0x33][r->mode];
    if (e->id != t || e->n < 1) return 0;
    snprintf(way, n, "M51WAY%s", e->s[r->geoset][rng(e->n)]);
    return 1;
}

/* 0x10067436: the enemies, by the threat's picks (the duel template 0x44: duelists, 0x100672e7): "M51" kind + the
 * enemy's side letter + their number */
static int step_enemies(rmg *r, char comp[2][24])
{
    int es = r->p5 == 0;
    if (r->threat < 0 || r->threat > 0x1a) return 0;
    for (int i = 0; i < r->nenemy; i++) {
        if (r->tmpl == 0x44) r->enemy[i] = 8;
        else if (!pick(rmg_enemy[r->threat].e, rmg_enemy[r->threat].n, &r->enemy[i])) return 0;
        snprintf(comp[i], 24, "%s%s%d", rmg_enemy_rec[r->enemy[i]], rmg_side_letter[es], i + 1);
    }
    return 1;
}

/* ---- the scenario's objective texts and the briefing's tags ---- */

static const char *const tag_name[10] = {"/ea", "/o1", "/o2", "/o3", "/mn", "/tm", "/u1", "/u2", "/tr", "/ct"};

/* 0x10071785 with 0x10070fa3: each "/xx" replaced by its tag's text; an unknown tag fails */
static int replace_tags(const rmg *r, char *text, size_t cap)
{
    char out[BRF_TEXT + 1];
    size_t o = 0;
    for (const char *p = text; *p && o + 1 < cap && o + 1 < sizeof out;) {
        if (*p == '/') {
            int k = -1;
            for (int i = 0; i < 10; i++) if (!strncasecmp(p, tag_name[i], 3)) k = i;
            if (k < 0) return 0;
            for (const char *t = r->tag[k]; *t && o + 1 < cap; t++) out[o++] = *t;
            p += 3;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = 0;
    memset(text, 0, cap);
    memcpy(text, out, o < cap ? o : cap - 1);
    return 1;
}

/* 0x10064709 / 0x100645ce: in the scenario's MTBL objectives (0x97 bytes from +0x26, the text at +0x57), /o1 .. /o3
 * become the objectives' names (as many as there are objectives); a text with another tag stays as it is */
static void patch_scn(const rmg *r, uint8_t *d, size_t len)
{
    int n = nobj_count(r->nobj);
    if (n < 1) return;
    for (size_t o = 12; o + 8 <= len;) {
        uint32_t sz = rd_u32(d + o + 4);
        if (sz < 8 || o + sz > len) break;
        if (!memcmp(d + o, "MTBL", 4) && sz >= 0x26) {
            for (uint32_t k = 0; k < (sz - 0x26) / 0x97; k++) {
                char *t = (char *)d + o + 0x26 + k * 0x97 + 0x57;
                if (!memchr(t, '/', 0x40)) continue;
                char buf[0x41];
                size_t w = 0;
                int ok = 1;
                for (const char *p = t; p < t + 0x40 && *p && w < 0x40;) {
                    if (*p == '/') {
                        int j = -1;
                        for (int i = 0; i < n; i++) if (!strncasecmp(p, tag_name[1 + i], 3)) j = i;
                        if (j < 0) { ok = 0; break; }
                        for (const char *q = r->tag[1 + j]; *q && w < 0x40; q++) buf[w++] = *q;
                        p += 3;
                    } else {
                        buf[w++] = *p++;
                    }
                }
                if (!ok) continue;
                buf[w < 0x40 ? w : 0x3f] = 0;
                memset(t, 0, 0x40);
                memcpy(t, buf, strlen(buf));
            }
        }
        o += sz;
    }
}

/* 0x10070cdd: the locale - region k's 24 names by the front line there (+0xd4 + k x 4), the nearer threshold */
static const char *locale(const rmg *r)
{
    int k = r->region;
    if (k < 0 || k > 0x10) return NULL;
    float f = r->front[k + 1];
    int idx = 0x17;
    if (rmg_locale[k][23].t < f) {
        if (f < rmg_locale[k][0].t) {
            for (int j = 0x17; j > 0; j--)
                if (rmg_locale[k][j].t <= f && f < rmg_locale[k][j - 1].t) {
                    idx = f - rmg_locale[k][j].t <= rmg_locale[k][j - 1].t - f ? j : j - 1;
                    break;
                }
        } else {
            idx = 0;
        }
    }
    int nm = rmg_locale[k][idx].name;
    return nm >= 0 && nm < (int)(sizeof rmg_locale_name / sizeof *rmg_locale_name) ? rmg_locale_name[nm] : "";
}

/* 0x10071989: the briefing - the template's BRF, its tags' texts, the objectives' sub-briefings (RANDOBJn.BRF) and
 * the enemy's gear briefing (GEAROBJ1.BRF), then the tags replaced in the summary and every debriefing */
static int step_briefing(rmg *r, hg_archive *a)
{
    int es = r->p5 == 0;
    char name[16];
    if (r->tmpl == 99) snprintf(name, sizeof name, "M99MBRF1");
    else if (r->mode == 2) snprintf(name, sizeof name, "M%2.2d%sBRF%c", r->tmpl, letter(r), r->p5 ? 'B' : 'A');
    else snprintf(name, sizeof name, "M%2.2d%sBRF1", r->tmpl, letter(r));
    size_t len;
    uint8_t *d = hga_load(a, name, "BRF", &len, NULL);   /* 0x10070321 */
    if (!d) return 0;
    int bad = brf_parse(d, len, &r->brf);
    free(d);
    if (bad) return 0;
    if (r->tmpl != 99) {
        snprintf(r->tag[0], 64, "%s", rmg_allegiance[es]);                       /* 0x100703e2 */
        int n = nobj_count(r->nobj);                                              /* 0x10070430 */
        if (n > 3) return 0;
        for (int i = 0; i < n; i++) {
            if (r->obj[i] < 1 || r->obj[i] > 0x21) return 0;
            snprintf(r->tag[1 + i], 64, "%s", rmg_objective_name[r->obj[i]]);
        }
        /* 0x10070502: the mission's name - rarely (rand(10000) < 20) the type's own, else word 1 by side and template
         * and word 2 one of 1 .. 10 */
        if (rng(10000) < 0x14) {
            rng(r->type >= 0 && r->type < 13 ? rmg_rare_n[r->type] : 0);
            snprintf(r->tag[4], 64, "%s", r->type >= 0 && r->type < 13 ? rmg_rare_name[r->type] : "");
        } else {
            int w1 = rmg_name_word1[es][r->tmpl - 0x33], w2 = rng(10) + 1;
            snprintf(r->tag[4], 64, "%s%s", w1 >= 0 && w1 < 13 ? rmg_word1[w1] : "", rmg_word2[w2]);
        }
    }
    {   /* 0x10070855: the time - an hour of the time of day's range (past 24 less 24), a minute */
        int a0 = rmg_hours[r->tod][0], b0 = rmg_hours[r->tod][1];
        int h = rng(a0 < b0 ? b0 - a0 : a0 - b0) + a0;
        if (h > 0x18) h -= 0x18;
        int m = rng(0x3c);
        snprintf(r->tag[5], 64, "%2.2d:%2.2d", h, m);
    }
    if (r->tmpl != 99) {
        if (r->nenemy == 0 || r->nenemy > 2) return 0;                           /* 0x100709af */
        for (int i = 0; i < r->nenemy; i++) snprintf(r->tag[6 + i], 64, "%s", rmg_enemy_name[es][r->enemy[i]]);
    }
    if (r->art < 1 || r->art > 0x14) return 0;                                    /* 0x10070aa4 */
    snprintf(r->tag[8], 64, "%s", rmg_terrain[r->art]);
    const char *ct = locale(r);                                                    /* 0x10070cdd */
    if (!ct) return 0;
    snprintf(r->tag[9], 64, "%s", ct);
    int n = nobj_count(r->nobj);                                                   /* 0x10070ae7 */
    if (n < 0 || n > 3) return 0;
    for (int i = 0; i < n; i++) {
        char g[24], out[24];
        snprintf(g, sizeof g, "M51GEO%2.2d", r->obj[i]);
        snprintf(out, sizeof out, "RANDOBJ%d.BRF", i + 1);
        if (!copy_record(a, g, "BRF", out)) return 0;
    }
    if (r->tmpl != 99) copy_record(a, es ? "M51GEO30" : "M51GEO29", "BRF", "GEAROBJ1.BRF");   /* 0x10070c65 */
    if (!replace_tags(r, r->brf.summary, sizeof r->brf.summary)) return 0;        /* 0x10071946 */
    for (int i = 0; i < r->brf.ndebrief; i++)
        if (!replace_tags(r, r->brf.debrief[i].text, sizeof r->brf.debrief[i].text)) return 0;
    return 1;
}

/* 0x1006630b, the 'C' generator: a new record of the enemy's first two GPS (each 0x5a bytes, a 100-byte chunk) */
static int write_comput(hg_archive *a, const char *rec, const char *out)
{
    size_t len;
    uint8_t *d = hga_load(a, rec, "BWD", &len, NULL);
    if (!d) return 0;
    uint8_t b[0x800];
    memset(b, 0, sizeof b);
    size_t w = 0;
    memcpy(b, "BWD\0", 4);                       /* 0x1005cdb6: the header, REV "1.22", DTBL */
    memcpy(b + 12, "REV \x0c\0\0\0" "1.22", 12);
    memcpy(b + 24, "DTBL\x1c\0\0\0", 8);
    w = 52;
    size_t dtbl = 24;
    int got = 0;
    for (size_t o = 12; o + 8 <= len && got < 2;) {
        uint32_t sz = rd_u32(d + o + 4);
        if (sz < 8 || o + sz > len) break;
        if (memcmp(d + o, "REV ", 4) && memcmp(d + o, "DTBL", 4)) {
            memcpy(b + w, "GPS ", 4);
            b[w + 4] = 100;
            memcpy(b + w + 8, d + o + 8, sz - 8 < 0x5a ? sz - 8 : 0x5a);
            w += 100;
            b[dtbl + 8]++; b[dtbl + 10]++;
            got++;
        }
        o += sz;
    }
    free(d);
    wr_u32(b + 4, (uint32_t)w);
    wr_u32(b + 8, 100);
    return write_file(a, out, b, w);
}

int rmg_generate(rmg *r, hg_archive *a)
{
    char scn[24], mesh[24], art[24], loc[24], way[24], rock[24], obj[3][24], comp[2][24];
    r->tmpl = -1;
    r->nenemy = 2;
    memset(r->obj, 0, sizeof r->obj);
    memset(r->tag, 0, sizeof r->tag);
    memset(&r->brf, 0, sizeof r->brf);
    /* 0x10065f6a: the threat, the level and its supply / reinforcement steps (0x10065e57 / 0x10065eea), 0 .. 0x1a */
    int s = r->supply < -0x14 ? -2 : r->supply <= -0xb ? -1 : r->supply <= 10 ? 0 : r->supply <= 0x28 ? 1 :
            r->supply <= 0x50 ? 2 : 3;
    int g = r->reinforce < -0x14 ? -1 : r->reinforce <= 0x14 ? 0 : 1;
    r->threat = r->level + s + g;
    if (r->threat < 0) r->threat = 0;
    if (r->threat > 0x1a) r->threat = 0x1a;
    rmg_srand(r->seed);
    /* 0x10066572: the last mission's files go */
    static const char *const gone[] = {"RAN1SCN1.BWD", "RAN2SCN1.BWD", "RAN3SCN1.BWD", "RAN4SCN1.BWD", "RANDART1.BWD",
        "RANDLOC1.BWD", "RANDWAY1.BWD", "OBJECT01.BWD", "OBJECT02.BWD", "OBJECT03.BWD", "COMPUT01.BWD", "COMPUT02.BWD",
        "RANDOBJ1.BRF", "RANDOBJ2.BRF", "RANDOBJ3.BRF"};
    for (size_t i = 0; i < sizeof gone / sizeof *gone; i++) {
        char p[1024];
        path_of(a, gone[i], p, sizeof p);
        remove(p);
    }
    if (!step_template(r, scn, sizeof scn)) return 0;
    step_mesh(r, a, mesh, sizeof mesh);
    step_art(r, a, art, sizeof art);
    if (!step_nobj(r) || !step_objectives(r, obj) || !step_loc(r, loc, sizeof loc) || !step_way(r, way, sizeof way) ||
        !step_enemies(r, comp))
        return 0;
    r->region = rng(0x11);                                       /* 0x1006bc26 */
    r->rock = rng(0x1e) + 1;                                      /* 0x1006bca9 */
    snprintf(rock, sizeof rock, "M51ROC%2.2d", r->rock);
    if (!step_briefing(r, a)) return 0;
    /* 0x100660ef: the scenario, its objectives' texts named, as RAN<tod + 1>SCN1.BWD */
    snprintf(r->scenario, sizeof r->scenario, "RAN%dSCN1", (r->tod + 1) % 10);
    {
        size_t len;
        uint8_t *d = hga_load(a, scn, "BWD", &len, NULL);
        if (!d) return 0;
        patch_scn(r, d, len);
        char out[20];
        snprintf(out, sizeof out, "%s.BWD", r->scenario);
        int ok = write_file(a, out, d, len);
        free(d);
        if (!ok) return 0;
    }
    if (!copy_record(a, mesh, "BWD", "RANDMESH.BWD") || !copy_record(a, art, "BWD", "RANDART1.BWD") ||
        !copy_record(a, loc, "BWD", "RANDLOC1.BWD") || !copy_record(a, way, "BWD", "RANDWAY1.BWD") ||
        !copy_record(a, rock, "BWD", "RANDROCK.BWD"))
        return 0;
    for (int i = 0; i < r->nobj; i++) {   /* as the original: no OBJECT files for the counts -1 / -3 */
        char out[24];
        snprintf(out, sizeof out, "OBJECT0%d.BWD", i + 1);
        if (!copy_record(a, obj[i], "BWD", out)) return 0;
    }
    for (int i = 0; i < r->nenemy; i++) {   /* 0x1006630b */
        char out[24];
        snprintf(out, sizeof out, "COMPUT0%d.BWD", i + 1);
        if (!(r->mode == 1 ? write_comput(a, comp[i], out) : copy_record(a, comp[i], "BWD", out))) return 0;
    }
    /* 0x10065af2: the template's type name */
    r->movie = -1;
    for (int i = 0; rmg_tmpl_movie[i][0] != 99; i++)
        if (rmg_tmpl_movie[i][0] == r->tmpl) { r->movie = rmg_tmpl_movie[i][1]; break; }
    return 1;
}
