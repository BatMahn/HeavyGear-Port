/* test_rmg.c - the random mission generator's pieces that need no game folder: the C library's rand() as the
 * original links it, and B04's mission type lists (0x100fcfcf) */
#include "../src/career.h"
#include "../src/randmsn.h"
#include "../src/tour.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    rmg_srand(1);   /* MSVCRT: srand(1) gives 41, 18467, 6334 */
    int a = rmg_rand(), b = rmg_rand(), c = rmg_rand();
    CHECK(a == 41 && b == 18467 && c == 6334, "rand: %d %d %d", a, b, c);
    career_char ch;
    memset(&ch, 0, sizeof ch);
    ch.stat[11] = -1;
    int t[16], n = tour_mission_types(&ch, 0, t);
    CHECK(n == 2 && t[0] == 2 && t[1] == 7, "new game's types: %d", n);
    ch.stat[11] = 3; ch.stat[7] = 8;
    n = tour_mission_types(&ch, 0, t);
    CHECK(n == 7 && t[6] == 3, "stat 11 = 3, unit 8: %d", n);
    CHECK(tour_faction(0) == 1 && tour_faction(1) == 0 && tour_faction(2) == 1, "factions");
    printf(fails ? "test_rmg: %d failures\n" : "test_rmg: ok\n", fails);
    return fails != 0;
}
