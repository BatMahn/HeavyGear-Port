#include "tex.h"

#include "bytes.h"

#include <stdlib.h>
#include <string.h>

int tex_parse_tex(const uint8_t *d, size_t len, hg_texture *t)
{
    memset(t, 0, sizeof *t);
    if (len < 12 || memcmp(d, "1.2", 3)) return -1;
    t->type = rd_u16(d + 4);
    t->w = rd_u16(d + 6);
    t->h = rd_u16(d + 8);
    t->npal = rd_u16(d + 10);
    if (t->npal > 256 || 12 + (size_t)t->npal * 4 + (size_t)t->w * t->h > len) return -1;
    memcpy(t->pal, d + 12, (size_t)t->npal * 4);
    t->idx = malloc((size_t)t->w * t->h + 1);
    memcpy(t->idx, d + 12 + t->npal * 4, (size_t)t->w * t->h);
    return 0;
}

int tex_parse_xel(const uint8_t *d, size_t len, hg_texture *t)
{
    memset(t, 0, sizeof *t);
    if (len < 4) return -1;
    t->w = rd_u16(d);
    t->h = rd_u16(d + 2);
    if (4 + (size_t)t->w * t->h > len) return -1;
    t->idx = malloc((size_t)t->w * t->h + 1);
    memcpy(t->idx, d + 4, (size_t)t->w * t->h);
    return 0;
}

void tex_free(hg_texture *t)
{
    free(t->idx);
    memset(t, 0, sizeof *t);
}
