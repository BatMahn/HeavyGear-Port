#include "pcx.h"

#include "bytes.h"

#include <stdlib.h>
#include <string.h>

int pcx_parse(const uint8_t *d, size_t len, hg_pcx *p)
{
    memset(p, 0, sizeof *p);
    if (len < 128 || d[0] != 0x0a || d[3] != 8 || d[65] != 1) return -1;
    int x0 = rd_u16(d + 4), y0 = rd_u16(d + 6), x1 = rd_u16(d + 8), y1 = rd_u16(d + 10), bpl = rd_u16(d + 66);
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || bpl < w) return -1;
    p->w = w; p->h = h; p->bpl = bpl;
    p->px = calloc((size_t)w * h, 1);
    if (!p->px) return -1;
    size_t o = 128;
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < bpl && o < len) {
            uint8_t c = d[o++], n = 1;
            if ((c & 0xc0) == 0xc0) { n = c & 0x3f; if (o >= len) break; c = d[o++]; }
            for (int k = 0; k < n && x < bpl; k++, x++)
                if (x < w) p->px[(size_t)y * w + x] = c;
        }
    }
    if (len >= 769 && d[len - 769] == 0x0c) {
        memcpy(p->pal, d + len - 768, 768);
        p->has_pal = 1;
    }
    return 0;
}

void pcx_free(hg_pcx *p)
{
    free(p->px);
    memset(p, 0, sizeof *p);
}
