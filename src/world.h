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
} camera;

typedef struct world world;

world *world_load(hg_archive *a, const char *mission_name, char *err, size_t errlen);
void   world_free(world *w);
/* the mission's SKYGND.PAR line (sky, lights, fog); returns 0 if found */
int    world_set_skygnd(world *w, const char *path);
/* SOFTWARE (1) or 3D (0) look; returns the look in effect (SOFTWARE needs the mission's luma table) */
int    world_set_software(world *w, int on);
/* the mission palette (PALG's first), 256 x RGB */
const uint8_t (*world_palette(const world *w))[3];
/* seconds since the mission started (the 3D cloud layer drifts with it, animated materials run by it) */
void   world_set_time(world *w, float seconds);
void   world_draw(world *w, raster_target *t, const camera *cam);
/* the player's starting view: the cockpit eye of group 0's leader on its start navpoint (else over the middle of
 * the ground, looking along +z) */
void   world_start_camera(const world *w, camera *cam);
/* ground height under (x, z), or 0 */
float  world_ground_height(const world *w, float x, float z, int *found);
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
void   world_set_effect(world *w, int id, int on, float x, float y, float z, float age_ticks);
/* turn an object fully (degrees; yaw about y, pitch about x, roll about z); it is then drawn lit, as a solid */
void   world_turn_object(world *w, int id, float yaw, float pitch, float roll);
/* an object's model bounds, in its own frame (before its yaw and position); 0 on success */
int    world_object_bounds(const world *w, int id, float mn[3], float mx[3]);
/* shots: p[i] = {x, y, z (head), x, y, z (tail)} */
void   world_draw_streaks(const world *w, raster_target *t, const camera *cam, const float (*p)[6], int n, uint32_t rgb);
/* the instance drawing mission obj slot `obj`, or -1 */
int    world_obj_instance(const world *w, int obj);
/* an instance's position and yaw (sin, cos); returns 1 if hidden, 0 if shown, -1 if no such instance */
int    world_object_pose(const world *w, int id, float *x, float *y, float *z, float *s, float *c);
/* the first mission object (not the ground) whose polygons the segment p0 -> p1 passes through: its instance, *t
 * the fraction along; -1 none. world_set_solid takes an instance out of (or back into) the test */
int    world_ray(const world *w, const float p0[3], const float p1[3], float *t);
void   world_set_solid(world *w, int id, int on);
/* a world point on a w x h view: 1 if it falls inside, 0 if outside or behind (sx, sy still say which side) */
int    world_project(const camera *cam, int w, int h, float x, float y, float z, float *sx, float *sy);
void   world_stats(const world *w, int *objects, int *models, int *textures, int *missing_tex);

#endif
