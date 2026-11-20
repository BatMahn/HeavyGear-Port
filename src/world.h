/*
 * world.h - a loaded mission ready to draw: meshes resolved to textures, objects placed.
 */
#ifndef HG_WORLD_H
#define HG_WORLD_H

#include "hgprj.h"
#include "mission.h"
#include "raster.h"

typedef struct {
    float x, y, z, yaw, pitch;   /* degrees; yaw 0 looks along +z */
    float fov;                   /* horizontal, degrees */
    float roll;                  /* degrees about the view axis (the cockpit: the eye object's / 10, 0x100373fc); R = Ry Rx Rz */
} camera;

typedef struct world world;

world *world_load(hg_archive *a, const char *mission_name, char *err, size_t errlen);
void   world_free(world *w);
/* the mission's SKYGND.PAR line (sky, lights, fog); returns 0 if found */
int    world_set_skygnd(world *w, const char *path);
/* SOFTWARE (1) or 3D (0) look; returns the look in effect (SOFTWARE needs the mission's luma table) */
int    world_set_software(world *w, int on);
/* B64's Sky (hgsnd +0x38: the 3D cloud layer, 0x1008dff3 -> 0x1008da61) and Texture Filtering (+0x3c: SimMain's
 * render state 0x200) */
void   world_set_options(world *w, int sky, int filter);
/* 3D look: 1 lights vertices with SKYGND's coloured sums (the 3DFX / PowerVR plugins, 0x1014b954), 0 (the default)
 * with DX5DRAW's grey intensity */
void   world_set_coloured_light(world *w, int on);
/* the mission palette (PALG's first), 256 x RGB */
const uint8_t (*world_palette(const world *w))[3];
/* seconds since the mission started (the 3D cloud layer drifts with it, animated materials run by it) */
void   world_set_time(world *w, float seconds);
void   world_draw(world *w, raster_target *t, const camera *cam);
/* the target display (0x100ad8cb): instances ids[0..n) drawn over the frame, each flat in palette colour colour[i] */
void   world_draw_flat(world *w, raster_target *t, const camera *cam, const int *ids, const unsigned char *colour, int n);
/* draw without the fog (the radar map's scene: how the original fogs it isn't traced) */
void   world_set_nofog(int on);
/* point lights (0x1004b275 / 0x1004b2e1 / 0x1004b193): an explosion's light at a world point for `life` ticks (-1
 * none if the dynamic lights are off or the pool of 0x100 is full); aged once a frame; the option is hgsnd +0x2c */
int    world_add_light(world *w, float x, float y, float z, int life);
void   world_lights_tick(world *w, int ticks);
void   world_set_dynamic_lights(world *w, int on);
/* the player's starting view: the cockpit eye of group 0's leader on its start navpoint (else over the middle of
 * the ground, looking along +z) */
void   world_start_camera(const world *w, camera *cam);
/* ground height under (x, z), or 0 */
float  world_ground_height(const world *w, float x, float z, int *found);
/* the same, with the struck polygon's unit normal (facing up; (0, 1, 0) where there is none) */
float  world_ground_height_n(const world *w, float x, float z, int *found, float n[3]);
/* the mission's music: the CD track its record names ("track2" -> 2, HGSIM 0x10015cdc), or -1 */
int    world_music(const world *w);
const mission *world_mission(const world *w);
/* a model placed in the world by the sim (a vehicle's part): returns its id, -1 if the model isn't found */
int    world_add_object(world *w, const char *model, float x, float y, float z, float yaw);
void   world_move_object(world *w, int id, float x, float y, float z, float yaw);
void   world_show_object(world *w, int id, int on);
/* an effect model (a flat quad drawn facing the camera, unlit, its animated material run from its start); hidden
 * until world_set_effect turns it on. -1 if the model isn't there */
int    world_add_effect(world *w, const char *model);
void   world_set_effect(world *w, int id, int on, float x, float y, float z, float age_ticks, int seq);
/* an object's orientation as a rotation matrix (row-major, applied to the model's points as m v); it is then drawn
 * lit, as a solid */
