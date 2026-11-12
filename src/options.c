#include "options.h"

#include "fsutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const off_on[2] = {"Off", "On"};
static const char *const di_items[3] = {"Low", "Normal", "High"};
static const char *const ds_items[6] = {"Off", "Headphones", "Mono Speakers", "Quad Speakers", "Stereo Speakers",
                                        "Surround Speakers"};
static const char *const dy_items[2] = {"Software 640", "Direct3D"};
static const char *const stems[OPT_COUNT] = {"B64DI", "B64IN", "B64UA", "B64PA", "B64SH", "B64ML", "B64OT", "B64ST",
                                             "B64TF", "B64DS", "B64DY"};

const char *options_stem(int k)
{
    return k >= 0 && k < OPT_COUNT ? stems[k] : "";
}

int options_items(int k, const char *const **text)
{
    if (k == OPT_DI) { *text = di_items; return 3; }
    if (k == OPT_DS) { *text = ds_items; return 6; }
    if (k == OPT_DY) { *text = dy_items; return 2; }
    *text = off_on;
    return 2;
}

static int rd32(const uint8_t *p) { return (int)((unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24); }
static void wr32(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* the hgsnd i32 a spinner sits in (0x100e3c57), -1 for the hgdif bytes */
static int snd_off(int k)
{
    switch (k) {
    case OPT_PA: return 0x28;
    case OPT_SH: return 0x30;
    case OPT_ML: return 0x2c;
    case OPT_OT: return 0x14;
    case OPT_ST: return 0x38;
    case OPT_TF: return 0x3c;
    case OPT_DS: return 0x40;
    default: return -1;
    }
}

void options_from_blocks(options *o)
{
    o->sel[OPT_DI] = o->dif[5] <= 2 ? o->dif[5] : 1;
    o->sel[OPT_IN] = o->dif[1] != 0;
    o->sel[OPT_UA] = o->dif[0] != 0;
    for (int k = OPT_PA; k <= OPT_DS; k++) {
        int v = rd32(o->snd + snd_off(k));
        o->sel[k] = k == OPT_DS ? (v >= 0 && v <= 5 ? v : 0) : v != 0;
    }
    o->sel[OPT_DY] = rd32(o->snd + 0x58) >= 0;
}

static int load_block(const char *dir, const char *name, uint8_t *dst, size_t n)
{
    char path[1200];
    memset(dst, 0, n);
    if (fs_find_ci(dir, name, path, sizeof path)) return 0;
    size_t len;
    uint8_t *d = fs_read_all(path, &len);
    if (d) memcpy(dst, d, len < n ? len : n);
    free(d);
    return d != NULL;
}

void options_load(options *o, const char *game_dir)
{
    memset(o, 0, sizeof *o);
    load_block(game_dir, "hgdif.cfg", o->dif, sizeof o->dif);
    o->snd_found = load_block(game_dir, "hgsnd.cfg", o->snd, sizeof o->snd);
    options_from_blocks(o);
}

static int save_block(const char *dir, const char *name, const uint8_t *src, size_t n)
{
    char path[1200];
    if (fs_find_ci(dir, name, path, sizeof path)) snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(src, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}

int options_save(options *o, const char *game_dir)
{
    o->dif[5] = (uint8_t)o->sel[OPT_DI];
    o->dif[1] = (uint8_t)(o->sel[OPT_IN] != 0);
    o->dif[0] = (uint8_t)(o->sel[OPT_UA] != 0);
    for (int k = OPT_PA; k <= OPT_DS; k++) wr32(o->snd + snd_off(k), o->sel[k]);
    /* 0x100e3c57: the Graphics Type's item at +0x58, the renderer's name at +0x48 (mcga.dll only for item -1) */
    wr32(o->snd + 0x58, o->sel[OPT_DY] ? 0 : -2);
    memset(o->snd + 0x48, 0, 16);
    memcpy(o->snd + 0x48, "vesa480.dll", 11);
    wr32(o->snd + 0x18, o->sel[OPT_OT] != 0);   /* OT goes to all three (0x100e3c57's local_78 / 74 / 58) */
    wr32(o->snd + 0x34, o->sel[OPT_OT] != 0);
    int a = save_block(game_dir, "hgdif.cfg", o->dif, sizeof o->dif);
    int b = save_block(game_dir, "hgsnd.cfg", o->snd, sizeof o->snd);
    return a || b ? -1 : 0;
}

int options_difficulty(const options *o)
{
    return o->dif[5] <= 2 ? o->dif[5] : 1;
}

float options_internal_scale(const options *o)
{
    static const float m[3] = {12.0f, 6.0f, 1.0f};
    return m[options_difficulty(o)];
}

int options_invulnerable(const options *o)
{
    return o->dif[1] != 0;
}

int options_unlimited_ammo(const options *o)
{
    return o->dif[0] != 0;
}

int options_snd_i32(const options *o, int off)
{
    if (off < 0 || off + 4 > (int)sizeof o->snd) return 0;
    return (int)((uint32_t)o->snd[off] | (uint32_t)o->snd[off + 1] << 8 | (uint32_t)o->snd[off + 2] << 16 |
                 (uint32_t)o->snd[off + 3] << 24);
}

int options_graphics_3d(const options *o)
{
    if (!o->snd_found) return -1;
    return options_snd_i32(o, 0x58) >= 0;
}
