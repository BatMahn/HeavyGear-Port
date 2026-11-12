/*
 * skygnd.h - per-mission sky, light and fog settings (SKYGND.PAR). Parsed as HGSIM 0x1008ceab does: one sscanf per line
 * (text after '#' dropped), the first line whose mission matches the mission's first 4 letters wins:
 *   mission=%4s globalintensity=%d hardwareintensity=%d hardwareambient=%d gintensity=%d gambient=%d skytexture=%s
 *   horizontexture=%s hardwarehorizon=%s horizontile=%f skycolor=%d,%d,%d,%d,%d,%d fogswitch=%d fogcolor=%d,%d,%d,%d
 *   skyspeed=%f density=%f,%f,%f hambient=%d,%d,%d l1=%d,%d,%d,%d,%d,%d l2=%d,%d,%d,%d,%d,%d gl=%d,%d,%d
 * The sim opens it with a plain fopen in the game folder (not through the archive).
 */
#ifndef HG_SKYGND_H
#define HG_SKYGND_H

typedef struct {
    int   found;
    int   globalintensity, hardwareintensity, hardwareambient, gintensity, gambient;
    char  skytexture[32], horizontexture[32], hardwarehorizon[32];
    float horizontile;
    int   skycolor[6];     /* sw above, hw sky alpha, sw below, sky tile rate (<= 256), hw above, hw below */
    int   fogswitch;
    int   fogcolor[4];
    float skyspeed;
    float density[3];      /* Y, M, Z */
    int   hambient[3];     /* clamped 0..255 */
    int   l1[6], l2[6];    /* r, g, b (clamped 0..255), direction x, y, z */
    float l1dir[3], l2dir[3];   /* normalized (0x1008ceab) */
    int   gl[3];
} skygnd;

/* Load the mission's line from `path`. Returns 0 if found. */
int skygnd_load(const char *path, const char *mission, skygnd *s);

#endif
