/*
 * wtb.h - Heavy Gear polygon models (WTB files, "WTBO" objects). Layout traced from HGSIM.DLL 0x100c5e73; see
 * docs/RE_NOTES.md "Models".
 */
#ifndef HG_WTB_H
#define HG_WTB_H

#include <stddef.h>
#include <stdint.h>

#define WTB_MAX_POLY 7

typedef struct {
    float x, y, z;
    float nx, ny, nz;     /* as the engine keeps them: the stored doubles negated */
    float u, v;
} wtb_vertex;

typedef struct {
    char     label[17];   /* material label, resolved through the TLT tables */
    uint16_t f10;         /* +0x10, meaning unconfirmed */
    uint8_t  rgb[3];      /* +0x13..0x15 */
    float    nx, ny, nz;  /* face normal, negated as the engine loads it */
    int      n;
    uint16_t idx[WTB_MAX_POLY];
} wtb_poly;

typedef struct {
    char        name[17];
    uint16_t    flags;
    int         legacy;   /* 1: the MW2 DOS layout (i32 vertices, colour polygons) - see RE_NOTES */
    int         nverts, npolys;
    wtb_vertex *v;
    wtb_poly   *p;
} wtb_object;

typedef struct {
    wtb_object *obj;
    int         count;
} wtb_model;

/* Parse every object in a WTB record. Returns 0, or -1 if the record is malformed. */
int  wtb_parse(const uint8_t *d, size_t len, wtb_model *out);
void wtb_free(wtb_model *m);

#endif
