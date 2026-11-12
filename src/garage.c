#include "garage.h"

#include "shell_picture.h"
#include "shell_tables.h"

#include <math.h>
#include <stdio.h>

const char *garage_name(int id)
{
    return id >= 0 && id < SHELL_EQUIP_IDS ? shell_equip_name[id] : "";
}

int garage_type(int id)
{
    return id >= 0 && id < SHELL_EQUIP_IDS ? shell_equip_type[id] : -1;
}

int garage_tech(int id)
{
    return id >= 0 && id < SHELL_EQUIP_IDS ? shell_equip_tech[id] : 0;
}

int garage_model(int torso)
{
    return torso == 7 ? GARAGE_MAMMOTH : torso == 15 ? GARAGE_NAGA : GARAGE_GEAR;
}

/* FUN_100f267f's test */
static int offered(int id, int level)
{
    return id >= 0 && id < SHELL_EQUIP_IDS && shell_equip_offered[id] && shell_equip_tech[id] <= level;
}

int garage_list(int cat, int north, int model, int level, int *ids, int max)
{
    int n = 0;
    if (cat < 0 || cat >= GARAGE_CATS) return 0;
    if (cat < 5) {
        const short *p = shell_garage_parts[north ? 0 : 1];
        for (int i = 0; i < SHELL_GARAGE_PARTS && n < max; i++)
            if (shell_equip_type[p[i]] == cat && offered(p[i], level)) ids[n++] = p[i];
    } else if (model >= 1 && model <= 3) {
        const short *p = shell_garage_arms[model - 1];
        for (int i = 0; p[i] >= 0 && n < max; i++)
            if (shell_equip_type[p[i]] == cat && offered(p[i], level)) ids[n++] = p[i];
    }
    return n;
}

