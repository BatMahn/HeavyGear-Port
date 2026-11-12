#include "career.h"

#include "unpack.h"
#include "zlite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t rd32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int career_class(int type)
{
    return type == 0 || type == 1 ? 1 : type == 2 ? 0 : 2;
}

/* a string: i32 length, i32, UTF-16 x (length + 1) when length > 0 (FUN_1002609f) */
static int rd_str(const uint8_t *d, size_t len, size_t *o, char *out)
{
    if (*o + 8 > len) return -1;
    int32_t n = rd32(d + *o);
    *o += 8;
    out[0] = 0;
    if (n <= 0) return 0;
    if (*o + (size_t)n * 2 + 2 > len) return -1;
    int k = 0;
    for (int i = 0; i < n; i++) {
        unsigned c = d[*o + 2 * i] | d[*o + 2 * i + 1] << 8;
        if (k < CAREER_NAME - 1) out[k++] = (char)(c < 256 ? c : '?');
    }
    out[k] = 0;
    *o += (size_t)n * 2 + 2;
    return 0;
}

static size_t wr_str(uint8_t *d, const char *s)
{
    int32_t n = (int32_t)strlen(s);
    if (d) { wr32(d, n); wr32(d + 4, n); }
    if (!n) return 8;
    if (d) {
        for (int i = 0; i <= n; i++) { d[8 + 2 * i] = (uint8_t)s[i]; d[9 + 2 * i] = 0; }
    }
    return 8 + (size_t)n * 2 + 2;
}

static int rd_ints(const uint8_t *d, size_t len, size_t *o, int32_t *v, int n)
{
    if (*o + (size_t)n * 4 > len) return -1;
    for (int i = 0; i < n; i++) v[i] = rd32(d + *o + 4 * i);
    *o += (size_t)n * 4;
    return 0;
}

static int rd_char(const uint8_t *d, size_t len, size_t *o, career_char *c)
{
    memset(c, 0, sizeof *c);
    int32_t t;
    if (rd_str(d, len, o, c->name) || rd_str(d, len, o, c->desc) || rd_ints(d, len, o, &t, 1)) return -1;
    c->type = t;
    if (rd_ints(d, len, o, c->stat, 26) || rd_ints(d, len, o, c->a8c, 8) || rd_ints(d, len, o, c->aac, 14) ||
        rd_ints(d, len, o, c->flag, 22)) return -1;
    if (*o + 200 + 76 + 128 > len) return -1;
    memcpy(c->r25c, d + *o, 200); *o += 200;
    for (int i = 0; i < 19; i++) { int32_t v = rd32(d + *o + 4 * i); memcpy(&c->front[i], &v, 4); }
    *o += 76;
    memcpy(c->b13c, d + *o, 128); *o += 128;
    for (int i = 0; i < 10; i++) if (rd_str(d, len, o, c->str[i])) return -1;
    return 0;
}

static size_t wr_char(uint8_t *d, const career_char *c)
{
    size_t o = 0;
    o += wr_str(d ? d + o : NULL, c->name);
    o += wr_str(d ? d + o : NULL, c->desc);
    if (d) {
        wr32(d + o, c->type);
        for (int i = 0; i < 26; i++) wr32(d + o + 4 + 4 * i, c->stat[i]);
        for (int i = 0; i < 8; i++) wr32(d + o + 0x6c + 4 * i, c->a8c[i]);
        for (int i = 0; i < 14; i++) wr32(d + o + 0x8c + 4 * i, c->aac[i]);
        for (int i = 0; i < 22; i++) wr32(d + o + 0xc4 + 4 * i, c->flag[i]);
        memcpy(d + o + 0x11c, c->r25c, 200);
        for (int i = 0; i < 19; i++) { int32_t v; memcpy(&v, &c->front[i], 4); wr32(d + o + 0x1e4 + 4 * i, v); }
        memcpy(d + o + 0x230, c->b13c, 128);
    }
    o += 0x2b0;
    for (int i = 0; i < 10; i++) o += wr_str(d ? d + o : NULL, c->str[i]);
    return o;
}

void career_free(career *k)
{
    free(k->c);
    memset(k, 0, sizeof *k);
}

int career_load(career *k, const char *path)
{
    memset(k, 0, sizeof *k);
    k->last[0] = k->last[1] = k->last[2] = -1;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n || n < 20) { fclose(f); free(d); return -1; }
    fclose(f);
    int count = rd32(d);
    size_t ulen = (size_t)(uint32_t)rd32(d + 16);
    for (int i = 0; i < 3; i++) k->last[i] = rd32(d + 4 + 4 * i);
    if (count < 0 || count > 1000 || ulen > (1u << 24)) { free(d); return -1; }
    uint8_t *u = malloc(ulen ? ulen : 1);
    long got = count ? unpack_zlib(d + 20, (size_t)n - 20, u, ulen) : 0;
    if (!u || got < 0) { free(d); free(u); return -1; }
    free(d);
    k->c = calloc((size_t)count + 1, sizeof *k->c);
    size_t o = 0;
    for (int i = 0; i < count; i++)
        if (rd_char(u, (size_t)got, &o, &k->c[i])) { free(u); career_free(k); return -1; }
    k->count = count;
    free(u);
    return 0;
}

int career_save(const career *k, const char *path)
{
    size_t ulen = 0;
    for (int i = 0; i < k->count; i++) ulen += wr_char(NULL, &k->c[i]);
    uint8_t *u = malloc(ulen ? ulen : 1);
    if (!u) return -1;
    size_t o = 0;
    for (int i = 0; i < k->count; i++) o += wr_char(u + o, &k->c[i]);
    size_t zlen = 0;
    uint8_t *z = k->count ? zl_compress_stored(u, ulen, &zlen) : NULL;
    free(u);
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(z); return -1; }
    uint8_t h[20];
    wr32(h, k->count);
    for (int i = 0; i < 3; i++) wr32(h + 4 + 4 * i, k->last[i]);
    wr32(h + 16, (int32_t)ulen);
    int ok = fwrite(h, 1, 20, f) == 20 && (!z || fwrite(z, 1, zlen, f) == zlen);
    ok &= fclose(f) == 0;
    free(z);
    if (!ok) { remove(tmp); return -1; }
    remove(path);
    return rename(tmp, path) ? -1 : 0;
}

int career_new_story(career *k, const char *name)
{
    career_char *n = realloc(k->c, ((size_t)k->count + 1) * sizeof *n);
    if (!n) return -1;
    k->c = n;
    career_char *c = &k->c[k->count];
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    c->type = 2;
    /* FUN_10110eaa: the story's start (stat[7] 1, [10] missions reached 1, [11] 0, [12] current mission 1, [16] 1,
     * [8] = 1 << stat[7]); the rank (FUN_101112bb, type 2) 3; the 10 x 20-byte records start with 1
     * (DAT_101aef80); the [8] block (DAT_101af048) and the front line (the story's table row 0) are zero */
    c->stat[7] = 1;
    c->stat[10] = 1;
    c->stat[12] = 1;
    c->stat[16] = 1;
    c->stat[8] = 1 << c->stat[7];
    c->stat[CAREER_RANK] = 3;
    for (int i = 0; i < 10; i++) c->r25c[i * 20] = 1;
    return k->count++;
}

void career_delete(career *k, int i)
{
    if (i < 0 || i >= k->count) return;
    memmove(&k->c[i], &k->c[i + 1], (size_t)(k->count - i - 1) * sizeof *k->c);
    k->count--;
}
