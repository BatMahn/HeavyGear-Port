/*
 * test_data - parse every model and texture in a player's game folder (needs the game files; skipped without).
 *   test_data <gamedir>
 */
#include "../src/brf.h"
#include "../src/design.h"
#include "../src/shell_picture.h"
#include "../src/shell_tables.h"
#include "../src/gear.h"
#include "../src/gearmodel.h"
#include "../src/mission.h"
#include "../src/shp.h"
#include "../src/hgprj.h"
#include "../src/tex.h"
#include "../src/wtb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) { printf("test_data: no game folder given - skipped\n"); return 0; }
    char err[256];
    hg_archive *a = hga_open(argv[1], err, sizeof err);
    if (!a) { printf("test_data: %s - skipped\n", err); return 0; }
    int wtb_ok = 0, wtb_legacy = 0, wtb_bad = 0, tex_ok = 0, tex_bad = 0, xel_ok = 0, xel_bad = 0;
    int shp_ok = 0, shp_empty = 0, fnt_ok = 0, fnt_bad = 0;
    for (int i = 0; i < hga_count(a); i++) {
        const hga_entry *e = hga_entry_at(a, i);
        int w = !strcmp(e->type, "WTB"), t = !strcmp(e->type, "TEX"), x = !strcmp(e->type, "XEL");
        if (!strcmp(e->type, "SHP") || !strcmp(e->type, "FNT")) {
            size_t len;
            uint8_t *d = hga_load_index(a, i, &len);
            if (!d) continue;
            if (e->type[0] == 'S') {
                hg_shape sh;
                if (shp_parse(d, len, 0, &sh)) shp_empty++; else { shp_ok++; shp_free(&sh); }
                free(d);
            } else {
                hg_font f;
                if (fnt_parse(d, len, &f)) fnt_bad++; else { fnt_ok++; fnt_free(&f); }
            }
            continue;
        }
        if (!w && !t && !x) continue;
        size_t len;
        uint8_t *d = hga_load_index(a, i, &len);
        if (!d) { printf("unreadable %s.%s\n", e->name, e->type); wtb_bad++; continue; }
        if (w) {
            wtb_model m;
            if (wtb_parse(d, len, &m)) { wtb_bad++; if (wtb_bad < 10) printf("bad WTB %s\n", e->name); }
            else { wtb_ok++; if (m.obj[0].legacy) wtb_legacy++; wtb_free(&m); }
        } else {
            hg_texture tx;
            int r = t ? tex_parse_tex(d, len, &tx) : tex_parse_xel(d, len, &tx);
            if (t) { if (r) tex_bad++; else tex_ok++; } else { if (r) xel_bad++; else xel_ok++; }
            if (!r) tex_free(&tx);
        }
        free(d);
    }
    printf("WTB %d ok (%d MW2 layout), %d bad; TEX %d ok, %d bad; XEL %d ok, %d bad\n", wtb_ok, wtb_legacy, wtb_bad,
           tex_ok, tex_bad, xel_ok, xel_bad);
    printf("SHP %d decoded (%d empty or not 1.10); FNT %d ok, %d bad\n", shp_ok, shp_empty, fnt_ok, fnt_bad);
    /* player starts: checked against the original running under Wine (the player gear's position and heading read from
     * its memory at the start of each mission; mission 5's gear read -150.1, a tenth of a degree off its navpoint's -150 after
     * the first ticks) */
    static const struct { const char *m; float x, z, h; } starts[] = {
        {"m01", -26279, 70690, 180}, {"m02", 9217, -123110, -51}, {"m05", 27463, 122162, -150}, {"m10", 6202, 106266, -187},
    };
    int start_bad = 0;
    for (size_t i = 0; i < sizeof starts / sizeof starts[0]; i++) {
        mission ms;
        if (mission_load(&ms, a, starts[i].m, err, sizeof err)) { printf("mission %s: %s\n", starts[i].m, err); start_bad++; continue; }
        mission_start st = mission_group_start(&ms, 0);
        float dh = st.heading - starts[i].h;
        while (dh > 180) dh -= 360;
        while (dh < -180) dh += 360;
        if (!st.found || st.x != starts[i].x || st.z != starts[i].z || dh > 0.1f || dh < -0.1f) {
            printf("mission %s: start %.0f %.0f %.1f, expected %.0f %.0f %.1f\n", starts[i].m, st.x, st.z, st.heading,
                   starts[i].x, starts[i].z, starts[i].h);
            start_bad++;
        }
        mission_free(&ms);
    }
    printf("player starts: %d of %d match the original\n", (int)(sizeof starts / sizeof starts[0]) - start_bad,
           (int)(sizeof starts / sizeof starts[0]));
    /* weapon lists: checked against the running original's weapon slots (gear block +0xc51) */
    static const struct { const char *g; int n; int type[4], ammo[4]; } gears[] = {
        {"gdhuntea", 2, {28, 2}, {6, 240}},
        {"gfjaguar", 4, {28, 3, 18, 12}, {6, 200, 32, 6}},
    };
    int gear_bad = 0;
    for (size_t i = 0; i < sizeof gears / sizeof gears[0]; i++) {
        gear g;
        int bad = gear_load(a, gears[i].g, &g) || g.nweapons != gears[i].n;
        for (int k = 0; !bad && k < g.nweapons; k++)
            bad = g.weapon[k].type != gears[i].type[k] || g.weapon[k].ammo != gears[i].ammo[k];
        if (bad) { printf("gear %s: weapons differ from the original\n", gears[i].g); gear_bad++; }
    }
    /* damage zones: armor / internal as the running original holds them (mission 1's vehicles, gear block +0x1591) */
    static const struct { const char *g; float z[GEAR_ZONES][2]; } zones[] = {
        {"gdhuntea", {{9, 120}, {85, 250}, {45, 350}, {45, 350}, {40, 500}, {40, 500}, {60, 450}, {0, 0}, {0, 0},
                      {180, 200}, {0, 0}, {50, 100}}},
        {"grjagera", {{9, 120}, {75, 250}, {45, 350}, {45, 350}, {40, 500}, {40, 500}, {40, 450}, {0, 0}, {0, 0},
                      {180, 200}, {0, 0}, {50, 100}, {20, 100}, {100, 100}}},
        {"v72antel", {{0, 0}, {50, 100}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 10}, {100, 600}}},
        {"v12rdjkt", {{0, 0}, {15, 300}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 1000}, {10, 60}, {0, 0}, {0, 0}, {0, 0},
                      {0, 0}, {10, 60}, {10, 60}}},
    };
    for (size_t i = 0; i < sizeof zones / sizeof zones[0]; i++) {
        gear g;
        int bad = gear_load(a, zones[i].g, &g);
        for (int z = 0; !bad && z < GEAR_ZONES; z++)
            if (g.armor[z] != zones[i].z[z][0] || g.internal[z] != zones[i].z[z][1]) {
                printf("gear %s zone %d: %g / %g, the original %g / %g\n", zones[i].g, z + 1, g.armor[z], g.internal[z],
                       zones[i].z[z][0], zones[i].z[z][1]);
                bad = 1;
            }
        if (bad) gear_bad++;
    }
    printf("gear weapons: %d of %d match the original\n", (int)(sizeof gears / sizeof gears[0]) - gear_bad,
           (int)(sizeof gears / sizeof gears[0]));
    /* the campaign's briefings parse (the other BRF records, *_BRF2 / *_BRF3 / M51GEO*, are the narration's object
     * scenes, another format); mission 1's as the original shows it */
    int brf_ok = 0, brf_bad = 0;
    for (int i = 0; i < SHELL_CAMPAIGN; i++) {
        if (!shell_campaign[i].briefing[0]) continue;
        size_t len;
        uint8_t *d = hga_load(a, shell_campaign[i].briefing, "BRF", &len, NULL);
        if (!d) continue;   /* M00_BRF1 isn't shipped; RANDBRF1 is written by the shell (FUN_101103d5) */
        hg_brf *b = malloc(sizeof *b);
        if (d && b && !brf_parse(d, len, b)) brf_ok++;
        else { printf("brf %s: not parsed\n", shell_campaign[i].briefing); brf_bad++; }
        free(b);
        free(d);
    }
    if (SHELL_PICTURES != 1691 || strcmp(shell_pictures[0].name, "B00UNDER") || strcmp(shell_fonts[9], "DEVDDEUT") ||
        shell_front_x[18] != 1.0f || shell_front[1][1] != 0.5f) { printf("shell tables: unexpected\n"); brf_bad++; }
    {
        size_t len;
        uint8_t *d = hga_load(a, "M01_BRF1", "BRF", &len, NULL);
        hg_brf *b = malloc(sizeof *b);
        if (!d || !b || brf_parse(d, len, b) || strncmp(b->summary, "Mission Code Name:", 18) || b->ndebrief != 2 ||
            strcmp(b->debrief[0].title, "MISSION SUCCESSFUL") || b->nmarker != 2 || strcmp(b->marker[0].icon, "B05NSSHP")) {
            printf("brf M01_BRF1: differs from the original's briefing\n");
            brf_bad++;
        }
        free(b);
        free(d);
    }
    printf("campaign briefings: %d parsed, %d bad\n", brf_ok, brf_bad);
    /* the gear editor (src/design.c): every stock design rebuilt from its trailing data matches its parts, equipment
     * records, gear block and trailing data - but the bytes the original leaves from earlier saves of its session
     * (an empty part's +4 / +8, the name buffer past its end, the items' pointer and unused attach bytes) and the
     * links of the parts whose shipped ODFs differ from the ones the designs were made with */
    {
        int bad = 0, ok = 0, links = 0;
        for (int s = 0; s < SHELL_STOCK_GEARS; s++) {
            char nm[16];
            size_t k = 0;
            for (; shell_stock_gear[s][k] && k < 15; k++) nm[k] = (char)(shell_stock_gear[s][k] | 0x20);
            nm[k] = 0;
            size_t len;
            uint8_t *d = hga_load(a, nm, "GDF", &len, NULL);
            design g;
            if (!d) continue;   /* GBASSALT isn't shipped */
            if (design_load(&g, a, nm) || design_build(&g, a) || g.len < 0x88 || memcmp(g.gdf, d, 0x88)) { bad++; free(d); design_free(&g); continue; }
            int n = (int)(d[0x84] | d[0x85] << 8), e = 0;
            for (int r = 0; r < n; r++) {
                const uint8_t *p = d + 0x88 + (size_t)r * 0x438, *q = g.gdf + 0x88 + (size_t)r * 0x438;
                if ((p[0x14] && memcmp(p, q, 0xc)) || memcmp(p + 0xc, q + 0xc, 0x48) || memcmp(p + 0x54, q + 0x54, 0x114)) e++;
                else if (memcmp(p + 0x168, q + 0x168, 0x2d0)) links++;
            }
            size_t o = 0x88 + (size_t)n * 0x438;
            if (memcmp(d + o, g.gdf + o, 4) || strcmp((const char *)d + o + 4, (const char *)g.gdf + o + 4) ||
                memcmp(d + o + 0x24, g.gdf + o + 0x24, 0x18)) e++;
            int neq = (int)(d[o + 0x24] | d[o + 0x25] << 8);
            if (memcmp(d + o + 0x3c, g.gdf + o + 0x3c, (size_t)neq * 0x6c)) e++;
            size_t t = o + 0x3c + (size_t)neq * 0x6c;
            if (len < t + 6 || g.len != len) e++;
            else for (size_t i = 0; i + 0x12 <= len - t - 6; i += 0x12) {
                const uint8_t *p = d + t + 6 + i, *q = g.gdf + t + 6 + i;
                if (memcmp(p, q, 4) || memcmp(p + 12, q + 12, 6) || p[8] != q[8]) e++;
            }
            if (e) { bad++; printf("design %s: %d differences\n", nm, e); } else ok++;
            free(d);
            design_free(&g);
        }
        char name[64];
        design_save_name(".", "GACHEETA", "Black Mamba", name, sizeof name);
        if (strcmp(name, "Black M0.GDF")) bad++;
        design_save_name(".", "MYGEAR.GDF", "x", name, sizeof name);
        if (strcmp(name, "MYGEAR.GDF")) bad++;
        printf("gear editor designs: %d rebuilt as shipped (%d parts' links from other ODFs), %d differ\n", ok, links, bad);
        brf_bad += bad;
    }
    /* the Hunter standing (GRFSTAND, the rifle set): its parts as the running original's objects had them (mission 1,
     * the player's gear, relative to its root and facing) */
    int pose_bad = 0;
    {
        gearmodel gm;
        gear gr;
        int ty[GEAR_MAX_WEAPONS], ki[GEAR_MAX_WEAPONS];
        if (gearmodel_load(a, "gdhuntea", &gm) || gear_load(a, "gdhuntea", &gr)) pose_bad = 1;
        else {
            for (int i = 0; i < gr.nweapons; i++) { ty[i] = gr.weapon[i].type; ki[i] = gr.weapon[i].kind; }
            if (gearmodel_stand(&gm, a, ty, ki, gr.nweapons, gr.version)) pose_bad = 1;
            static const struct { int rec; float x, y, z; } want[] = {
                {40, 128.6f, 111.1f, -10.6f}, {41, 128.0f, 70.8f, 40.1f}, {43, 128.0f, 71.8f, 103.8f},
                {44, 125.2f, 72.0f, 139.4f}, {45, 125.7f, 98.3f, 344.4f}, {46, 123.4f, 79.0f, 120.8f},
                {28, 0.0f, 155.6f, 48.3f},
            };
            for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
                const float *r = gm.rec[want[i].rec];
                if (fabsf(r[0] - want[i].x) > 0.2f || fabsf(r[1] - want[i].y) > 0.2f || fabsf(r[2] - want[i].z) > 0.2f) {
                    printf("pose: record %d at %.1f %.1f %.1f\n", want[i].rec, r[0], r[1], r[2]);
                    pose_bad++;
                }
            }
            /* the camera (0x100373fc): the eye lifted by its own offset's y, 0x100c4c26 */
            if (fabsf(gm.eye[1] + gm.eye_local[1] - 171.3f) > 0.2f || fabsf(gm.stand - 245.3f) > 0.1f) pose_bad++;
        }
        printf("gear pose: the Hunter standing %s\n", pose_bad ? "DIFFERS" : "as the original");
        /* the walk (0x1003fbb7 / 0x10013d53): GRFWALKF (41 frames at 30 a second, footsteps on 5 and 25) picked at
         * half throttle, blended in over 10 steps, then stepped 1 frame a 6-tick frame (ftol(30 x 6 / 181 + 0.5));
         * a foot comes down at frame 5 and 25; the soles stay on the ground (the standing height kept) */
        int walk_bad = 0;
        if (!pose_bad) {
            anim_player ap;
            anim_player_init(&ap);
            anim_in in = {0};
            in.cls = 1; in.set = gm.anim_set; in.thr = 1.0f / 64 + 0.5f / 64;
            unsigned fl = 0;
            int steps = 0, feet = 0;
            const anim3di *w = anim_get(a, 0x10a);
            if (!w || w->frames != 41 || w->fps != 30 || w->nev != 2 || w->ev[0].frame != 5 || w->ev[1].frame != 25) walk_bad++;
            anim_pick(&ap, &in, &fl);   /* standing first: the stand at rate 2 (t 0.5: band 0.0078..0.0117 -> 3) */
            for (steps = 0; steps < 200; steps++) {
                anim_pick(&ap, &in, &fl);
                anim_advance(&ap, a, 6, &fl, 0);
                gearmodel_animate(&gm, &ap, fl, 1);
                if (ap.footstep) { feet++; ap.footstep = 0; }
                if (fabsf(gm.stand - 245.3f) > 0.1f) walk_bad++;
            }
            if (ap.cur != 0x10a || ap.rate != 3 || feet < 10) walk_bad++;
            printf("gear walk: GRFWALKF rate %d, %d footsteps in 200 frames %s\n", ap.rate, feet, walk_bad ? "DIFFERS" : "ok");
            pose_bad += walk_bad;
        }
    }
    hga_close(a);
    return pose_bad || wtb_bad || tex_bad || xel_bad || start_bad || fnt_bad || gear_bad || brf_bad;
}
