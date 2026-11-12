/*
 * raster.h - the port's CPU triangle rasterizer: 32-bit colour, float depth, perspective-correct texture
 * coordinates, nearest texel, wrap addressing, alpha test (a texel with alpha 0 is not drawn).
 */
#ifndef HG_RASTER_H
#define HG_RASTER_H

#include <stdint.h>

typedef struct {
    int       w, h;
    uint32_t *color;    /* 0xAARRGGBB */
    float    *depth;    /* 1/w; larger = nearer */
    uint32_t  fog_rgb;  /* colour fog blends toward */
    /* SOFTWARE look: texel index -> luma row -> palette. NULL for the 3D look. */
    const uint8_t (*pal)[3];
    const uint8_t  *luma;      /* 32 rows x 256 (a TBL file) */
    int             bilinear;  /* 3D look: filter textures */
} raster_target;

typedef struct {
    int             w, h;
    const uint32_t *texels;   /* 0xAARRGGBB (3D look) */
    const uint8_t  *idx;      /* palette indices (SOFTWARE look, XEL); used when the target has a luma table */
} raster_texture;

typedef struct {
    float x, y;       /* screen pixels */
    float iw;         /* 1 / clip w */
    float u, v;       /* texture coordinates (texture repeats per 1.0) */
    float r, g, b;    /* light, 1.0 = the texel as is (clamped at 1, as a modulated 8-bit vertex colour) */
    float fog;        /* visibility: 1 clear, 0 fully fogged */
    float lum;        /* SOFTWARE look: I76DRAW's table index, 0..31 (fractional; interpolated, truncated; TBL row 31 - it) */
} raster_vertex;

int  raster_init(raster_target *t, int w, int h);
void raster_free(raster_target *t);
void raster_clear(raster_target *t, uint32_t color);
/* tex NULL: flat colour (argb) */
void raster_triangle(raster_target *t, const raster_vertex *a, const raster_vertex *b, const raster_vertex *c,
                     const raster_texture *tex, uint32_t argb);

#endif
