/*
 * hgview - fly around a Heavy Gear mission (port preview renderer).
 *   hgview <gamedir> <mission, e.g. m01>          window; WASD/arrows move, mouse drag looks, Q/E down/up, L toggles
 *                                                 the SOFTWARE / 3D look
 *   HGVIEW_SHOT=out.ppm hgview <gamedir> <mission> [x z yaw]   render one frame to a PPM and exit (no window)
 *   The mission's music plays from the game CD's track (wavmap.ini, music/trackNN.wav or a CD image in the game
 *   folder; see audio.h); HGVIEW_MUSIC=0 silences it.
 *   x z yaw: start there instead of the player's start (eye HGVIEW_EYE above the ground, default the Hunter's 416.6)
 */
#include "../src/audio.h"
#include "../src/fsutil.h"
#include "../src/hgprj.h"
#include "../src/hud.h"
#include "../src/world.h"
#include "../src/units.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_ppm(const char *path, const raster_target *t)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", t->w, t->h);
    for (int i = 0; i < t->w * t->h; i++) {
        uint32_t c = t->color[i];
        unsigned char rgb[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: hgview <gamedir> <mission>\n"); return 2; }
    char err[256];
    hg_archive *a = hga_open(argv[1], err, sizeof err);
    if (!a) { fprintf(stderr, "%s\n", err); return 1; }
    /* patch 1.2's files: the game folder's own Patch folder, else the copy shipped with the port */
    if (!hga_has_patch(a)) {
        const char *base = SDL_GetBasePath();
        char p[1200];
        snprintf(p, sizeof p, "%sgamedata/patch12/Patch", base ? base : "");
        if (hga_set_patch_fallback(a, p)) hga_set_patch_fallback(a, "gamedata/patch12/Patch");
    }
    printf("patch 1.2 files: %s\n", hga_has_patch(a) ? "yes" : "no (1.0 data only)");
    world *w = world_load(a, argv[2], err, sizeof err);
    if (!w) { fprintf(stderr, "%s\n", err); return 1; }
    /* SKYGND.PAR: the game folder's (the sim fopens it there), or patch 1.2's when the port supplies the patch */
    {
        char p[1200];
        int own = !fs_find_ci(argv[1], "Patch", p, sizeof p);
        const char *base = SDL_GetBasePath();
        int ok = -1;
        if (own && !fs_find_ci(argv[1], "SKYGND.PAR", p, sizeof p)) ok = world_set_skygnd(w, p);
        if (ok) { snprintf(p, sizeof p, "%sgamedata/patch12/SKYGND.PAR", base ? base : ""); ok = world_set_skygnd(w, p); }
        if (ok) ok = world_set_skygnd(w, "gamedata/patch12/SKYGND.PAR");
        if (ok && !fs_find_ci(argv[1], "SKYGND.PAR", p, sizeof p)) ok = world_set_skygnd(w, p);
        printf("SKYGND.PAR: %s\n", ok ? "mission not found" : "loaded");
    }
    int no, nm, nt, nmiss;
    world_stats(w, &no, &nm, &nt, &nmiss);
    static units us;
    if (!(getenv("HGVIEW_UNITS") && !strcmp(getenv("HGVIEW_UNITS"), "0"))) {
        units_spawn(&us, w, a);
        for (int i = 0; i < us.n; i++)
            printf("unit %s group %d slot %d%s: %.0f %.0f %.0f heading %.0f, %d parts, stand %.1f\n", us.u[i].gear, us.u[i].group,
                   us.u[i].slot, us.u[i].placed ? "" : " (no start)", us.u[i].s.x, us.u[i].s.y, us.u[i].s.z,
                   us.u[i].s.heading, us.u[i].gm.nparts, us.u[i].gm.stand);
    }
    printf("%s: %d objects, %d models, %d textures (%d missing)\n", argv[2], no, nm, nt, nmiss);
    if (getenv("HGVIEW_LOOK") && !strcmp(getenv("HGVIEW_LOOK"), "software"))
        printf("look: %s\n", world_set_software(w, 1) ? "SOFTWARE" : "3D (no luma table)");
    camera cam;
    world_start_camera(w, &cam);
    if (argc >= 6) {
        int f;
        cam.x = (float)atof(argv[3]); cam.z = (float)atof(argv[4]); cam.yaw = (float)atof(argv[5]);
        cam.y = world_ground_height(w, cam.x, cam.z, &f) + (getenv("HGVIEW_EYE") ? (float)atof(getenv("HGVIEW_EYE")) : 416.6f);
        if (getenv("HGVIEW_PITCH")) cam.pitch = (float)atof(getenv("HGVIEW_PITCH"));
        if (getenv("HGVIEW_FOV")) cam.fov = (float)atof(getenv("HGVIEW_FOV"));
    }
    /* the HUD (HGVIEW_HUD=0 hides it; H toggles it in the window) */
    hud *hd = hud_load(a, 0);
    /* the player's gear: the shell writes it into USERSTAR (USERGEAR); the viewer takes HGVIEW_GEAR, else the Hunter */
    if (hd) {
        gear g;
        const char *gn = getenv("HGVIEW_GEAR") ? getenv("HGVIEW_GEAR") : "gdhuntea";
        if (!gear_load(a, gn, &g)) hud_set_gear(hd, a, &g, 0);
        else fprintf(stderr, "gear %s not found\n", gn);
    }
    int show_hud = !(getenv("HGVIEW_HUD") && !strcmp(getenv("HGVIEW_HUD"), "0"));
    const char *shot = getenv("HGVIEW_SHOT");
    raster_target rt;
    if (shot) {
        raster_init(&rt, 640, 480);
        world_draw(w, &rt, &cam);
        if (show_hud && hd) { hud_state hs; hud_state_simple(&hs, cam.yaw, 0.0f); hud_draw(hd, rt.color, rt.w, rt.h, world_palette(w), &hs); }
        write_ppm(shot, &rt);
        printf("camera %.0f %.0f %.0f yaw %.0f -> %s\n", cam.x, cam.y, cam.z, cam.yaw, shot);
        return 0;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win;
    SDL_Renderer *ren;
    if (!SDL_CreateWindowAndRenderer("Heavy Gear - hgview", 960, 600, SDL_WINDOW_RESIZABLE, &win, &ren)) return 1;
    if (!(getenv("HGVIEW_MUSIC") && !strcmp(getenv("HGVIEW_MUSIC"), "0")) && !audio_open()) {
        int t = world_music(w);
        if (t > 0) printf("music: track %d %s\n", t, audio_music_track(argv[1], t) ? "not found (see audio.h)" : "playing");
    }
    raster_init(&rt, 960, 600);
    SDL_Texture *tx = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, rt.w, rt.h);
    Uint64 last = SDL_GetTicks();
    for (int run = 1; run;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) run = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_H) show_hud = !show_hud;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_L) {
                static int sw = 0;
                sw = world_set_software(w, !sw);
                printf("look: %s\n", sw ? "SOFTWARE" : "3D");
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
                cam.yaw += e.motion.xrel * 0.25f;
                cam.pitch -= e.motion.yrel * 0.25f;
                if (cam.pitch > 89) cam.pitch = 89;
                if (cam.pitch < -89) cam.pitch = -89;
            }
        }
        Uint64 now = SDL_GetTicks();
        float dt = (float)(now - last) / 1000.0f, sp = 3000.0f * dt;
        last = now;
        const bool *k = SDL_GetKeyboardState(NULL);
        float fx = SDL_sinf(cam.yaw * 3.14159265f / 180), fz = SDL_cosf(cam.yaw * 3.14159265f / 180);
        if (k[SDL_SCANCODE_LSHIFT]) sp *= 4;
        if (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) { cam.x += fx * sp; cam.z += fz * sp; }
        if (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) { cam.x -= fx * sp; cam.z -= fz * sp; }
        if (k[SDL_SCANCODE_A]) { cam.x -= fz * sp; cam.z += fx * sp; }
        if (k[SDL_SCANCODE_D]) { cam.x += fz * sp; cam.z -= fx * sp; }
        if (k[SDL_SCANCODE_LEFT]) cam.yaw -= 90 * dt;
        if (k[SDL_SCANCODE_RIGHT]) cam.yaw += 90 * dt;
        if (k[SDL_SCANCODE_Q]) cam.y -= sp;
        if (k[SDL_SCANCODE_E]) cam.y += sp;
        world_set_time(w, (float)SDL_GetTicks() / 1000.0f);
        world_draw(w, &rt, &cam);
        if (show_hud && hd) { hud_state hs; hud_state_simple(&hs, cam.yaw, 0.0f); hud_draw(hd, rt.color, rt.w, rt.h, world_palette(w), &hs); }
        SDL_UpdateTexture(tx, NULL, rt.color, rt.w * 4);
        SDL_RenderTexture(ren, tx, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    audio_close();
    SDL_Quit();
    hud_free(hd);
    world_free(w);
    hga_close(a);
    return 0;
}
