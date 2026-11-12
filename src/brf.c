#include "brf.h"

#include <ctype.h>
#include <string.h>

static int rd_i32(const unsigned char *d, size_t len, size_t *o, int *v)
{
    if (*o + 4 > len) return -1;
    *v = (int)((unsigned)d[*o] | (unsigned)d[*o + 1] << 8 | (unsigned)d[*o + 2] << 16 | (unsigned)d[*o + 3] << 24);
    *o += 4;
    return 0;
}

static int rd_f32(const unsigned char *d, size_t len, size_t *o, float *v)
{
    int i;
    if (rd_i32(d, len, o, &i)) return -1;
    memcpy(v, &i, 4);
    return 0;
}

static int rd_str(const unsigned char *d, size_t len, size_t *o, char *out, size_t n)
{
    if (*o + n > len) return -1;
    memcpy(out, d + *o, n);
    out[n] = 0;
    *o += n;
    return 0;
}

int brf_parse(const unsigned char *d, size_t len, hg_brf *b)
{
    memset(b, 0, sizeof *b);
    size_t o = 0x1a;
    char skip[64];
    if (rd_str(d, len, &o, b->summary, BRF_TEXT)) return -1;
    if (rd_i32(d, len, &o, &b->ndebrief) || b->ndebrief < 0 || b->ndebrief > 5) return -1;
    for (int i = 0; i < b->ndebrief; i++) {
        brf_debrief *e = &b->debrief[i];
        if (rd_str(d, len, &o, e->name, 32) || rd_str(d, len, &o, e->title, 128) || rd_str(d, len, &o, e->text, BRF_TEXT)) return -1;
    }
    if (rd_i32(d, len, &o, &b->nobjective) || b->nobjective < 0 || b->nobjective > 10) return -1;
    for (int i = 0; i < b->nobjective; i++)
        if (rd_str(d, len, &o, skip, 32) || rd_str(d, len, &o, b->objective_file[i], 13)) return -1;
    if (rd_i32(d, len, &o, &b->nmarker) || b->nmarker < 0 || b->nmarker > 10) return -1;
    for (int i = 0; i < b->nmarker; i++) {
        brf_marker *m = &b->marker[i];
        if (rd_f32(d, len, &o, &m->x) || rd_f32(d, len, &o, &m->y) || rd_str(d, len, &o, m->icon, 13) ||
            rd_str(d, len, &o, m->label, 30)) return -1;
        for (char *c = m->icon; *c; c++) *c = (char)toupper((unsigned char)*c);
    }
    for (int i = 0; i < 4; i++) if (rd_f32(d, len, &o, &b->sit_rect[i])) return -1;
    if (rd_str(d, len, &o, b->situation, BRF_TEXT)) return -1;
    if (rd_i32(d, len, &o, &b->squad) || rd_i32(d, len, &o, &b->nnarration) || b->nnarration < 0 || b->nnarration > 32) return -1;
    for (int i = 0; i < b->nnarration; i++) {
        brf_narration *n = &b->narration[i];
        if (rd_i32(d, len, &o, &n->seconds) || rd_str(d, len, &o, skip, 4) || rd_str(d, len, &o, n->wav, 13)) return -1;
        char *dot = strchr(n->wav, '.');
        if (dot) *dot = 0;
    }
    return 0;
}
