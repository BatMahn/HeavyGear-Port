#include "hgrand.h"

#include <math.h>

static int uni[0x7f], nor[0x7f], at_u, at_n, at_2, ready;

void hg_rand_reset(void)
{
    unsigned x = 0;
    for (int i = 0; i < 0x7f; i++) { x = x * 0x343fdu + 0x269ec3u; uni[i] = (int)((x >> 16) & 0x7fff); }
    double r = sqrt(90.0), k = 2.0 * r / 983010.0;
    for (int i = 0; i < 0x7f; i++) {
        int sum = 0;
        for (int j = 0; j < 0x1e; j++) { x = x * 0x343fdu + 0x269ec3u; sum += (int)((x >> 16) & 0x7fff); }
        nor[i] = (int)(((double)sum * k - r) * 1024.0);
    }
    at_u = at_n = at_2 = 0;
    ready = 1;
}

int hg_rand(int n)
{
    if (!ready) hg_rand_reset();
    int v = uni[at_u];
    if (++at_u == 0x7f) at_u = 0;
    return n > 0 ? v % n : 0;
}

int hg_nrand(void)
{
    if (!ready) hg_rand_reset();
    int v = nor[at_n];
    if (++at_n == 0x7f) at_n = 0;
    return v;
}

int hg_rand2(int n)
{
    if (!ready) hg_rand_reset();
    int v = uni[at_2];
    at_2 = (at_2 + 1) % 0x7f;
    return n > 0 ? v % n : 0;
}
