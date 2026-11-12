#include "zlite.h"

#include <stdlib.h>
#include <string.h>

uint8_t *zl_compress_stored(const uint8_t *in, size_t inlen, size_t *outlen)
{
    size_t nblk = inlen / 65535 + 1;
    uint8_t *o = malloc(2 + nblk * 5 + inlen + 4);
    if (!o) return NULL;
    size_t p = 0;
    o[p++] = 0x78; o[p++] = 0x01;
    size_t left = inlen, at = 0;
    do {
        unsigned n = left > 65535 ? 65535 : (unsigned)left;
        o[p++] = left <= 65535 ? 1 : 0;
        o[p++] = (uint8_t)n; o[p++] = (uint8_t)(n >> 8);
        o[p++] = (uint8_t)~n; o[p++] = (uint8_t)(~n >> 8);
        memcpy(o + p, in + at, n);
        p += n; at += n; left -= n;
    } while (left);
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < inlen; i++) { a = (a + in[i]) % 65521; b = (b + a) % 65521; }
    uint32_t ad = b << 16 | a;
    o[p++] = (uint8_t)(ad >> 24); o[p++] = (uint8_t)(ad >> 16); o[p++] = (uint8_t)(ad >> 8); o[p++] = (uint8_t)ad;
    *outlen = p;
    return o;
}
