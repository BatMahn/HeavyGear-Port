/*
 * tex.h - texture records.
 *   TEX (the hardware renderers' textures): "1.22", u16 type, u16 w, u16 h, u16 n, n x { B, G, R, A }, w*h indices.
 *       The Titanium edition's layout (see RE_NOTES "Textures"); all 2,255 archive + patch files check.
 *   XEL (the software renderer's texels): u16 w, u16 h, w*h indices into the game palette.
 */
#ifndef HG_TEX_H
#define HG_TEX_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int       w, h;
    int       type;         /* TEX: the record's type field (0, 1, 3) */
    int       npal;
    uint8_t   pal[256][4];  /* TEX: B, G, R, A as stored */
    uint8_t  *idx;          /* w*h, malloc'd */
} hg_texture;

int  tex_parse_tex(const uint8_t *d, size_t len, hg_texture *t);
int  tex_parse_xel(const uint8_t *d, size_t len, hg_texture *t);
void tex_free(hg_texture *t);

#endif
