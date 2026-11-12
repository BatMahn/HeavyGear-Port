/*
 * hgprj.c - HEAVYG.MAP / HEAVYG.PRJ reader and the game's file lookup order. Formats and order in hgprj.h.
 */
#include "hgprj.h"

#include "fsutil.h"
#include "unpack.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct hg_archive {
    FILE      *prj;
    hga_entry *ent;
    int        count;
    char       dir[1024];
    char       patchdir[1024];
    int        has_patch;
    char       lang;
};

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void seterr(char *err, size_t n, const char *msg, const char *arg)
{
    if (err && n) snprintf(err, n, "%s%s%s", msg, arg ? ": " : "", arg ? arg : "");
}

static int cmp_entry(const void *a, const void *b)
{
    const hga_entry *x = a, *y = b;
    int c = strcmp(x->name, y->name);
    return c ? c : strcmp(x->type, y->type);
}

/* lang.ini: "language = x" (0x100a21be: the line's last non-blank character; 'X' with no file) */
static char read_language(const char *dir)
{
    char path[1100];
    if (fs_find_ci(dir, "lang.ini", path, sizeof path)) return 'X';
    size_t n;
    char *t = (char *)fs_read_all(path, &n);
    if (!t) return 'X';
    char c = 'X';
    for (char *line = strtok(t, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (strncmp(line, "language =", 10)) continue;
        char *e = line + strlen(line) - 1;
        while (e > line && (*e == ' ' || *e == '\t')) e--;
        c = (char)toupper((unsigned char)*e);
        break;
    }
    free(t);
    return c;
}

hg_archive *hga_open(const char *dir, char *err, size_t errlen)
{
    char path[1100];
    if (fs_find_ci(dir, "HEAVYG.MAP", path, sizeof path)) { seterr(err, errlen, "HEAVYG.MAP not found in", dir); return NULL; }
    size_t mlen;
    uint8_t *m = fs_read_all(path, &mlen);
    if (!m || mlen < 8) { free(m); seterr(err, errlen, "not a Heavy Gear map file", path); return NULL; }
    uint32_t total = rd32(m), ntypes = rd32(m + 4);
    size_t p = 8 + (size_t)ntypes * 8;
    if (ntypes > 256 || p + 4 > mlen) { free(m); seterr(err, errlen, "bad map header", path); return NULL; }
    p += 4 + (size_t)rd32(m + p) * 12;                       /* pma list */
    if (p + 4 > mlen) { free(m); seterr(err, errlen, "bad map header", path); return NULL; }
    uint32_t ncodec = rd32(m + p);
    p += 4 + (size_t)ncodec * 16;
    if (ncodec > 16 || total > 1000000 || p + (size_t)total * 20 > mlen) {
        free(m); seterr(err, errlen, "bad map index", path); return NULL;
    }

    hg_archive *a = calloc(1, sizeof *a);
    snprintf(a->dir, sizeof a->dir, "%s", dir);
    a->ent = calloc(total ? total : 1, sizeof *a->ent);
    for (uint32_t i = 0; i < total; i++, p += 20) {
        hga_entry *e = &a->ent[a->count++];
        int k;
        for (k = 0; k < 8 && m[p + k]; k++) e->name[k] = (char)tolower(m[p + k]);
        e->name[k] = 0;
        for (k = 0; k < 4 && m[p + 8 + k]; k++) e->type[k] = (char)toupper(m[p + 8 + k]);
        e->type[k] = 0;
        snprintf(e->ext, sizeof e->ext, "%.3s", e->type);
        e->offset = rd32(m + p + 12);
    }
    free(m);
    qsort(a->ent, (size_t)a->count, sizeof *a->ent, cmp_entry);

    if (fs_find_ci(dir, "HEAVYG.PRJ", path, sizeof path) || !(a->prj = fopen(path, "rb"))) {
        seterr(err, errlen, "HEAVYG.PRJ not found in", dir);
        hga_close(a);
        return NULL;
    }
    a->has_patch = !fs_find_ci(dir, "Patch", a->patchdir, sizeof a->patchdir);
    a->lang = read_language(dir);
    return a;
}

void hga_close(hg_archive *a)
{
    if (!a) return;
    if (a->prj) fclose(a->prj);
    free(a->ent);
    free(a);
}

char hga_language(const hg_archive *a) { return a->lang; }
const char *hga_dir(const hg_archive *a) { return a->dir; }
int hga_has_patch(const hg_archive *a) { return a->has_patch; }

int hga_set_patch_fallback(hg_archive *a, const char *dir)
{
    if (a->has_patch) return 0;
    DIR *d = opendir(dir);
    if (!d) return -1;
    closedir(d);
    snprintf(a->patchdir, sizeof a->patchdir, "%s", dir);
    a->has_patch = 1;
    return 0;
}
int hga_count(const hg_archive *a) { return a->count; }
const hga_entry *hga_entry_at(const hg_archive *a, int i) { return i >= 0 && i < a->count ? &a->ent[i] : NULL; }

int hga_find(const hg_archive *a, const char *name, const char *type)
{
    hga_entry key;
    memset(&key, 0, sizeof key);
    for (int k = 0; k < 8 && name[k]; k++) key.name[k] = (char)tolower((unsigned char)name[k]);
    for (int k = 0; k < 5 && type[k]; k++) key.type[k] = (char)toupper((unsigned char)type[k]);
    const hga_entry *e = bsearch(&key, a->ent, (size_t)a->count, sizeof *a->ent, cmp_entry);
    return e ? (int)(e - a->ent) : -1;
}

uint8_t *hga_load_index(hg_archive *a, int i, size_t *len)
{
    if (i < 0 || i >= a->count) return NULL;
    uint8_t h[22];
    if (fseek(a->prj, (long)a->ent[i].offset, SEEK_SET) || fread(h, 1, 22, a->prj) != 22) return NULL;
    unsigned method = h[12] | (h[13] << 8);
    uint32_t size = rd32(h + 14), packed = rd32(h + 18);
    if (size > 64u << 20 || packed > 64u << 20) return NULL;
    uint8_t *raw = malloc(packed ? packed : 1);
    uint8_t *out = malloc((size_t)size + 1);
    if (!raw || !out || fread(raw, 1, packed, a->prj) != packed) { free(raw); free(out); return NULL; }
    long got;
    if (method == 0) { got = packed <= size ? (long)packed : -1; if (got >= 0) memcpy(out, raw, packed); }
    else if (method == 1) got = unpack_explode(raw, packed, out, size);
    else if (method == 2) got = unpack_zlib(raw, packed, out, size);
    else got = -1;
    free(raw);
    if (got != (long)size) { free(out); return NULL; }
    out[size] = 0;
    if (len) *len = size;
    return out;
}

static uint8_t *try_file(const char *dir, const char *name, const char *ext, size_t *len)
{
    char fn[32], path[1100];
    snprintf(fn, sizeof fn, "%s.%s", name, ext);
    if (fs_find_ci(dir, fn, path, sizeof path)) return NULL;
    return fs_read_all(path, len);
}

uint8_t *hga_load(hg_archive *a, const char *name, const char *ext, size_t *len, int *from)
{
    char lext[8];
    snprintf(lext, sizeof lext, "%.3s%c", ext, a->lang);
    uint8_t *d;
    if (from) *from = HGA_FROM_PATCH;
    if (a->has_patch && ((d = try_file(a->patchdir, name, lext, len)) || (d = try_file(a->patchdir, name, ext, len))))
        return d;
    if (from) *from = HGA_FROM_FOLDER;
    if ((d = try_file(a->dir, name, lext, len)) || (d = try_file(a->dir, name, ext, len))) return d;
    if (from) *from = HGA_FROM_ARCHIVE;
    int i = hga_find(a, name, lext);
    if (i < 0) i = hga_find(a, name, ext);
    return hga_load_index(a, i, len);
}
