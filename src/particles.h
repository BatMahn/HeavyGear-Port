/*
 * particles.h - the sim's particle engine (traced: HGSIM 0x10079c54 and its kinds) - src/particles.c
 *
 * A particle system (0x1007953c: up to one per pooled scene object, 1000 at most, 0x10079356) is a scene object with
 * eight nodes (offsets from it, +0x12 + n x 0x32), an update run once a frame while the frame's ticks are not 0
 * (0x10079dfa) and a draw run as its object is drawn (0x10079eee). Its type (+0x402) is one of the 22 records at
 * 0x1011e700 (stride 0x1e): +0 the texture slot, +4 the frame count, +8..+0xa red, green, blue, +0xc intensity, +0xd
 * size, +0xe the name ("railgun", "haywire", "accel", "laser", "shockwave", "skate", "gun", "smoke", "grenade",
 * "mortar", "missile", "chunket", "tracer", "spark", "missilespark", "laserspark", "pweaponspark", "projpuff",
 * "laserpuff", "pweaponpuff", "missilepuff", "footstep"), +0x12 lock, +0x16 order, +0x1a loop. PartSpec.ini fills
 * them (0x10078e34: [name] Red, Green, Blue, Intensity, Size, Lock, Order, Loop, Texture00..31) for the 3D
 * renderers only (0x100790be: in software every type is untextured, colour 15, size 1); the types with textures
 * take texture slots 0x100, 0x101 .. in table order.
 *
 * Kinds (the spawn, its update, its draw):
 *  - tracer glow (0x10084362 / 0x10083dde / 0x10083e8a): type 12 "tracer"; shells of kinds 0x38 / 0x39 each frame
 *  - missile trail (0x10082556 / 0x10081afb / 0x10081bdc): type 10 "missile"
 *  - smoke trail (0x100805dc / 0x1007ffb3 / 0x1008011d): type 8 "grenade"
 *  - ground spray (0x1008514d / 0x10084a87 / 0x10084b83): types 17..20 by the weapon; the 3D renderers only
 *  - beams (0x100baf31): kind 6 (accelerators) 15 segments (0x1007acf3 / 0x1007a26c / 0x1007a379, type 2), kind 4
 *    (rail guns) 8 points (0x1007a0a6 / 0x10079f7d / 0x1007a092 draws nothing; at its end effect 0x49), others
 *    (lasers) 15 segments (0x1007b956 / 0x1007aec2 / 0x1007afdc, type 3)
 */
#ifndef HG_PARTICLES_H
#define HG_PARTICLES_H

#include <stdint.h>

#include "hgprj.h"
#include "raster.h"
#include "world.h"

#define PART_TYPES 22
#define PART_MAX   1000
#define PART_BEAMS 0x200

typedef struct {
    uint8_t r, g, b, a, intensity, size;
    int     lock, order, loop;
    int     nframes;
    char    frame[32][9];
    int     slot;          /* the 3D look's texture slot (0x100 + n), -1 none */
} part_spec;

typedef struct {
    int   used, fn, type;
    int   start, life;     /* the clock at the spawn (+0x33e); beams' life (+0x342) */
    float pos[3], drift[3];
    struct { float o[3], v[3]; int frame, size; } n[8];
    int   seq;             /* +0x3fa: the system's frame, -1 random */
    int   lock, ended;     /* +0x10 bits 4 (a Lock type's nodes keep their frames after the first draw) / 2 */
    float persp;           /* +0x3fe: (0x101316dc / its distance) / 4 */
    int   colour, colour2; /* +0x346 / +0x34a */
    int   beam, seg;
} part_sys;

/* what a spawn needs of the view (0x10084362's test: within the eyepoint's far distance (+0x40), on the screen
 * (0x100b87f9) and nothing between it and the eye (0x100229d9)) and what the draw scales by */
typedef struct {
    camera cam;
    int    w, h;           /* the view's size */
    float  far;
    float  camdist;        /* 0x101316dc: the outside view's distance (the gear's +0x53c x 5 x 4) */
    int    outside;        /* 0x1014b58c: the view is 1 (outside): sizes by +0x3fe, else by the zoom */
    float  zoom;           /* the eyepoint's +0x18 (1) */
    int    software;       /* the SOFTWARE look (DAT_1014a5e8 == 0) */
    void  *ctx;
    /* 1 if the segment a -> b is blocked by the scene (0x100229d9); may be NULL */
    int  (*blocked)(void *ctx, const float a[3], const float b[3]);
} part_view;

typedef struct particles particles;

/* the types from PartSpec.ini (the original reads the game folder's, 0x10078e34): the file ini if it is given and
 * there, else the archive's lookup (the patch folder, the game folder); a NULL archive and file: the defaults */
particles *particles_new(hg_archive *a, const char *ini);
void       particles_free(particles *p);
/* B64's Particles (hgsnd +0x28, 0x10086a56 -> 0x10079cec): off, only the beams are made; the others' systems end */
void       particles_set_enabled(particles *p, int on);
void       particles_set_view(particles *p, const part_view *v);
int        particles_count(const particles *p);
const part_spec *particles_spec(const particles *p, int type);

/* spawns: at (x, y, z), d the shell's motion this frame (the spray: its velocity, wtype the weapon type) */
void particles_tracer(particles *p, int clock, const float at[3], const float d[3]);
void particles_missile(particles *p, int clock, const float at[3], const float d[3]);
void particles_smoke(particles *p, int clock, const float at[3], const float d[3]);
void particles_spray(particles *p, int clock, const float at[3], const float v[3], int wtype);
/* a beam fired (0x100baf31): id the caller's beam slot, kind the weapon's +0x10 (2 laser, 4 rail gun, 6
 * accelerator), life its life (ticks), pts its 16 points (0x10018a9c: mount to end, in 15 steps) */
void particles_beam(particles *p, int clock, int id, int kind, int life, const float pts[16][3]);
/* the beam's points as 0x10018a9c keeps them while it lasts */
void particles_beam_points(particles *p, int id, const float pts[16][3]);
/* the lasers' loop sound: called with 1 as a laser's segment system starts (0x1007b956), 0 as one ends (0x1007aec2) */
void particles_set_sound(particles *p, void (*laser)(void *ctx, int on), void *ctx);

/* the frame's update (0x10079dfa), run when the frame's ticks are not 0; effect: a railgun point's end (0x49) */
void particles_frame(particles *p, int clock, void (*effect)(void *ctx, int type, const float at[3]), void *ctx);

/* draws every system over the scene (depth-tested); tex: the 3D look's texture by name (NULL in SOFTWARE),
 * pal: the mission palette */
void particles_draw(particles *p, raster_target *t, const camera *cam, const uint8_t (*pal)[3],
                    const raster_texture *(*tex)(void *ctx, const char *name), void *tctx);

#endif
