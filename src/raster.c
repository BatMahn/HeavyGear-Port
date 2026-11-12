#include "raster.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int raster_init(raster_target *t, int w, int h)
{
    t->w = w; t->h = h;
    t->color = malloc((size_t)w * h * 4);
    t->depth = malloc((size_t)w * h * sizeof(float));
    return t->color && t->depth ? 0 : -1;
}

void raster_free(raster_target *t)
{
    free(t->color);
    free(t->depth);
    memset(t, 0, sizeof *t);
}

void raster_clear(raster_target *t, uint32_t color)
{
    size_t n = (size_t)t->w * t->h;
    for (size_t i = 0; i < n; i++) { t->color[i] = color; t->depth[i] = 0.0f; }
}

static uint32_t shade(uint32_t c, float lr, float lg, float lb, float f, uint32_t fog)
{
    if (lr > 1) lr = 1;
    if (lg > 1) lg = 1;
    if (lb > 1) lb = 1;
    if (f > 1) f = 1;
    if (f < 0) f = 0;
    float g = 1.0f - f;
    int r = (int)(((c >> 16) & 255) * lr * f + ((fog >> 16) & 255) * g);
    int gg = (int)(((c >> 8) & 255) * lg * f + ((fog >> 8) & 255) * g);
    int b = (int)((c & 255) * lb * f + (fog & 255) * g);
    return (c & 0xff000000u) | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)b;
}

static float edge(const raster_vertex *a, const raster_vertex *b, float x, float y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

void raster_triangle(raster_target *t, const raster_vertex *a, const raster_vertex *b, const raster_vertex *c,
                     const raster_texture *tex, uint32_t argb)
{
    float area = edge(a, b, c->x, c->y);
    if (fabsf(area) < 1e-6f) return;
    float minx = fminf(a->x, fminf(b->x, c->x)), maxx = fmaxf(a->x, fmaxf(b->x, c->x));
    float miny = fminf(a->y, fminf(b->y, c->y)), maxy = fmaxf(a->y, fmaxf(b->y, c->y));
    int x0 = (int)fmaxf(0, floorf(minx)), x1 = (int)fminf((float)t->w - 1, ceilf(maxx));
    int y0 = (int)fmaxf(0, floorf(miny)), y1 = (int)fminf((float)t->h - 1, ceilf(maxy));
    if (x0 > x1 || y0 > y1) return;
    float inv = 1.0f / area;
    /* attributes over w */
    float au = a->u * a->iw, av = a->v * a->iw, bu = b->u * b->iw, bv = b->v * b->iw, cu = c->u * c->iw, cv = c->v * c->iw;
    for (int y = y0; y <= y1; y++) {
        float py = (float)y + 0.5f;
        for (int x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f;
            float w0 = edge(b, c, px, py) * inv, w1 = edge(c, a, px, py) * inv, w2 = edge(a, b, px, py) * inv;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float iw = w0 * a->iw + w1 * b->iw + w2 * c->iw;
            size_t o = (size_t)y * t->w + x;
            if (iw <= t->depth[o]) continue;
            uint32_t col;
            if (tex) {
                float u = (w0 * au + w1 * bu + w2 * cu) / iw, v = (w0 * av + w1 * bv + w2 * cv) / iw;
                int tx = (int)floorf(u * (float)tex->w), ty = (int)floorf(v * (float)tex->h);
                tx %= tex->w; if (tx < 0) tx += tex->w;
                ty %= tex->h; if (ty < 0) ty += tex->h;
                if (t->luma && tex->idx) {
                    /* SOFTWARE look (I76DRAW): the table index, interpolated across the polygon in 8.8 fixed point,
                     * picks the row by its high byte; the table is the TBL upside down (world.c software_luma). */
                    int row = 31 - (int)(w0 * a->lum + w1 * b->lum + w2 * c->lum);
                    if (row < 0) row = 0;
                    if (row > 31) row = 31;
                    uint8_t ix = tex->idx[(size_t)ty * tex->w + tx];
                    if (ix == 0xff) continue;   /* the colour key: I76DRAW's rasterizers skip index 255 (0x10002db5) */
                    const uint8_t *p = t->pal[t->luma[row * 256 + ix]];
                    t->depth[o] = iw;
                    t->color[o] = 0xff000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
                    continue;
                }
                if (t->bilinear) {
                    /* the 3D renderers filter bilinearly (matches the original's Direct3D frame) */
                    float fu = u * (float)tex->w - 0.5f, fv = v * (float)tex->h - 0.5f;
                    int x0 = (int)floorf(fu), y0 = (int)floorf(fv);
                    float ax = fu - (float)x0, ay = fv - (float)y0;
                    uint32_t q[4];
                    for (int k = 0; k < 4; k++) {
                        int sx = (x0 + (k & 1)) % tex->w, sy = (y0 + (k >> 1)) % tex->h;
                        if (sx < 0) sx += tex->w;
                        if (sy < 0) sy += tex->h;
                        q[k] = tex->texels[(size_t)sy * tex->w + sx];
                    }
                    float wq[4] = {(1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay};
                    float ch[4] = {0, 0, 0, 0};
                    for (int k = 0; k < 4; k++)
                        for (int s = 0; s < 4; s++) ch[s] += wq[k] * (float)((q[k] >> (s * 8)) & 255);
                    col = 0;
                    for (int s = 0; s < 4; s++) col |= (uint32_t)(ch[s] + 0.5f) << (s * 8);
                    if ((col >> 24) < 128) continue;
                    col |= 0xff000000u;
                } else {
                    col = tex->texels[(size_t)ty * tex->w + tx];
                    if (!(col >> 24)) continue;
                }
            } else {
                col = argb;
            }
            t->depth[o] = iw;
            t->color[o] = shade(col, w0 * a->r + w1 * b->r + w2 * c->r, w0 * a->g + w1 * b->g + w2 * c->g,
                                w0 * a->b + w1 * b->b + w2 * c->b, w0 * a->fog + w1 * b->fog + w2 * c->fog, t->fog_rgb);
        }
    }
}
