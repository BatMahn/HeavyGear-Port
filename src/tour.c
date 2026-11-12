#include "tour.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tour_faction(int type)
{
    return type == 1 || type == 4 ? 0 : 1;
}

int tour_new_game(career *k, const char *name, int faction, int unit)
{
    career_char *n = realloc(k->c, ((size_t)k->count + 1) * sizeof *n);
    if (!n) return -1;
    k->c = n;
    career_char *c = &k->c[k->count];
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    c->type = faction != 1;          /* 0x1013702e: kind 1 -> faction != 1 */
    c->stat[7] = unit;               /* FUN_10110eaa: +0x40 = its 4th argument, +0x50 = -1 */
    c->stat[11] = -1;
    c->stat[8] = 1 << (c->stat[7] & 31);
    /* the rank (FUN_101112bb, types 0 / 1 / 3 / 4): with no missions (stat 3, +0x30) the lowest step, max(rank, 0) */
    c->stat[CAREER_RANK] = 0;
    for (int i = 0; i < 10; i++) c->r25c[i * 20] = 1;   /* DAT_101aef80 */
    /* FUN_1011109f: outside the story the front line is 19 x rand() % 1000 / 50000 + 0.5 (0x101afc34 / 0x101afc38) */
    for (int i = 0; i < 19; i++) c->front[i] = (float)(rmg_rand() % 1000) / 50000.0f + 0.5f;
    return k->count++;
}

int tour_mission_types(const career_char *c, int level, int *t)
{
    int n = 0, b1 = 0, b2 = 0, b3 = 0, unit = c->stat[7];
#define ADD(x) (t[n++] = (x))
    if (c->stat[3] != 0 && c->stat[5] < 1) {
        /* the second branch (a game with missions, the last not won): one type, drawn by the CRT's rand()
         * (FUN_10034fab) - rand() % 2 == 0 keeps the first, else the inner draw picks */
        int r = rmg_rand() % 2, x;
        switch (c->stat[11]) {
        case 0: if (!r) x = 0; else { int k = rmg_rand() % 4; x = k == 0 ? 1 : k == 1 ? 7 : k == 2 ? 2 : level == 0x1a ? 9 : 0; } break;
        case 1: if (!r) x = 3; else { int k = rmg_rand() % 3; x = k == 0 ? 0 : k == 1 ? 4 : level == 0x1a ? 9 : 3; } break;
        case 2: if (!r) x = 1; else { int k = rmg_rand() % 2; x = k == 0 ? 7 : level == 0x1a ? 9 : 7; } break;
        case 3: if (!r) x = 1; else { int k = rmg_rand() % 3; x = k == 0 ? 0 : k == 1 ? 4 : 5; } break;
        case 4: if (!r) x = 0; else { int k = rmg_rand() % 5; x = k == 0 ? 4 : k == 1 ? 7 : k == 2 ? 2 : k == 3 ? 1 : level == 0x1a ? 9 : 0; } break;
        case 5: if (!r) x = 5; else { int k = rmg_rand() % 3; x = k == 0 ? 1 : k == 1 ? 2 : 7; } break;
        case 6: case 8: if (!r) x = 1; else x = rmg_rand() % 2 == 0 ? 7 : 2; break;
        case 7: if (!r) x = 7; else { int k = rmg_rand() % 3; x = k == 0 ? 2 : k == 1 ? 1 : level == 0x1a ? 9 : 7; } break;
        case 9: x = r ? 7 : 1; break;
        default: { int k = rmg_rand() % 3; x = k == 0 ? 2 : k == 1 ? 1 : 7; } break;
        }
        ADD(x);
        return n;
    }
    switch (c->stat[11]) {
    case 0: ADD(4); ADD(1); ADD(7); ADD(2); break;
    case 1: ADD(0); ADD(3); ADD(4); if (level == 0x1a) ADD(9); b1 = 1; break;
    case 2: ADD(1); ADD(7); if (level == 0x1a) ADD(9); break;
    case 3: ADD(0); ADD(4); ADD(5); ADD(6); ADD(8); ADD(1); b2 = b3 = 1; break;
    case 4: ADD(0); ADD(1); ADD(2); ADD(7); break;
    case 5: case 6: case 8: ADD(2); ADD(7); break;
    case 7: ADD(0); ADD(4); ADD(1); ADD(5); if (level == 0x1a) ADD(9); break;
    case 9: ADD(0); ADD(1); ADD(4); ADD(5); ADD(9); break;
    default: ADD(2); ADD(7); break;   /* -1 and the rest */
    }
    if (!b1 && unit == 8) ADD(3);
    if (!b2 && (unit == 5 || unit == 6 || unit == 8 || unit == 9 || unit == 0xf || unit == 0x10)) ADD(6);
    if (!b3 && (unit == 6 || unit == 9 || unit == 0x12)) ADD(8);
#undef ADD
    return n;
}

int tour_generate(hg_archive *a, const career_char *c, int level, int seed, int mode, int type, rmg *r, int *alone)
{
    memset(r, 0, sizeof *r);
    /* local_24: 0 when the game has missions (stat 3) but stat 5 < 1; the template table's side is its negation */
    int l24 = !(c->stat[5] < 1 && c->stat[3] != 0);
    int north = tour_faction(c->type) == 1;
    r->mode = mode;
    r->type = type;
    r->side = l24 == 0;
    r->p5 = north ? 0 : 1;
    r->level = level;
    /* the record's +0x8c block: North [6] supply, [7] reinforcements; South [2] / [1] (0x10112620) */
    r->supply = north ? c->a8c[6] : c->a8c[2];
    r->reinforce = north ? c->a8c[7] : c->a8c[1];
    memcpy(r->front, c->front, sizeof r->front);   /* FUN_10112881(0x22): the game's front line */
    r->seed = (unsigned)seed;
    *alone = l24;
    return rmg_generate(r, a);
}

int tour_units(int faction, int *units)
{
    static const int north[] = {0, 1, 2, 3, -1}, south[] = {10, 11, 12, -1};
    const int *u = faction == 1 ? north : south;
    if (units) for (int i = 0; ; i++) { units[i] = u[i]; if (u[i] < 0) break; }
    return faction == 1 ? 1 : 12;
}
