#include "squad.h"

#include "shell_picture.h"
#include "shell_tables.h"

#include <stdio.h>
#include <string.h>

static const char *const north_names[10] = {"Janus", "Reeves", "Warlock", "Lucky", "Statistic", "Eagle", "Lynx",
                                            "Spike", "Nihilist", "Tagger"};
static const char *const south_names[10] = {"Sabre", "Viper", "Stinger", "Hector", "Templar", "Mouse", "Pyro",
                                            "Gator", "Devil", "Raven"};

const char *squad_pilot_name(int north, int k)
{
    if (k < 0 || k > 9) return "No squadmate";
    return north ? north_names[k] : south_names[k];
}

int squad_portrait(int north, int list, int k)
{
    if (k < 0 || k > 9) return -1;
    return (north ? (list ? 0x128 : 0x11d) : (list ? 0x13e : 0x133)) + k;
}

int squad_lists(int mask, const int alive[10], int l1[11], int *n1, int l2[11], int *n2)
{
    unsigned m = (unsigned)mask;
    *n1 = *n2 = 0;
    if (m == 0) {
        l1[(*n1)++] = SQUAD_NONE1; l2[(*n2)++] = SQUAD_NONE2;
        return 0;
    }
    int single = 0;
    if (m & 1) {
        l1[(*n1)++] = SQUAD_NONE1; l2[(*n2)++] = SQUAD_NONE2;
    } else if ((m & (m - 1)) == 0) {
        int k = 0;
        for (unsigned b = m; b != 2; b >>= 1) k++;
        l1[(*n1)++] = k; l2[(*n2)++] = SQUAD_NONE2;
        single = 1;
    }
    if (!single)
        for (int k = 0; k < 10; k++)
            if (m & (1u << (k + 1)) && alive[k]) { l1[(*n1)++] = k; l2[(*n2)++] = k; }
    if (*n1 == 0) l1[(*n1)++] = SQUAD_NONE1;
    if (*n2 == 0) l2[(*n2)++] = SQUAD_NONE2;
    return (m & 1) != 0;
}

void squad_voices(squad *s, int north)
{
    /* 0x100ff1e6 */
    s->voice[0] = s->pilot[0] >= 0 ? (north ? s->pilot[0] - 1 : s->pilot[0] + 9) : 0;
    s->voice[1] = s->pilot[1] >= 0 ? s->pilot[1] : 0;
}

int squad_step(const int *l, int n, int cur, int other, int dir)
{
    /* the list's next / previous item the linked list hasn't taken (0x10076ff7 / 0x10076f1a, round) */
    int at = 0;
    for (int i = 0; i < n; i++) if (l[i] == cur) at = i;
    for (int k = 1; k <= n; k++) {
        int v = l[((at + dir * k) % n + n) % n];
        if (v < 0 || v != other) return v;
    }
    return cur;
}

/* 0x10076f1a on a list: from its current item on, cyclically, the first whose pilot isn't the other list's current */
static void step_list(const int *l, int n, int *cur, int other)
{
    if (n <= 1) return;
    int start = *cur;
    do {
        *cur = (*cur + 1) % n;
        if (l[*cur] != other) return;
    } while (*cur != start);
}

void squad_default(squad *s, int mask, const int alive[10], int north)
{
    memset(s, 0, sizeof *s);
    int l1[11], n1, l2[11], n2;
    if (squad_lists(mask, alive, l1, &n1, l2, &n2)) {
        s->pilot[0] = SQUAD_NONE1; s->pilot[1] = SQUAD_NONE2;
    } else {
        /* 0x100ef574: each list's items are added after its current one, which they become (0x10026927), so both
         * end at their last; then 0x10076f1a runs twice on the second list (0x100ef972 / 0x100ef97d: ecx = +0x14) */
        int c1 = n1 - 1, c2 = n2 - 1;
        step_list(l2, n2, &c2, l1[c1]);
        step_list(l2, n2, &c2, l1[c1]);
        s->pilot[0] = l1[c1];
        s->pilot[1] = l2[c2];
    }
    squad_voices(s, north);
}

int squad_voice_set(int voice)
{
    int v = voice & 0xffff;
    if (v == 0xffff) return 0;
    return v < 9 ? v + 1 : v - 1;
}

