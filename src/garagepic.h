/*
 * garagepic.h - the gear editor's list pictures (HGSHELL FUN_100b2dac's items): each equipment item's picture is its
 * node tree (0x1013804f, design_item_picture) drawn by the shell's camera into the picture column of its list entry
 * (FUN_100b2dac's +0xc9..+0xd5: (5, 0x19)-(0x65, 0x173) of the list), the projection over the entry's whole rectangle
 * (0x100b3a..: the view's x 0 .. width - 1, y the entry's top .. bottom), the item's root at its view place
 * (0x101b0012 / 16 / 1a, FUN_1011d7ff) turned 180 degrees (0x1011d8bd), clipped to the column. Each item's object
 * is built once and kept (0x1013804f's table at +0x17a56), and turns: B07's action 0xd (FUN_1010e952(0x32, list, -1,
 * 0, 3.0, 0), run on every pass, message 0x20; started / stopped by actions 0x10 / 0x11 on the screen's events 0 / 1)
 * turns every item of the list's current category (0x100b31b6 -> 0x1011d6eb -> 0x1004a555) by 3 degrees about y per
 * 50 ms elapsed (ftime), all but the one being dragged.
 */
#ifndef HG_GARAGEPIC_H
#define HG_GARAGEPIC_H

#include "hgprj.h"

#include <stdint.h>

typedef struct garagepic garagepic;

garagepic *garagepic_open(hg_archive *a);
void       garagepic_close(garagepic *g);
/* draw item id's picture over (x0, y0)-(x1, y1), drawing only inside (cx0, cy0)-(cx1, cy1) */
void       garagepic_draw(garagepic *g, int id, uint32_t *out, const uint8_t (*pal)[3], int x0, int y0, int x1, int y1,
                          int cx0, int cy0, int cx1, int cy1);

/* turn item id's picture by deg about y */
void       garagepic_turn(garagepic *g, int id, float deg);

#endif
