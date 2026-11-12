#include "skygnd.h"

#include "fsutil.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void norm(const int *d, float *o)
{
    float l = sqrtf((float)(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
    if (l >= 1e-7f) { o[0] = (float)d[0] / l; o[1] = (float)d[1] / l; o[2] = (float)d[2] / l; }
}

int skygnd_load(const char *path, const char *mission, skygnd *s)
{
    memset(s, 0, sizeof *s);
    size_t n;
    char *t = (char *)fs_read_all(path, &n);
    if (!t) return -1;
    int ok = -1;
    for (char *line = strtok(t, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        if (!*line) continue;
        skygnd v;
        char m[8] = {0};
        memset(&v, 0, sizeof v);
        sscanf(line,
               "mission=%4s globalintensity=%d hardwareintensity=%d hardwareambient=%d gintensity=%d gambient=%d "
               "skytexture=%31s horizontexture=%31s hardwarehorizon=%31s horizontile=%f skycolor=%d,%d,%d,%d,%d,%d "
               "fogswitch=%d fogcolor=%d,%d,%d,%d skyspeed=%f density=%f,%f,%f hambient=%d,%d,%d "
               "l1=%d,%d,%d,%d,%d,%d l2=%d,%d,%d,%d,%d,%d gl=%d,%d,%d",
               m, &v.globalintensity, &v.hardwareintensity, &v.hardwareambient, &v.gintensity, &v.gambient,
               v.skytexture, v.horizontexture, v.hardwarehorizon, &v.horizontile, &v.skycolor[0], &v.skycolor[1],
               &v.skycolor[2], &v.skycolor[3], &v.skycolor[4], &v.skycolor[5], &v.fogswitch, &v.fogcolor[0],
               &v.fogcolor[1], &v.fogcolor[2], &v.fogcolor[3], &v.skyspeed, &v.density[0], &v.density[1],
               &v.density[2], &v.hambient[0], &v.hambient[1], &v.hambient[2], &v.l1[0], &v.l1[1], &v.l1[2],
               &v.l1[3], &v.l1[4], &v.l1[5], &v.l2[0], &v.l2[1], &v.l2[2], &v.l2[3], &v.l2[4], &v.l2[5], &v.gl[0],
               &v.gl[1], &v.gl[2]);
        if (strncasecmp(m, mission, 4)) continue;
        /* 0x1008ceab: colours clamped to 0..255, directions normalized, sky tile rate capped at 0x100 */
        for (int k = 0; k < 3; k++) {
            v.hambient[k] = clamp255(v.hambient[k]);
            v.l1[k] = clamp255(v.l1[k]);
            v.l2[k] = clamp255(v.l2[k]);
            v.gl[k] = clamp255(v.gl[k]);
        }
        norm(v.l1 + 3, v.l1dir);
        norm(v.l2 + 3, v.l2dir);
        if (v.skycolor[3] > 0x100) v.skycolor[3] = 0x100;
        if (v.fogswitch < 0) v.fogswitch = 0;   /* hardware path: no fog-down */
        v.found = 1;
        *s = v;
        ok = 0;
        break;
    }
    free(t);
    return ok;
}