int squad_tech_level(int type, int stat0, int prestige, int chapter)
{
    /* 0x10112146: the experience (stat 0; a third of it outside the story) against a ladder */
    static const int ladder[26] = {200, 500, 800, 1100, 1400, 1700, 2000, 2300, 2700, 3100, 3500, 3900, 4400, 4900,
                                   5400, 5900, 6400, 7000, 7600, 8200, 8800, 9500, 10500, 11000, 11500, 12000};
    int xp = type == 2 ? stat0 : stat0 / 3, l = 0;
    while (l < 26 && xp >= ladder[l]) l++;
    /* 0x1011238e (mode 0): the side's prestige (the record's +0x8c [2] North / [6] South) moves it */
    if (prestige <= -0xc9) l = l < 2 ? 0 : l - 2;
    else if (prestige <= -0x65) l = l < 1 ? 0 : l - 1;
    else if (prestige > 100) {
        if (prestige > 800) l = l < 0x18 ? l + 3 : 0x1a;
        else if (prestige > 400) l = l < 0x19 ? l + 2 : 0x1a;
        else if (l < 0x1a) l++;
    }
    /* the chapter (the record's +0x40) caps or floors it */
    switch (chapter) {
    case 0: case 10: case 0xb: if (l > 0x11) l = 0x11; break;
    case 4: if (l < 4) l = 4; break;
    case 7: case 9: case 0x12: if (l < 0xd) l = 0xd; break;
    case 0xd: if (l < 2) l = 2; break;
    case 0xe: if (l < 8) l = 8; break;
    case 0x11: if (l < 0x11) l = 0x11; break;
    }
    return l;
}

static int rd32(const unsigned char *p) { return (int)((unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24); }

int squad_gear_info(const unsigned char *d, size_t len, int *side, char *name, size_t nlen)
{
    if (len < 0x88 || rd32(d) != 0) return -1;
    size_t np = (size_t)(unsigned)rd32(d + 0x84), o = 0x88 + np * 0x438;
    if (o + 0x3c > len) return -1;
    int ver = rd32(d + o);
    if (ver != 1 && ver != 2) return -1;
    if (name) snprintf(name, nlen, "%.31s", (const char *)d + o + 4);
    *side = rd32(d + o + 0x2c);                       /* +0xb8 */
    int neq = rd32(d + o + 0x24);
    size_t p = o + 0x3c;
    if (neq < 0 || p + (size_t)neq * 0x6c > len) return -1;
    if (p + (size_t)neq * 0x6c >= len) return -1;   /* +0xd0: the data after the records (every stock gear has it) */
    /* 0x1005bb6d: the highest tech of its equipment and their options */
    int t = 0;
    for (int k = 0; k < neq; k++) {
        const unsigned char *r = d + p + (size_t)k * 0x6c;
        int id = rd32(r + 4), op = rd32(r + 0xc), idx[5], n = 0;
        if (id < 0 || id >= SHELL_EQUIP_IDS) continue;
        if (shell_equip_tech[id] > t) t = shell_equip_tech[id];
        switch (shell_equip_type[id]) {
        case 0:
            idx[n++] = (op & 15) + 6; idx[n++] = (op >> 4 & 15) + 9; idx[n++] = (op >> 8 & 15) + 0xb;
            if ((op >> 12 & 1) && shell_option_tech[51] > t) t = shell_option_tech[51];
            break;
        case 1: idx[n++] = op >> 16; break;
        case 3:
            idx[n++] = (op & 15) + 0xe; idx[n++] = (op >> 4 & 15) + 0xe; idx[n++] = (op >> 8 & 15) + 0xe;
            idx[n++] = (op >> 12 & 15) + 0x16; idx[n++] = (op >> 16 & 15) + 0x13;
            break;
        case 4:
            idx[n++] = (op & 15) + 0x18; idx[n++] = (op >> 4 & 15) + 0x18; idx[n++] = (op >> 8 & 15) + 0x18;
            idx[n++] = (op >> 12 & 15) + 0x1d; idx[n++] = (op >> 16 & 15) + 0x20;
            break;
        case 2: {
            static const int bit[11] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x200, 0x400, 0x800};
            for (int b = 0; b < 11; b++) if ((op & bit[b]) && shell_option_tech[34 + b] > t) t = shell_option_tech[34 + b];
            break;
        }
        default:
            if (shell_equip_type[id] >= 5 && shell_equip_type[id] <= 10) idx[n++] = op;
        }
        for (int q = 0; q < n; q++) if (idx[q] >= 0 && idx[q] < 52 && shell_option_tech[idx[q]] > t) t = shell_option_tech[idx[q]];
    }
    return t;
}

const char *squad_stock_gear(int i)
{
    return i >= 0 && i < SHELL_STOCK_GEARS ? shell_stock_gear[i] : NULL;
}
