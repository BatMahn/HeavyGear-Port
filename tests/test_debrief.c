/* the debriefing's score (src/debrief.h, HGSHELL 0x100df8e4 / 0x100e35cc / 0x100b9fad) */
#include "../src/debrief.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    career_char c;
    memset(&c, 0, sizeof c);
    c.type = 2;
    c.stat[10] = 1; c.stat[11] = 1; c.stat[12] = 1;
    debrief_in in = {4, {{"Destroy all enemies", 1, 1}, {"Destroy the base", 1, 1}, {"Defend your squad", 0, 2},
                         {"Hidden helper", 1, 0}}, 3, 1, 1};
    debrief_list l;
    /* the first win: bonus 100, the two done primaries 100 each, 3 kills over a best of 0: 150 */
    int xp = debrief_score(&c, 0, 2, &in, &l);
    CHECK(xp == 100 + 200 + 150 && c.stat[0] == xp && c.stat[6] == 3);
    CHECK(l.n == 3 && l.item[0].cat == 2 && l.item[0].value == 100 && l.item[2].cat == 1 && l.item[2].value == 0);
    CHECK(l.bonus == 100 && l.kill_bonus == 150 && l.death_penalty == -50 && l.total == 400 && l.career == xp);
    CHECK(c.stat[3] == 1 && c.stat[4] == 1 && c.stat[0x19] == 3);
    /* replayed (the reached mission is further on): no bonus, the objectives pay nothing, kills only above the best */
    c.stat[10] = 2;
    in.kills = 5; in.deaths = 0;
    xp = debrief_score(&c, 0, 2, &in, &l);
    CHECK(xp == 100 && l.bonus == 0 && l.item[0].value == 0 && l.kill_bonus == 100 && l.total == 100);
    /* not beaten: nothing; the list's kill bonus shows 0 */
    in.kills = 2;
    xp = debrief_score(&c, 0, 2, &in, &l);
    CHECK(xp == 0 && l.kill_bonus == 0);
    /* a failure: no experience; complete items still show 100 */
    int before = c.stat[0];
    xp = debrief_score(&c, 0, 1, &in, &l);
    CHECK(xp == 0 && c.stat[0] == before && l.item[0].value == 100 && l.kill_bonus == 100 && c.stat[5] == 0);
    /* the tour (0x100df8e4's tour case): type 6 won with 3 kills, 1 death: 200 + 100 bonus, 150 kills, -50, the two
     * done primaries 200; medals: a North game counts medal 2 three times and medal 0 for type 6 */
    {
        career_char t;
        memset(&t, 0, sizeof t);
        t.type = 0; t.stat[11] = 6;
        debrief_in ti;
        memset(&ti, 0, sizeof ti);
        ti.n = 2; strcpy(ti.o[0].text, "A"); ti.o[0].done = 1; ti.o[0].order = 1;
        strcpy(ti.o[1].text, "B"); ti.o[1].done = 1; ti.o[1].order = 2;
        ti.kills = 3; ti.deaths = 1; ti.valid = 1;
        int x = debrief_score(&t, 1, 2, &ti, &l);
        CHECK(x == 300 + 150 - 50 + 200 && t.stat[0] == x && l.bonus == 300 && t.stat[3] == 1 && t.stat[5] == 1);
        CHECK(t.aac[2] == 3 && t.aac[0] == 1);
        t.stat[11] = 0;
        debrief_score(&t, 1, 2, &ti, &l);
        CHECK(t.stat[0x15] == 1 && t.stat[0x16] == 3 && t.aac[2] == 1 && t.aac[3] == 1);
        debrief_score(&t, 1, 1, &ti, &l);
        CHECK(t.stat[0x15] == 0 && t.stat[0x16] == 0 && t.stat[5] == 0 && t.stat[3] == 3);
    }
    if (!fails) printf("test_debrief: ok\n");
    return fails != 0;
}