static int rd32(const unsigned char *p) { return (int)((unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24); }

int garage_design(const unsigned char *d, size_t len, int *ids, int max)
{
    /* the GDF: i32 0, ..., +0x84 the parts' count (0x438 each), then the gear record (+0x24 equipment count) and the
     * equipment records (0x6c each, the id at +4) */
    if (len < 0x88 || rd32(d) != 0) return -1;
    size_t np = (size_t)(unsigned)rd32(d + 0x84), o = 0x88 + np * 0x438;
    if (o + 0x3c > len) return -1;
    int neq = rd32(d + o + 0x24), n = 0;
    if (neq < 0) return -1;
    for (int k = 0; k < neq && n < max; k++) {
        size_t r = o + 0x3c + (size_t)k * 0x6c;
        if (r + 8 > len) break;
        ids[n++] = rd32(d + r + 4);
    }
    return n;
}

int garage_torso(const int *ids, int n)
{
    for (int i = 0; i < n; i++) if (garage_type(ids[i]) == 0) return ids[i];
    return -1;
}

int garage_complete(const int *ids, int n)
{
    int have[5] = {0};
    for (int i = 0; i < n; i++) {
        int t = garage_type(ids[i]);
        if (t == 0 && (ids[i] == 7 || ids[i] == 15)) return 1;
        if (t >= 0 && t < 5) have[t] = 1;
    }
    return have[0] && have[1] && have[2] && have[3] && have[4];
}

/* ---- the list items' statistics (HGSHELL 0x1011a1b0 ... 0x1011b531) ---- */

/* the record's i32 at 0x101b001e + 4k (shell_equip_field) */
enum { F_1E, F_LOADCAP, F_ARMOR, F_STRUCT, F_STRENGTH, F_STEALTH, F_HP, F_SPEED, F_MAXSPEED };

static int fld(int id, int k) { return shell_equip_field[id][k]; }
static const shell_option_stat *opt_of(int n) { static const shell_option_stat z; return n >= 0 && n < 52 ? &shell_option[n] : &z; }
static int opt_weight(int n) { return n >= 0 && n < 52 ? shell_option_load[n][0] : 0; }
static int opt_cap(int n) { return n >= 0 && n < 52 ? shell_option_load[n][1] : 0; }

/* the struct sum (0x1011a245 / 0x1011a81b / 0x1011ab8a): the record's +0x2a plus each option's add, then times each
 * option's factor in turn (single precision kept between), truncated */
static int struct_sum(int id, const int *o, int n)
{
    int s = fld(id, F_STRUCT);
    for (int i = 0; i < n; i++) s += opt_of(o[i])->struct_add;
    float f = (float)s;
    for (int i = 0; i < n - 1; i++) f = (float)(f * opt_of(o[i])->struct_mul);
    return (int)((double)f * opt_of(o[n - 1])->struct_mul);
}

/* 0x1011a14e: a part's armour against the record's (the list passes the same: -3) */
static int armor_step(int a, int b)
{
    if (a == b) return -3;
    float r = (float)a / (float)b;
    return r < 0.75 ? (r < 0.5 ? 0 : -1) : -2;
}

int garage_item_text(int id, char *out, size_t n)
{
    if (id < 0 || id >= SHELL_EQUIP_IDS || !n) return 0;
    const char *name = shell_equip_name[id];
    int t = shell_equip_type[id], armor = fld(id, F_ARMOR), o[5], k = 0, sum;
    switch (t) {
    case 0: {
        /* the torso (default options 0x1011a1b0(6, 10, 0xb, 0): 6, 10, 11), 0x1016c7e7 */
        o[0] = 6; o[1] = 10; o[2] = 11; k = 3;
        int strength = fld(id, F_STRENGTH), speed = fld(id, F_SPEED), max = fld(id, F_MAXSPEED), stealth = fld(id, F_STEALTH);
        int load = shell_equip_weight[id], cap = fld(id, F_LOADCAP);
        for (int i = 0; i < k; i++) {
            strength += opt_of(o[i])->strength; speed += opt_of(o[i])->speed; max += opt_of(o[i])->max_speed;
            stealth += opt_of(o[i])->stealth; load += opt_weight(o[i]); cap += opt_cap(o[i]);
        }
        stealth += armor_step(armor, armor);
        load += armor + opt_of(3)->load;   /* + 0x101c3824 + 0x10119ee3() (3) x 0x98 */
        return snprintf(out, n, "%s\nStrength: %d\nMax Speed: %d KPH\nSpeed: %d KPH\nArmor: %d\nStruct: %d\nStealth: %d\n"
                        "Load: %d\nMax Load: %d", name, strength, max * 6, speed * 6, armor, struct_sum(id, o, k), stealth, load, cap);
    }
    case 1: {
        /* the engine (0x1011a55e(0x2d, +0x36): option 0x2d, the HP in the low word), 0x1016ffc9 */
        int hp = fld(id, F_HP) & 0xffff;
        int speed = (int)sqrt((double)hp) + opt_of(0x2d)->speed;
        int st = hp > 0x3b6 ? (hp - 0x3b6) / 0x32 * 10 + 0x3b6 : hp;
        int stealth = opt_of(0x2d)->stealth + (hp < 400 ? 1 : hp < 600 ? 0 : hp < 0x2ee ? -1 : hp < 0x3b6 ? -2 : -3);
        int load = shell_equip_weight[id] + opt_weight(0x2d) + armor;
        return snprintf(out, n, "%s\nSpeed: %s%d KPH\nArmor: %d\nStruct: %d\nStealth: %d\nHP: %d\nLoad: %d", name,
                        speed > 0 ? "+" : "", speed * 6, armor, st, stealth, fld(id, F_HP), load);
    }
    case 2: {
        /* the head (0x1011aec9(0, 1, 0 ...): bit 1, option 35), 0x1016da41: bits 0..7, 9..11 options 34.. */
        static const int bit[11] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x200, 0x400, 0x800};
        int bits = 2, sensors = fld(id, F_HP), stealth = fld(id, F_STEALTH), load = shell_equip_weight[id];
        for (int b = 0; b < 11; b++)
            if (bits & bit[b]) { sensors += opt_of(34 + b)->sensors; stealth += opt_of(34 + b)->stealth; load += opt_weight(34 + b); }
        return snprintf(out, n, "%s\nArmor: %d\nStruct: %d\nSensors: %d\nStealth: %d\nLoad: %d", name, armor,
                        fld(id, F_STRUCT), sensors, stealth, load + armor);
    }
    case 3: case 4: {
        /* arms (0x1011a75b(0xe, 0xe, 0xe, 0x16, 0x13)) 0x1016e571, legs (0x1011aaca(0x18, 0x18, 0x18, 0x1d, 0x20))
         * 0x1016f0a1: the option fields' bases with nothing set */
        if (t == 3) { o[0] = o[1] = o[2] = 0xe; o[3] = 0x16; o[4] = 0x13; }
        else { o[0] = o[1] = o[2] = 0x18; o[3] = 0x1d; o[4] = 0x20; }
        k = 5;
        int strength = fld(id, F_STRENGTH), speed = fld(id, F_SPEED), load = shell_equip_weight[id], cap = fld(id, F_LOADCAP);
        for (int i = 0; i < k; i++) {
            strength += opt_of(o[i])->strength; speed += opt_of(o[i])->speed; load += opt_weight(o[i]); cap += opt_cap(o[i]);
        }
        sum = struct_sum(id, o, k);
        if (t == 3)
            return snprintf(out, n, "%s\nStrength: %d\nArmor: %d\nStruct: %d\nLoad: %d\nMax Load: %d", name, strength,
                            armor, sum, load + armor, cap);
        return snprintf(out, n, "%s\nSpeed: %s%d KPH\nArmor: %d\nStruct: %d\nStealth: %d\nLoad: %d\nMax Load: %d", name,
                        speed > 0 ? "+" : "", speed * 6, armor, sum, fld(id, F_STEALTH), load + armor, cap);
    }
    default: {
        /* weapons, 0x10170d53: the salvo by the ammunition type (+0x3e, 0x1011b472), the load +0x1e (0x1011b531),
         * the clip's name (0x102588a8 + n x 0x10 by +0x36) */
        static const char *const clip[6] = {"Standard", "AT", "HE", "APDS", "Energy", "Particle"};
        int a = fld(id, F_MAXSPEED), salvo = a >= 0xf && a <= 0x12 ? 4 : a >= 0x13 && a <= 0x15 ? 3 : a == 0x16 || a == 0x17 ? 6
                : a == 0x19 ? 2 : a == 0x1a ? 3 : 1, c = fld(id, F_HP);
        return snprintf(out, n, "%s\nRange: %d\nPower: %d\nAmmo: %d\nSalvo: %d\nLoad: %d\nClip: %s", name,
                        fld(id, F_STEALTH), fld(id, F_STRENGTH), fld(id, F_SPEED), salvo, fld(id, F_1E) + armor,
                        c >= 0 && c < 6 ? clip[c] : "");
    }
    }
}
