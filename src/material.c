#include "material.h"

#include "bwd.h"
#include "bytes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void cstr(char *dst, const uint8_t *src, int n)
{
    memcpy(dst, src, (size_t)n);
    dst[n] = 0;
}

int material_load(material_set *s, hg_archive *a, const char *name, const char *ext)
{
    size_t len;
    uint8_t *d = hga_load(a, name, ext, &len, NULL);
    if (!d) return -1;
    int tlt = !strcmp(ext, "TLT");
    size_t es = tlt ? 72 : 96;
    uint32_t n = len >= 0x2c ? rd_u32(d + 0x28) : 0;
    if (0x2c + n * es > len) { free(d); return -1; }
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = d + 0x2c + i * es;
        if (tlt) {
            s->tlt = realloc(s->tlt, (size_t)(s->ntlt + 1) * sizeof *s->tlt);
            tlt_entry *t = &s->tlt[s->ntlt++];
            cstr(t->texture, e, 32);
            cstr(t->label, e + 32, 32);
        } else {
            s->mlt = realloc(s->mlt, (size_t)(s->nmlt + 1) * sizeof *s->mlt);
            mlt_entry *m = &s->mlt[s->nmlt++];
            cstr(m->texture, e, 32);
            cstr(m->name, e + 32, 32);
            m->type = rd_u32(e + 0x40);
            m->p44 = rd_u32(e + 0x44);
            m->p48 = rd_u32(e + 0x48);
            m->p4c = rd_u32(e + 0x4c);
            m->p50 = rd_u32(e + 0x50);
            m->p54 = rd_u32(e + 0x54);
            memcpy(m->rgb, e + 0x5d, 3);
        }
    }
    free(d);
    return 0;
}

int material_load_tabl(material_set *s, hg_archive *a, const uint8_t *bwd, size_t len)
{
    bwd_chunk c[512];
    int n = bwd_chunks(bwd, len, c, 512), loaded = 0;
    for (int i = 0; i < n && i < 512; i++) {
        int t = !strcmp(c[i].tag, "TLUT"), m = !strcmp(c[i].tag, "MLUT");
        if ((!t && !m) || c[i].size < 3) continue;
        char nm[16];
        snprintf(nm, sizeof nm, "%.10s", (const char *)c[i].data + 2);
        if (!material_load(s, a, nm, t ? "TLT" : "MLT")) loaded++;
    }
    return loaded;
}

/* The engine files each entry in a tree keyed by its first letter, its first four bytes and then strcmp
 * (0x100bf2f7), a repeated name being dropped - so the first loaded entry wins - and looks a polygon's label up upper-
 * cased (0x100ea690) the same way (0x100c5c07). Every table entry in the game is upper case, so comparing without
 * regard to case finds the same entries. */
const tlt_entry *material_find_label(const material_set *s, const char *label)
{
    for (int i = 0; i < s->ntlt; i++)
        if (!strcasecmp(s->tlt[i].label, label)) return &s->tlt[i];
    return NULL;
}

const mlt_entry *material_find_texture(const material_set *s, const char *texture)
{
    for (int i = 0; i < s->nmlt; i++)
        if (!strcasecmp(s->mlt[i].texture, texture)) return &s->mlt[i];
    return NULL;
}

void material_free(material_set *s)
{
    free(s->tlt);
    free(s->mlt);
    memset(s, 0, sizeof *s);
}
