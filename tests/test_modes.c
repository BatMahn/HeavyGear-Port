/* the front end's other modes: the barracks' contents (src/barracks.c) and the option blocks (src/options.c) */
#include "../src/barracks.h"
#include "../src/options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    career_char c;
    memset(&c, 0, sizeof c);
    c.type = 2;                 /* a new story game: rank 3, unit 1 (FUN_10110eaa) */
    c.stat[2] = 3; c.stat[7] = 1; c.stat[8] = 2;
    shell_barracks b;
    barracks_build(&c, &b);
    CHECK(b.rank_pic == 0x78, "rank picture %#x", b.rank_pic);
    CHECK(b.nunit == 1 && b.unit[0] == 1 && b.unit_sel == 0, "units %d", b.nunit);
    CHECK(strstr(b.stats, "Name:  Edward Scott\n") && strstr(b.stats, "Rank:  Senior Ranger\n"), "stats %s", b.stats);
    CHECK(!strcmp(b.ai, "<NONE>"), "ai %s", b.ai);
    c.flag[1] = c.flag[0] = 1; c.flag[17] = c.flag[18] = 1; c.aac[0] = 2;
    barracks_build(&c, &b);
    CHECK(!strcmp(b.ai, "Speed Increase 2\nArmor Bonus 2\n"), "ai %s", b.ai);
    CHECK(b.nmedal == 1 && b.medal_pic[0] == 0x8e && b.medal_n[0] == 2, "medals %d", b.nmedal);
    c.type = 1; c.stat[8] = (1 << 10) | (1 << 12); c.stat[7] = 12; c.stat[2] = 11;
    barracks_build(&c, &b);
    CHECK(b.nunit == 2 && b.unit[1] == 12 && b.unit_sel == 1 && b.rank_pic == 0x87, "south units %d", b.nunit);
    CHECK(!strcmp(barracks_priority_name(0), "C9") && !strcmp(barracks_priority_name(26), "A1"), "priority names");
    /* the option blocks round trip */
    options o;
    options_load(&o, "/nonexistent");
    CHECK(options_difficulty(&o) == 0 && options_internal_scale(&o) == 12.0f, "empty difficulty");
    o.sel[OPT_DI] = 2; o.sel[OPT_UA] = 1; o.sel[OPT_DS] = 4;
    const char *dir = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    if (!options_save(&o, dir)) {
        options p;
        options_load(&p, dir);
        CHECK(p.sel[OPT_DI] == 2 && p.sel[OPT_UA] == 1 && p.sel[OPT_DS] == 4 && p.dif[5] == 2 && p.dif[0] == 1,
              "reload %d %d %d", p.sel[OPT_DI], p.sel[OPT_UA], p.sel[OPT_DS]);
        CHECK(options_internal_scale(&p) == 1.0f, "high difficulty");
    }
    printf("test_modes: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
