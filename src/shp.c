#include "shp.h"

#include "bytes.h"

#include <stdlib.h>
#include <string.h>

int shp_parse(const uint8_t *d, size_t len, int index, hg_shape *s)
{
    memset(s, 0, sizeof *s);
    if (len < 16 || memcmp(d, "1.10", 4)) return -1;
    uint32_t n = rd_u32(d + 4);
    if (index < 0 || (uint32_t)index >= n || 8 + (size_t)(index + 1) * 8 > len) return -1;
    size_t o = rd_u32(d + 8 + (size_t)index * 8);
    if (o + 24 > len) return -1;
    int32_t x0 = rd_i32(d + o + 8), y0 = rd_i32(d + o + 12), x1 = rd_i32(d + o + 16), y1 = rd_i32(d + o + 20);
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return -1;   /* empty shapes */
    s->w = w; s->h = h; s->x0 = x0; s->y0 = y0;
    s->px = calloc((size_t)w * h, 1);
    s->mask = calloc((size_t)w * h, 1);
    if (!s->px || !s->mask) { shp_free(s); return -1; }
    size_t p = o + 24;
    int x = 0, y = 0;
    while (y < h && p < len) {
        uint8_t c = d[p++];
        if (c == 0) { y++; x = 0; }
        else if (c == 1) { if (p < len) x += d[p++]; }
        else if (!(c & 1)) {
            if (p >= len) break;
            uint8_t col = d[p++];
            for (int k = 0; k < c >> 1; k++, x++)
                if (x < w) { s->px[(size_t)y * w + x] = col; s->mask[(size_t)y * w + x] = 1; }
        } else {
            for (int k = 0; k < c >> 1 && p < len; k++, x++, p++)
                if (x < w) { s->px[(size_t)y * w + x] = d[p]; s->mask[(size_t)y * w + x] = 1; }
        }
    }
    return 0;
}

void shp_free(hg_shape *s)
{
    free(s->px);
    free(s->mask);
    memset(s, 0, sizeof *s);
}

int fnt_parse(uint8_t *d, size_t len, hg_font *f)
{
    memset(f, 0, sizeof *f);
    if (len < 16 || memcmp(d, "1.", 2)) { free(d); return -1; }
    int n = (int)rd_u32(d + 4), h = (int)rd_u32(d + 8);
    if (n <= 0 || n > 4096 || h <= 0 || h > 256 || 16 + (size_t)n * 4 > len) { free(d); return -1; }
    f->count = n; f->height = h; f->data = d;
    f->width = calloc((size_t)n, sizeof *f->width);
    f->glyph = calloc((size_t)n, sizeof *f->glyph);
    if (!f->width || !f->glyph) { fnt_free(f); return -1; }
    for (int i = 0; i < n; i++) {
        size_t o = rd_u32(d + 16 + (size_t)i * 4);
        if (o + 4 > len) continue;
        int w = (int)rd_u32(d + o);
        if (w <= 0 || w > 256 || o + 4 + (size_t)w * h > len) continue;
        f->width[i] = w;
        f->glyph[i] = d + o + 4;
    }
    return 0;
}

void fnt_free(hg_font *f)
{
    free(f->width);
    free(f->glyph);
    free(f->data);
    memset(f, 0, sizeof *f);
}
