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
    if (!fails) printf("test_garage: ok\n");
    return fails != 0;
}