void   world_set_object_matrix(world *w, int id, const float m[9]);
/* an object's model bounds, in its own frame (before its yaw and position); 0 on success */
int    world_object_bounds(const world *w, int id, float mn[3], float mx[3]);
/* the model's bounding sphere as the sim keeps it (0x10003326): centred on its box's centre, reaching its farthest
 * point; halved for models named w..., / 1.5 for names containing ENBP. centre (model space) may be NULL. */
float  world_object_radius(const world *w, int id, float centre[3]);
/* Shots in flight as their kind's model (gearsht1's HGBT chunks, kinds below WORLD_SHOT_KINDS); m from
 * world_shot_matrix (dir: the shot's fired direction, or a guided shot's motion). Instances are pooled per kind. */
#define WORLD_SHOT_KINDS 0x4d
typedef struct { int kind; float x, y, z; float m[9]; } world_shot;
void   world_set_shots(world *w, const world_shot *s, int n);
void   world_shot_matrix(const float dir[3], int guided, float m[9]);
int    world_has_shot_model(world *w, int kind);
/* the mission's cover points (objects flagged 0xd0 / 0xe0: 0x100275db), at most max (200); kind 1 / 2 */
int    world_cover_points(const world *w, float (*out)[3], int *kind, int max);
/* the same points' boxes over their model's vertices in the world (0x10024135: x0, x1, y0, y1, z0, z1), in order */
int    world_cover_boxes(const world *w, float (*box)[6], int max);
/* the kind's pool size (the HGBT's OBJ list: 40 tracers, 20 the others), 0 if it has no model */
int    world_shot_capacity(world *w, int kind);
/* the radius of the kind's model (the mesh's +0x50, 0x10003326's sphere), 0 if none: a shell starts twice that ahead
 * of its mount (0x100bd1e7) */
float  world_shot_radius(world *w, int kind);
/* the 3D look's texture (TEX) of a name, NULL if there's none; valid until the next texture is loaded */
const raster_texture *world_texture(world *w, const char *name);
/* the eyepoint's far distance (+0x40): the VIEW chunk's distance x 2.5 */
float  world_far(const world *w);
/* the instance drawing mission obj slot `obj`, or -1 */
int    world_obj_instance(const world *w, int obj);
/* show or hide a mission OBJ (by obj slot) and its children (0x1009dc4d) */
void   world_show_obj(world *w, int obj, int on);
/* an instance's position and yaw (sin, cos); returns 1 if hidden, 0 if shown, -1 if no such instance */
int    world_object_pose(const world *w, int id, float *x, float *y, float *z, float *s, float *c);
/* the first mission object (not the ground) whose polygons the segment p0 -> p1 passes through: its instance, *t
 * the fraction along; -1 none. world_set_solid takes an instance out of (or back into) the test */
int    world_ray(const world *w, const float p0[3], const float p1[3], float *t);
/* the same, with the struck polygon's normal's world y in *ny */
int    world_ray_n(const world *w, const float p0[3], const float p1[3], float *t, float *ny);
void   world_set_solid(world *w, int id, int on);
/* the view's near plane (0: the 640-wide view's 161, 0x100b8496; 8 in the track view); an instance drawn with the near
 * plane at 8 (the player's own arm in the cockpit, 0x10090d53) */
void   world_set_near(world *w, float near_z);
void   world_set_close(world *w, int id, int on);
/* the OBJ flags (mission_obj.flags) of the mission object an instance draws, 0 if none */
unsigned world_object_flags(const world *w, int id);
/* the sim object's kind (+0x34, 0x100c058b -> 0x100224c0): the OBJ's +0xc (outside 0..8: 4), 8 for the "tl1m"
 * ground tiles; -1 if the instance draws no mission object */
int    world_object_kind(const world *w, int id);
/* a world point on a w x h view: 1 if it falls inside, 0 if outside or behind (sx, sy still say which side) */
int    world_project(const camera *cam, int w, int h, float x, float y, float z, float *sx, float *sy);
void   world_stats(const world *w, int *objects, int *models, int *textures, int *missing_tex);

#endif
