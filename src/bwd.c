#include "bwd.h"

#include "bytes.h"

#include <string.h>

int bwd_chunks(const uint8_t *rec, size_t len, bwd_chunk *out, int max)
{
    if (len < 12 || memcmp(rec, "BWD", 4)) return -1;
    size_t p = 12;
    int n = 0;
    while (p + 8 <= len) {
        uint32_t sz = rd_u32(rec + p + 4);
        if (sz < 8 || p + sz > len) return -1;
        if (out && n < max) {
            memcpy(out[n].tag, rec + p, 4);
            out[n].tag[4] = 0;
            out[n].data = rec + p + 8;
            out[n].size = sz - 8;
        }
        n++;
        p += sz;
    }
    return p == len ? n : -1;
}
