#include "garagepic.h"

#include "design.h"
#include "modelview.h"

#include <stdlib.h>
#include <string.h>

#define PICS 32

typedef struct { int id, ok; modelview *v; float place[3], turn; unsigned use; } pic;

struct garagepic {
    hg_archive *a;
    pic         p[PICS];
    unsigned    clock;
};

garagepic *garagepic_open(hg_archive *a)
{
    garagepic *g = calloc(1, sizeof *g);
    if (g) { g->a = a; for (int i = 0; i < PICS; i++) g->p[i].id = -1; }
    return g;
}

void garagepic_close(garagepic *g)
{
    if (!g) return;
    for (int i = 0; i < PICS; i++) modelview_close(g->p[i].v);
    free(g);
}

/* the built pictures are kept (the original keeps each item's object once built, its +0x17a56 table) */
static pic *get(garagepic *g, int id)
{
    pic *lru = &g->p[0];
    for (int i = 0; i < PICS; i++) {
        if (g->p[i].id == id) { g->p[i].use = ++g->clock; return &g->p[i]; }
        if (g->p[i].use < lru->use) lru = &g->p[i];
    }
    pic *q = lru;
    if (!q->v) q->v = modelview_open(g->a);
    q->id = id;
    q->use = ++g->clock;
    q->ok = 0;
    q->turn = 180.0f;   /* made facing the camera (0x1011d8bd) */
    if (!q->v) return q;
    char model[64][9];
    float pos[64][3];
    int n = design_item_picture(g->a, id, model, pos, 64, q->place);
    q->ok = n > 0 && modelview_set_parts(q->v, (const char (*)[9])model, (const float (*)[3])pos, n) == 0;
    return q;
}

void garagepic_draw(garagepic *g, int id, uint32_t *out, const uint8_t (*pal)[3], int x0, int y0, int x1, int y1,
                    int cx0, int cy0, int cx1, int cy1)
{
    if (!g || id < 0) return;
    pic *q = get(g, id);
    if (!q->ok) return;
    modelview_clip(q->v, 1, cx0, cy0, cx1, cy1);
    modelview_draw(q->v, out, pal, x0, y0, x1, y1, q->place, q->turn);
}

void garagepic_turn(garagepic *g, int id, float deg)
{
    if (!g || id < 0) return;
    pic *q = get(g, id);
    q->turn += deg;
    while (q->turn >= 360.0f) q->turn -= 360.0f;
}
