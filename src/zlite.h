/*
 * zlite.h - writing zlib streams for the game's save file (CHARINFO.HG1 is zlib-compressed: HGSHELL links zlib's
 * compress / uncompress, FUN_1018cb70 / FUN_10195cc0). Stored blocks only, which zlib's uncompress reads like any
 * other; reading uses unpack_zlib (unpack.h).
 */
#ifndef HG_ZLITE_H
#define HG_ZLITE_H

#include <stddef.h>
#include <stdint.h>

/* a zlib stream of stored blocks; malloc'd, *outlen its size. NULL on failure. */
uint8_t *zl_compress_stored(const uint8_t *in, size_t inlen, size_t *outlen);

#endif
