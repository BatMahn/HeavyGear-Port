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

/* ---- a design's speed, load and capacity (HGSHELL 0x100a7346: 0x100a752f, 0x100a7802, 0x100a73a0, 0x10119f6d) ---- */

static int ow(int n) { return opt_weight(n); }
static int oc(int n) { return opt_cap(n); }
static int om(int n) { return opt_of(n)->max_speed; }

/* the option indices an item's options word names, by category (0x1011a201 .., 0x1011a7bf .., 0x1011ab2e ..) */
static int opts_of(int t, unsigned o, int *idx)
{
    switch (t) {
    case 0: idx[0] = (int)(o & 15) + 6; idx[1] = (int)(o >> 4 & 15) + 9; idx[2] = (int)(o >> 8 & 15) + 0xb; return 3;
    case 1: idx[0] = (int)(o >> 16); return 1;
    case 3: idx[0] = (int)(o & 15) + 0xe; idx[1] = (int)(o >> 4 & 15) + 0xe; idx[2] = (int)(o >> 8 & 15) + 0xe;
            idx[3] = (int)(o >> 12 & 15) + 0x16; idx[4] = (int)(o >> 16 & 15) + 0x13; return 5;
    case 4: idx[0] = (int)(o & 15) + 0x18; idx[1] = (int)(o >> 4 & 15) + 0x18; idx[2] = (int)(o >> 8 & 15) + 0x18;
            idx[3] = (int)(o >> 12 & 15) + 0x1d; idx[4] = (int)(o >> 16 & 15) + 0x20; return 5;
    }
    return 0;
}

/* one part's weight (0x1011a4a9 torso, 0x1011a708 engine, 0x1011a9df arms, 0x1011adde legs, 0x1011b2f6 head, 0x1011b531
 * a weapon's +0x1e), capacity (0x1011a50d, 0x1011a730, 0x1011aa53, 0x1011ae52) and top speed (0x1011a449, 0x1011a614,
 * 0x1011ad4e: the record's +0x3e and the options' +0x20) */
static void part_stats(int id, unsigned o, int *w, int *c, int *s)
{
    int t = garage_type(id), idx[5], n = opts_of(t, o, idx);
    *w = *c = *s = 0;
    if (t < 0) return;
    if (t >= 5) { *w = fld(id, F_1E); return; }
    *w = shell_equip_weight[id];
    if (t == 2) {
        static const unsigned bit[11] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x200, 0x400, 0x800};
        for (int b = 0; b < 11; b++) if (o & bit[b]) *w += ow(34 + b);
        return;
    }
    for (int i = 0; i < n; i++) *w += ow(idx[i]);
    if (t == 0 && (o >> 12 & 1)) *w += ow(51);   /* 0x1011a231 */
    *c = fld(id, F_LOADCAP);
    for (int i = 0; i < n; i++) *c += oc(idx[i]);
    if (t == 0 || t == 1 || t == 4) {
        *s = fld(id, F_MAXSPEED);
        for (int i = 0; i < n; i++) *s += om(idx[i]);
    }
}

/* the strider's own parts (0x10119e00's defaults: engine option 0x2d, arms 0xe x3 0x16 0x13, legs 0x18 x3 0x1d 0x20,
 * head bit 2): Mammoth (7) 0x17 / 0x37 / 0x47 / 0x27, Naga (15) 0x1f / 0x3f / 0x4f / 0x2f */
static const unsigned strider_def[4] = {0x2du << 16, 0, 0, 2};   /* engine, arms, legs, head */

void garage_design_stats(const int *ids, const int *armor, const unsigned *opts, int n, int b30, int b31,
                         int *speed, int *load, int *cap)
{
    int w = n > 0 ? opt_of(b31)->load : 0, a = 0, c = 0, s = 0, tarm = 0, tbase = 0, have_t = 0;
    for (int i = 0; i < n; i++) {
        int pw, pc, ps, t = garage_type(ids[i]);
        part_stats(ids[i], opts[i], &pw, &pc, &ps);
        w += pw; c += pc;
        a += armor[i];
        if (t == 0 || t == 1 || t == 4) s += ps;
        if (t == 0) {
            tarm = armor[i]; tbase = fld(ids[i], F_ARMOR); have_t = 1;
            if (ids[i] == 7 || ids[i] == 15) {
                int base = ids[i] == 7 ? 0x10 : 0x18;   /* + 0x17 / 0x37 / 0x47 / 0x27 or 0x1f / 0x3f / 0x4f / 0x2f */
                static const int off[4] = {0x07, 0x27, 0x37, 0x17};   /* engine, arms, legs, head */
                for (int k = 0; k < 4; k++) {
                    int sid = base + off[k];
                    part_stats(sid, strider_def[k], &pw, &pc, &ps);
                    w += pw;
                    if (k < 3) c += pc;
                    if (k == 0 || k == 2) s += ps;
                    a += fld(sid, F_ARMOR);
                }
            }
        }
    }
    w += a * opt_of(b30)->load;   /* 0x100a752f: the armour times option b30's +0 */
    /* 0x10119f6d: load over capacity, then the torso's armour over its record's */
    int pen = 0, one = 0;
    float r = c ? (float)w / (float)c : 0.0f;
    if (r > 1.26f) one = 1;
    else if (r > 1.2) pen = -5;
    else if (r > 1.15) pen = -5;
    else if (r > 1.1) pen = -3;
    else if (r > 1.05) pen = -2;
    else if (r > 1.0f) pen = -1;
    if (have_t && tbase) {
        float q = (float)tarm / (float)tbase;
        if (q == 1.0f) pen -= 3;
        else if (q >= 0.9f) pen -= 2;
        else if (q >= 0.75f) pen -= 1;
    }
    s = one ? 1 : s + pen;
    if (speed) *speed = s;
    if (load) *load = w;
    if (cap) *cap = c;
}

