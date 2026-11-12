/*
 * structures.h - the mission's destructible structures (GT chunks: tents, radio masts, flag poles, windmills ...).
 *
 * Traced (the GT branch of 0x100c1152, 0x1001cc6c): a structure has the GT's hit points (32000 and 0 can't be
 * destroyed; a negative value counts as its size and picks the explosion: odd 0x24, even 0x16, else 0x32); every shell that strikes it takes its
 * power off them (some polygons, 0xb0 in their flags, destroy it outright: not modelled), and at 0 it is destroyed:
 * its model gives way to the GT's other OBJ (the 'oa' wreck): 0x1001caf9 -> 0x1009db64 -> 0x1009d97e takes the
 * structure's object off and shows the linked one (0x1009d8f2, from the GT) in its place, with its children.
 */
#ifndef HG_STRUCTURES_H
#define HG_STRUCTURES_H

#include "world.h"

typedef struct {
    int n;
    struct { int hp, dead, inst, wreck, obj, alt, boom; } s[512];   /* boom: its explosion effect */
} structures;

void structures_init(structures *st, const world *w);
/* the first structure the segment p0 -> p1 passes through (its model's box), -1 if none; *t the fraction along */
int  structures_hit(const structures *st, const world *w, const float p0[3], const float p1[3], float *t);
/* a shell of the given power strikes structure i; returns 1 if that destroyed it */
int  structures_damage(structures *st, world *w, int i, int power);

#endif
