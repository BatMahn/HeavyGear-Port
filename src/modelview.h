/*
 * modelview.h - the shell's 3D views (the gear editor's design, HGSHELL FUN_100a6bb5 on the 3D view class
 * FUN_100be960): a gear's parts drawn into a rectangle of the 640 x 480 screen.
 *
 * Traced: the shell's material tables (0x1012b93c loads every TLT, then every MLT, of shell_tlt / shell_mlt); a
 * polygon's texture is found as in the sim (label -> TLT -> MLT -> texture). The camera (modelview.c: the shell's
 * camera at the origin, zoom 1, aspect 1, the projection of 0x10056736 on the view's rectangle), the object's place
 * and turn (the caller's: the root item's view place, 180 degrees, the arrow keys), and the light (the shell's point
 * light at (0, 10000, -3000), ambient 45). The shell draws 8-bit through the software renderer: XEL textures, the
 * FOG luma table, the screen's palette.
 * Checked against the original's gear editor under Wine (the Hunter, unturned and turned).
 */
#ifndef HG_MODELVIEW_H
#define HG_MODELVIEW_H

#include "hgprj.h"

#include <stddef.h>
#include <stdint.h>

typedef struct modelview modelview;

modelview *modelview_open(hg_archive *a);
void       modelview_close(modelview *v);
/* the light's ambient (+0x2a): 45 by default */
void       modelview_set_ambient(modelview *v, int ambient);
/* show a GDF's model ("gacheeta"); "" or NULL none. 0 if loaded */
int        modelview_set_gear(modelview *v, const char *gdf);
/* show a GDF in memory (the editor's design); extra[rec], when given and not "", is a model drawn at record rec's place
 * besides the GDF's parts (a grenade's: the GDF has POF_HWLH there, the view the grenade item's own ODF model) */
int        modelview_set_data(modelview *v, const uint8_t *d, size_t len, const char (*extra)[9]);
/* show loose models, each at its place from the root (an equipment item's picture, design_item_picture) */
int        modelview_set_parts(modelview *v, const char (*model)[9], const float (*pos)[3], int n);
/* limit the drawing to the screen rectangle (x0, y0)-(x1, y1) (the window the view sits in); on 0 none */
void       modelview_clip(modelview *v, int on, int x0, int y0, int x1, int y1);
/* draw into out (640 x 480 ARGB) as the view (x0, y0)-(x1, y1) of the shell's camera, the model's root at place[],
 * turned `turn` degrees about y; pal the screen's palette (NULL: the true-colour TEX look). Only the model's pixels
 * are written */
void       modelview_draw(modelview *v, uint32_t *out, const uint8_t (*pal)[3], int x0, int y0, int x1, int y1,
                          const float place[3], float turn);

/* where record rec of the shown GDF (its origin) was on screen in the last drawing; 0 if known */
int        modelview_screen(const modelview *v, int rec, float *sx, float *sy);

/* the record of the part drawn under (x, y) in the last drawing, -1 none */
int        modelview_pick(const modelview *v, int x, int y);

#endif
