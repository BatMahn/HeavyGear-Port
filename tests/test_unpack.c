/* test_unpack - the zlib decoder against streams made by zlib itself (dynamic, stored and fixed-code blocks); the
 * stored-block writer and the save file (CHARINFO.HG1) through a round trip */
#include "../src/career.h"
#include "../src/unpack.h"
#include "../src/zlite.h"

#include <stdlib.h>

#include <stdio.h>
#include <string.h>

#include "zlib_vectors.h"

static int one(const char *what, const unsigned char *z, size_t n)
{
    static unsigned char out[sizeof raw_data + 16];
    long got = unpack_zlib(z, n, out, sizeof out);
    int ok = got == (long)sizeof raw_data && !memcmp(out, raw_data, sizeof raw_data);
    printf("zlib %-8s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}

static int roundtrip(void)
{
    size_t zl;
    uint8_t *z = zl_compress_stored(raw_data, sizeof raw_data, &zl);
    static unsigned char out[sizeof raw_data + 16];
    long got = z ? unpack_zlib(z, zl, out, sizeof out) : -1;
    int ok = got == (long)sizeof raw_data && !memcmp(out, raw_data, sizeof raw_data);
    free(z);
    printf("zlib stored writer %s\n", ok ? "ok" : "FAIL");
    /* a new story game, saved and read back */
    career k = {0}, r;
    k.last[0] = k.last[1] = k.last[2] = -1;
    int i = career_new_story(&k, "Mike");
    k.c[i].stat[CAREER_MISSION] = 7;
    snprintf(k.c[i].desc, sizeof k.c[i].desc, "test");
    const char *path = "test_charinfo.hg1";
    int ok2 = i == 0 && !career_save(&k, path) && !career_load(&r, path) && r.count == 1 &&
              !memcmp(&r.c[0], &k.c[0], sizeof r.c[0]) && r.c[0].type == 2 && r.c[0].stat[CAREER_RANK] == 3;
    remove(path);
    career_free(&k);
    if (ok2) career_free(&r);
    printf("save file round trip %s\n", ok2 ? "ok" : "FAIL");
    return !ok + !ok2;
}

int main(void)
{
    int bad = roundtrip() + one("dynamic", z_data, sizeof z_data) + one("stored", z_stored, sizeof z_stored) +
              one("fixed", z_fixed, sizeof z_fixed);
    unsigned char junk[4] = {0x78, 0x9c, 0xff, 0xff}, out[16];
    if (unpack_zlib(junk, 4, out, sizeof out) >= 0) { printf("zlib junk accepted: FAIL\n"); bad++; }
    return bad;
}
