/*
 * material.h - a mission's texture and material lookups (TLT / MLT files, listed by the TLUT / MLUT chunks of its
 * xxx_TABL record). See RE_NOTES "Materials".
 *   TLT entry (72 bytes, from +0x2c, count at +0x28): char texture[32], char label[32], i32, u32
 *   MLT entry (96 bytes, same header):              char texture[32], char name[32], u32 type (+0x40), u32 (+0x44),
 *                                                   u32 (+0x48), ..., u8 rgb[3] (+0x5d)
 * A polygon's label (upper-cased) -> TLT -> key -> MLT (keyed by it) -> the texture is the MLT's name (+0x20,
 * HGSIM 0x100bef4c).
 */
#ifndef HG_MATERIAL_H
#define HG_MATERIAL_H

#include "hgprj.h"

#include <stdint.h>

typedef struct {
    char    texture[33];
    char    label[33];
} tlt_entry;

typedef struct {
    char     texture[33];
    char     name[33];
    uint32_t type, p44, p48;
    uint32_t p4c, p50, p54;   /* type 3: first frame, last frame, rate divisor (0x100bef4c) */
    uint8_t  rgb[3];
} mlt_entry;

typedef struct {
    tlt_entry *tlt; int ntlt;
    mlt_entry *mlt; int nmlt;
} material_set;

/* Add one TLT or MLT file's entries (ext "TLT" / "MLT"). Returns 0 if loaded. */
int  material_load(material_set *s, hg_archive *a, const char *name, const char *ext);
/* Load every TLUT / MLUT listed in a BWD record. Returns the number of files loaded. */
int  material_load_tabl(material_set *s, hg_archive *a, const uint8_t *bwd, size_t len);
const tlt_entry *material_find_label(const material_set *s, const char *label);
const mlt_entry *material_find_texture(const material_set *s, const char *texture);
void material_free(material_set *s);

#endif
