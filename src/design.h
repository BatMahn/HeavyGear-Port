/*
 * design.h - a gear design in the gear editor (HGSHELL state 7): its equipment items, and the GDF built from them.
 *
 * Traced (RE_NOTES "Gear editor internals"):
 *   - an item is an equipment id dropped on an attach point of an item already in the design (FUN_100bec8a); a torso
 *     (or a strider) has none and starts a new design (FUN_1011d33e), its skeleton by the torso: 7 MAMMOTH.VDF, 15
 *     NAGA.VDF, else GEAR.VDF (0x1013614e, 0x10139067);
 *   - an item's node tree (the picture builders 0x1013281e ... 0x101358c4): its parts (ODFs), and empty nodes - its
 *     attach points, numbered in preorder per instance (0x1011c3a9, 0x100c0121); arms and legs are two instances;
 *     the weapons that go either side are a pair of variants (0x1013804f -> 0x101348aa), the variant bit choosing;
 *   - an item fits an attach point when one of the classes the parent part's link lists (link +0x30 count, +0x34
 *     the classes) is its root part's type (0x1011d0d2; type 0x49 also fits 0x22 and 0x24); for one variant item the
 *     first such point of the target item is taken, whatever is on it (0x1011d0d2), which it then replaces
 *     (0x100bf1dd); for a pair, a variant whose point is free is preferred, then one not holding the same id, then
 *     the first (0x100c515d); the target is the item whose point lies nearest the mouse (0x100c5659);
 *   - the GDF (FUN_100c04e5 -> 0x1005b80c): the skeleton's records cleared and filled per instance (0x100bfa60), one
 *     equipment record per instance in preorder (0x100bf681), six fixed records for a strider, the gear block, and
 *     the items as the trailing data (0x100c025b), which a loaded design is rebuilt from (0x100c0a61).
 * Checked: the archive's 39 editor designs rebuild from their trailing data to their own parts and equipment
 * records (but a few link fields, where the shipped ODFs differ from the ones the designs were made with, and the
 * empty records' +4 / +8, which the original leaves from earlier saves of the session).
 */
#ifndef HG_DESIGN_H
#define HG_DESIGN_H

#include "hgprj.h"

#include <stddef.h>
#include <stdint.h>

#define DESIGN_ITEMS 48

typedef struct {
    int id, bit;               /* the equipment id; the variant of a pair (1: the first) */
    int parent, pinst;         /* the item and its instance it hangs on (-1 none) */
    unsigned char slot[4];     /* the attach point of the parent instance, per instance */
    int armor, options;
} design_item;

typedef struct {
    int         model;         /* 1 gear, 2 mammoth, 3 naga (0x10139067) */
    char        name[32];      /* the gear block's name */
    int         side;          /* the gear block's +0x2c: 0 North, 1 South */
    uint8_t     b30, b31;      /* the design's two bytes (the trailing data's first, records' +0x68 / +0x64) */
    design_item item[DESIGN_ITEMS];
    int         nitem;
    int         dirty;
    uint8_t    *gdf;           /* the GDF built from it (design_build) */
    int         rec_item[96];  /* which item filled each part record, -1 none */
    size_t      len;
} design;

/* load a GDF's design ("gacheeta"); 0 on success */
int  design_load(design *g, hg_archive *a, const char *gdf);
void design_free(design *g);
/* build the GDF (g->gdf, g->len); 0 on success */
int  design_build(design *g, hg_archive *a);
/* the design's first item of type t (0 torso .. 4 legs), -1 none */
int  design_part(const design *g, int type);
/* drop equipment id onto the design: near (x, y) on screen, the attach points' screen places from pos (record i ->
 * pos[i][0..1], NULL: the first fitting target). 0 placed, -1 nowhere it fits */
int  design_drop(design *g, hg_archive *a, int id, float mx, float my, const float (*pos)[2]);
/* remove item i and everything on it (0x100bf1dd / 0x100bf0a6; delete piece 0x100bf2c0); clear: all (0x100c0c6a) */
void design_remove(design *g, int i);
void design_clear(design *g);
/* the file name saving would use (0x100fb719): `current` the design's file ("GACHEETA" / "MYGEAR.GDF") */
void design_save_name(const char *dir, const char *current, const char *name, char *out, size_t n);
/* write g->gdf to dir/file; 0 on success */
int  design_write(const design *g, const char *dir, const char *file);

#endif
