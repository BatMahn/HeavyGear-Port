/* pcx.h - 8-bit PCX (the shell's art): ZSoft header (128 bytes), RLE rows, then 0x0c and a 256 x RGB palette. */
#ifndef HG_PCX_H
#define HG_PCX_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    int      w, h;
    int      bpl;           /* the file's bytes per line (w rounded up to even) */
    uint8_t *px;            /* w*h palette indices */
    uint8_t  pal[256][3];
    int      has_pal;
} hg_pcx;
int  pcx_parse(const uint8_t *d, size_t len, hg_pcx *p);
void pcx_free(hg_pcx *p);
#endif
