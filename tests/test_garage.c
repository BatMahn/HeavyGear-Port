/* the gear editor's catalogue (src/garage.h) */
#include "../src/garage.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    int ids[128], n;
    /* the table: id 0 the Cheetah's torso, 0x50 the light autocannon (a rifle), 0x82 not offered */
    CHECK(!strcmp(garage_name(0), "Cheetah") && garage_type(0) == 0);
    CHECK(!strcmp(garage_name(0x50), "Light Autocannon") && garage_type(0x50) == 5);
    CHECK(garage_type(0x82) == 10);
    /* North torsos at level 0: Cheetah, Hunter; at 26 every North gear and the Mammoth */
    n = garage_list(0, 1, GARAGE_GEAR, 0, ids, 128);
    CHECK(n == 2 && ids[0] == 0 && ids[1] == 2);
    n = garage_list(0, 1, GARAGE_GEAR, 26, ids, 128);
    CHECK(n == 8 && ids[7] == 7);
    /* South engines in the list's order (Iguana's first) */
    n = garage_list(1, 0, GARAGE_GEAR, 26, ids, 128);
    CHECK(n == 7 && ids[0] == 0x18 && ids[6] == 0x1e);
    /* the gear's energy weapons: the heavy laser (0x82) isn't offered */
    n = garage_list(10, 1, GARAGE_GEAR, 26, ids, 128);
    CHECK(n == 4 && ids[0] == 0x80 && ids[1] == 0x83 && ids[2] == 0x81 && ids[3] == 0x84);
    /* the first weapons list's rifles at level 0: light autocannon, the machine guns */
    n = garage_list(5, 1, GARAGE_GEAR, 0, ids, 128);
    CHECK(n == 3 && ids[0] == 0x89 && ids[1] == 0x8a && ids[2] == 0x50);
    /* Naga: nose and shoulder weapons */
    n = garage_list(5, 0, GARAGE_NAGA, 26, ids, 128);
    CHECK(n > 0 && !strncmp(garage_name(ids[0]), "Nose", 4));
    CHECK(garage_model(7) == GARAGE_MAMMOTH && garage_model(15) == GARAGE_NAGA && garage_model(2) == GARAGE_GEAR);
    /* complete designs */
    int cheetah[5] = {0, 0x10, 0x20, 0x30, 0x40}, naga[1] = {15};
    CHECK(garage_complete(cheetah, 5) && !garage_complete(cheetah, 4) && garage_complete(naga, 1));
    CHECK(garage_torso(cheetah, 5) == 0);
    /* the list's texts (0x100f267f + 0x10170d53 / 0x1016c7e7) */
    char t[256];
    garage_item_text(0x50, t, sizeof t);
    CHECK(!strcmp(t, "Light Autocannon\nRange: 800\nPower: 85\nAmmo: 240\nSalvo: 1\nLoad: 380\nClip: Standard"));
    garage_item_text(0, t, sizeof t);
    CHECK(!strncmp(t, "Cheetah\nStrength: 0\nMax Speed: 90 KPH\nSpeed: -60 KPH\nArmor: 60\nStruct: 150\n", 74));
    /* a design's top speed (0x100a7346): the torso's armour at its record's costs 3 (0x10119f6d), under 0.75 of it
     * nothing; the Cheetah's own parts (torso +0x3e 15, engine, legs) */
    {
        int a1[5] = {60, 0, 0, 0, 0}, a2[5] = {30, 0, 0, 0, 0}, s1, s2, l1, c1;
        unsigned o[5] = {0};
        garage_design_stats(cheetah, a1, o, 5, 0, 0, &s1, &l1, &c1);
        garage_design_stats(cheetah, a2, o, 5, 0, 0, &s2, NULL, NULL);
        CHECK(s2 - s1 == 3 && c1 > 0 && l1 > 0);
        /* over 1.26 of the capacity the speed is 1 */
        int heavy[6] = {0, 0x10, 0x20, 0x30, 0x40, 7}, ah[6] = {60, 0, 0, 0, 0, 0};
        unsigned oh[6] = {0};
        garage_design_stats(heavy, ah, oh, 6, 0, 0, &s1, &l1, &c1);
        CHECK(l1 <= c1 * 1.26f || s1 == 1);
    }
    /* the statistics panel for the stock Hunter (GDHUNTER's items), as the original's editor shows it (refs
     * b07_hunter: Strength 2, Max Speed 42 KPH, Sensors 1, Stealth -3, Load 6627, Max Load 6627) */
    {
        static const int hid[9] = {2, 18, 34, 50, 121, 80, 66, 142, 139}, har[9] = {75, 40, 9, 45, 50, 180, 40, 100, 20};
        static const unsigned hop[9] = {0x10u, 0x2d01c2u, 0x2u, 0x0u, 0x2fu, 0x2fu, 0x0u, 0x2fu, 0x2fu};
        garage_design_text(hid, har, hop, 9, 0, 3, t, sizeof t);
        CHECK(!strcmp(t, "\n\nStrength: 2\nMax Speed: 42 KPH\nSensors: 1\nStealth: -3\nLoad: 6627\nMax Load: 6627"));
    }
    if (!fails) printf("test_garage: ok\n");
    return fails != 0;
}
