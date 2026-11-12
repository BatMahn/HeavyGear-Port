/* the briefing's wingmen: the lists from a BRF mask, the default choice, stepping, the voices */
#include "../src/squad.h"

#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    int all[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    squad s;
    squad_default(&s, 0x6, all, 1);                          /* mission 1: Reeves, Janus (as the original shows) */
    CHECK(s.pilot[0] == 1 && s.pilot[1] == 0);
    CHECK(s.voice[0] == 0 && s.voice[1] == 0);
    CHECK(squad_voice_set(s.voice[0]) == 1 && squad_voice_set(s.voice[1]) == 1);
    squad_default(&s, 0x2, all, 1);                          /* mission 2: Janus alone */
    CHECK(s.pilot[0] == 0 && s.pilot[1] == SQUAD_NONE2);
    squad_default(&s, 0x1, all, 1);                          /* alone */
    CHECK(s.pilot[0] == SQUAD_NONE1 && s.pilot[1] == SQUAD_NONE2);
    squad_default(&s, 0x19, all, 1);                         /* may go alone: "No squadmate" chosen */
    CHECK(s.pilot[0] == SQUAD_NONE1 && s.pilot[1] == SQUAD_NONE2);
    int l1[11], n1, l2[11], n2;
    CHECK(squad_lists(0x19, all, l1, &n1, l2, &n2) == 1 && n1 == 3 && l1[1] == 2 && l1[2] == 3);
    int p = squad_step(l1, n1, SQUAD_NONE1, SQUAD_NONE2, 1);
    CHECK(p == 2);
    CHECK(squad_step(l2, n2, SQUAD_NONE2, p, 1) == 3);       /* list 2 skips list 1's pilot */
    int dead[10] = {1, 0, 1, 1, 1, 1, 1, 1, 1, 1};
    squad_default(&s, 0x6, dead, 1);                         /* Reeves dead: both lists hold Janus alone */
    CHECK(s.pilot[0] == 0 && s.pilot[1] == 0);
    squad_default(&s, 0x6, all, 0);                          /* the South's voices: k + 9, k */
    CHECK(s.voice[0] == 10 && squad_voice_set(s.voice[0]) == 9);
    CHECK(squad_pilot_name(1, 1)[0] == 'R' && squad_portrait(1, 1, 1) == 0x129);
    /* the gear lists' tech level (0x10112146 / 0x1011238e) */
    CHECK(squad_tech_level(2, 0, 0, 1) == 0);
    CHECK(squad_tech_level(2, 2500, 0, 1) == 8);
    CHECK(squad_tech_level(0, 2500, 0, 1) == 3);            /* a third outside the story */
    CHECK(squad_tech_level(2, 2500, 500, 1) == 10 && squad_tech_level(2, 2500, -150, 1) == 7);
    CHECK(squad_tech_level(2, 0, 0, 4) == 4 && squad_tech_level(2, 20000, 900, 0) == 17);
    CHECK(squad_stock_gear(0) && squad_stock_gear(24) && !squad_stock_gear(25));
    if (!fails) printf("test_squad: ok\n");
    return fails != 0;
}
