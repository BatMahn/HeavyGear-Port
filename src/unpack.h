/*
 * unpack.h - the two decompressors HEAVYG.PRJ records use.
 *
 * A record's method field (see hgprj.h) picks one:
 *   1  PKWARE Data Compression Library "implode" (the game ships PKWARE.DLL; all 1,419 PCX records use it)
 *   2  zlib stream (RFC 1950 header + RFC 1951 deflate; the game ships ZLIB.DLL)
 * Both are written here from the published formats so the port needs no outside library.
 */
#ifndef HG_UNPACK_H
#define HG_UNPACK_H

#include <stddef.h>
#include <stdint.h>

/* Each returns the number of bytes written to out, or -1 on a malformed or truncated stream / too small a buffer. */
long unpack_explode(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap);
long unpack_zlib(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap);

#endif