/* ---- the statistics panel's design text (HGSHELL FUN_100a7b0a with nothing selected: FUN_1016c0cc) ----
 * "\n\n" Strength (0x100a881e) "\n" Max Speed (min(0x100a73a0, 0x100a8a2f) x 6) " KPH\n" Sensors (0x100a89b2)
 * "\n" Stealth (0x100a88e4) "\n" Load (0x100a752f) "\n" Max Load (0x100a7802), each "<label>: <n>" (the labels from
 * 0x10172a24, ": " 0x1023d9c4, the line break the char 0x0a: 0x1016bc45). The sums run over the design's items:
 *   strength: torso +0x2e and its three options' +0x14 (0x1011a30d); an engine 0 / 1 / 2 / 3 by its HP under 600 /
 *     750 / 950 / over (0x1011a644); arms +0x2e and their five options' +0x14 (0x1011a94f);
 *   stealth: torso +0x32, its options' +0x18 and its armour step (0x1011a36d / 0x1011a14e); an engine its option's
 *     +0x18 and +1 / 0 / -1 / -2 / -3 by HP under 400 / 600 / 750 / 950 / over (0x1011a68d); the head +0x32 and its
 *     equipment bits' options' +0x18 (0x1011b1a5);
 *   sensors: the head +0x36 and its bits' options' +0x24 (0x1011b054);
 *   the second speed (0x100a8a2f): torso +0x3a and its options' +0x1c (0x1011a3e9; a strider adds its own engine's
 *     and legs' with their default options, 0x10119e00); an engine (int)sqrt(HP) and its option's +0x1c (0x1011a5c9);
 *     legs +0x3a and their options' +0x1c (0x1011acbe); then the load over capacity's step (0x1011a099: over 1.26 -8,
 *     1.2 -6, 1.15 -5, 1.1 -4, 1.05 -3, 1.0 -2); 0 without an engine (or a strider). */
static int hp_class(int hp) { return hp < 600 ? 0 : hp < 0x2ee ? 1 : hp < 0x3b6 ? 2 : 3; }

static int head_bits_sum(int id, unsigned o, int sensors)
{
    static const unsigned bit[11] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x200, 0x400, 0x800};
    int v = fld(id, sensors ? F_HP : F_STEALTH);
    for (int b = 0; b < 11; b++) if (o & bit[b]) v += sensors ? opt_of(34 + b)->sensors : opt_of(34 + b)->stealth;
    return v;
}

int garage_design_text(const int *ids, const int *armor, const unsigned *opts, int n, int b30, int b31, char *out,
                       size_t len)
{
    int strength = 0, stealth = 0, sensors = 0, speed2 = 0, engine = 0, top, load, cap;
    garage_design_stats(ids, armor, opts, n, b30, b31, &top, &load, &cap);
    for (int i = 0; i < n; i++) {
        int id = ids[i], t = garage_type(id), idx[5], k = opts_of(t, opts[i], idx);
        unsigned o = opts[i];
        switch (t) {
        case 0:
            strength += fld(id, F_STRENGTH);
            stealth += fld(id, F_STEALTH) + armor_step(armor[i], fld(id, F_ARMOR));
            speed2 += fld(id, F_SPEED);
            for (int j = 0; j < k; j++) {
                strength += opt_of(idx[j])->strength; stealth += opt_of(idx[j])->stealth; speed2 += opt_of(idx[j])->speed;
            }
            if (id == 7 || id == 15) {
                /* the strider's engine (0x17 / 0x1f: option 0x2d, its HP) and legs (0x47 / 0x4f: 0x18 x3, 0x1d, 0x20) */
                int e = id == 7 ? 0x17 : 0x1f, l = id == 7 ? 0x47 : 0x4f;
                speed2 += (int)sqrt((double)(fld(e, F_HP) & 0xffff)) + opt_of(0x2d)->speed;
                static const int lo[5] = {0x18, 0x18, 0x18, 0x1d, 0x20};
                speed2 += fld(l, F_SPEED);
                for (int j = 0; j < 5; j++) speed2 += opt_of(lo[j])->speed;
                engine = 1;
            }
            break;
        case 1: {
            int hp = (int)(o & 0xffff), c = hp_class(hp);
            strength += c;
            stealth += opt_of(idx[0])->stealth + (hp < 400 ? 1 : hp < 600 ? 0 : -c);
            speed2 += (int)sqrt((double)hp) + opt_of(idx[0])->speed;
            engine = 1;
            break;
        }
        case 2:
            stealth += head_bits_sum(id, o, 0);
            sensors += head_bits_sum(id, o, 1);
            break;
        case 3:
            strength += fld(id, F_STRENGTH);
            for (int j = 0; j < k; j++) strength += opt_of(idx[j])->strength;
            break;
        case 4:
            speed2 += fld(id, F_SPEED);
            for (int j = 0; j < k; j++) speed2 += opt_of(idx[j])->speed;
            break;
        }
    }
    if (n > 0) {
        float r = cap ? (float)load / (float)cap : 0.0f;
        speed2 += r > 1.26f ? -8 : r > 1.2 ? -6 : r > 1.15 ? -5 : r > 1.1 ? -4 : r > 1.05 ? -3 : r > 1.0f ? -2 : 0;
    }
    if (!engine) speed2 = 0;
    int ms = top < speed2 ? top : speed2;
    return snprintf(out, len, "\n\nStrength: %d\nMax Speed: %d KPH\nSensors: %d\nStealth: %d\nLoad: %d\nMax Load: %d",
                    strength, ms * 6, sensors, stealth, load, cap);
}
