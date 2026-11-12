/*
 * shp.h - SHP 1.10 shapes (the HUD's art) and FNT fonts.
 *
 * SHP: "1.10", u32 count, then count x { u32 offset, u32 palette offset }. A shape at its offset:
 *   u16, u16 (the bounding box's height - 1, width - 1), u16, u16, i32 x0, y0, x1, y1 (the pixels' extent relative to
 *   the shape's hot spot), then run-length rows: 0 ends a row, 1 n skips n pixels, an even code 2n repeats the next
 *   byte n times, an odd code 2n + 1 copies the next n bytes. Skipped pixels are transparent.
 * FNT: "1.", u32 glyph count, u32 height, u32, count x u32 glyph offsets; a glyph is u32 width then width * height
 *   bytes, row by row, 0xff transparent. The sim draws a glyph's bytes through a 16-entry colour table whose entry
 *   0x0e is the text colour (DAT_107ca800; identity otherwise).
 */
#ifndef HG_SHP_H
#define HG_SHP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int      w, h;       /* pixel extent */
    int      x0, y0;     /* top-left relative to the hot spot */
    uint8_t *px;         /* w*h palette indices */
    uint8_t *mask;       /* w*h: 1 = drawn */
} hg_shape;

typedef struct {
    int       height, count;
    int      *width;     /* per glyph */
    uint8_t **glyph;     /* per glyph, width * height bytes (pointers into data) */
    uint8_t  *data;
} hg_font;

/* first shape of a SHP record; 0 on success */
int  shp_parse(const uint8_t *d, size_t len, int index, hg_shape *s);
void shp_free(hg_shape *s);
int  fnt_parse(uint8_t *d, size_t len, hg_font *f);   /* takes ownership of d */
void fnt_free(hg_font *f);

#endif
