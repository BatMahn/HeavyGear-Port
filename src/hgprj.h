/*
 * hgprj.h - Heavy Gear's resource archive: HEAVYG.MAP (the index) + HEAVYG.PRJ (the records), and the 1.2 patch's
 * loose-file folder that overrides them.
 *
 * HEAVYG.MAP (all integers little-endian), as HGSIM.DLL reads it (sysfile.c 0x100a4a91):
 *   u32  entry_count (19,545 retail)
 *   u32  type_count, then type_count x { char tag[4], u32 hash_size }   e.g. "WTB\0" 10345 (one hash list per type)
 *   u32  pma_count, then pma_count x 12 bytes { tag[4], u32, u32 }      (0 retail; not used further here)
 *   u32  codec_count, then codec_count x char dll[16]                 "pkware.dll", "zlib.dll" (LoadLibrary'd,
 *                                                                       "Uncompress" export, 0x100a4980)
 *   entry_count x 20 bytes { char name[8], char type[4], u32 prj_offset, char[4] ("NONE") }
 *   The type tag is the extension or extension + language letter: "WAVX" / "PCXX" are the English voice and
 *   screen-art records (1,053 / 630), "WAV" / "PCX" the language-neutral ones.
 * HEAVYG.PRJ record at prj_offset:
 *   char src[12]     the art group's directory, e.g. "C:\PRJ\AWAV" (NOT NUL-terminated when it fills the field)
 *   u16  method      0 stored, 1 PKWARE implode, 2 zlib
 *   u32  size        unpacked size
 *   u32  packed      stored size
 *   packed bytes
 * Checked on the retail 1.0 disc: all 19,545 entries unpack to their stated size.
 *
 * (src is only the art group the record came from; the map's tag is what the game looks records up by.)
 *
 * Lookup order (1.2 HGSIM.DLL, syscache.c 0x100a2bc9; L = the language letter, lang.ini "language = x" upper-cased,
 * 'X' when the file is missing, 0x100a21be):
 *   Patch\name.EXTL, Patch\name.EXT, name.EXTL, name.EXT (loose in the game folder), then the archive: type EXTL,
 *   then type EXT (0x100a4779). Patch 1.2 installs the Patch folder (1,379 files).
 */
#ifndef HG_HGPRJ_H
#define HG_HGPRJ_H

#include <stddef.h>
#include <stdint.h>

typedef struct hg_archive hg_archive;

typedef struct {
    char     name[9];     /* lower-case 8.3 stem */
    char     ext[4];      /* upper-case extension, e.g. "WAV" */
    char     type[6];     /* the record's type: the extension, or extension + language letter ("WAVX") */
    uint32_t offset;
} hga_entry;

/* Open the archive in the game folder `dir` (HEAVYG.MAP + HEAVYG.PRJ, any case; its Patch folder and lang.ini are
 * found there too). Returns NULL with a message in err on failure. */
hg_archive *hga_open(const char *dir, char *err, size_t errlen);
/* the game folder it was opened from */
const char *hga_dir(const hg_archive *a);
char        hga_language(const hg_archive *a);   /* 'X' English, 'S', 'G', ... */
/* Use `dir` as the Patch folder when the game folder has none (the port ships patch 1.2's files in
 * gamedata/patch12/Patch). Returns 0 if `dir` exists. */
int         hga_set_patch_fallback(hg_archive *a, const char *dir);
int         hga_has_patch(const hg_archive *a);
void        hga_close(hg_archive *a);

int              hga_count(const hg_archive *a);
const hga_entry *hga_entry_at(const hg_archive *a, int i);
/* index of the record name of the given type (case-insensitive), or -1 */
int              hga_find(const hg_archive *a, const char *name, const char *type);

enum { HGA_FROM_ARCHIVE, HGA_FROM_PATCH, HGA_FROM_FOLDER };
/* Load a resource the game's way (order above). Returns a malloc'd buffer (with one extra NUL byte after the data)
 * and its size, or NULL. *from (may be NULL) is one of the HGA_FROM_ values. */
uint8_t *hga_load(hg_archive *a, const char *name, const char *ext, size_t *len, int *from);
/* Load archive entry i only (no patch lookup). */
uint8_t *hga_load_index(hg_archive *a, int i, size_t *len);

#endif
