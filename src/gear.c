#include "gear.h"

#include "bytes.h"
#include "gear_tables.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

static int v1_zone(int kind)
{
    if (kind == 1 || kind == 2) return 2;
    if (kind == 3) return 1;
    if (kind >= 5 && kind <= 0xc) return 4;
    if (kind >= 0xd && kind <= 0x14) return 3;
    if (kind >= 0x16 && kind <= 0x1a) return 6;
    if (kind >= 0x1b && kind <= 0x1f) return 5;
    switch (kind) {
    case 0x20: return 7;  case 0x21: return 14; case 0x22: return 16; case 0x23: return 13; case 0x24: return 15;
    case 0x26: return 11; case 0x27: return 9;  case 0x29: return 12; case 0x2a: return 10; case 0x2c: return 8;
    }
    return 0;
}

static float v2_internal(int kind, int id, int opts)
{
    static const int f2[3][2] = {{0, 6}, {4, 9}, {8, 0xb}};
    static const int f3[5][2] = {{0, 0xe}, {4, 0xe}, {8, 0xe}, {12, 0x16}, {16, 0x13}};
    static const int f5[5][2] = {{0, 0x18}, {4, 0x18}, {8, 0x18}, {12, 0x1d}, {16, 0x20}};
    int base = id >= 0 && id < GEAR_EQUIP_IDS ? gear_equip_internal[id] : 0;
    if (kind == 7) {
        int v = opts & 0xffff;
        return (float)(v > 0x3b6 ? (v - 0x3b6) / 0x32 * 10 + 0x3b6 : v);
    }
    const int (*f)[2] = NULL;
    int n = 0;
    if (kind == 2) { f = f2; n = 3; }
    else if (kind == 3 || kind == 4) { f = f3; n = 5; }
    else if (kind == 5 || kind == 6) { f = f5; n = 5; }
    if (!f) return (float)base;
    int sum = base;
    for (int k = 0; k < n; k++) sum += gear_option[((opts >> f[k][0]) & 15) + f[k][1]].add;
    float v = (float)sum;
    for (int k = 0; k < n; k++) v *= gear_option[((opts >> f[k][0]) & 15) + f[k][1]].mul;
    return (float)(int)v;
}

/* the builder's sums over a version 2 gear's records (0x1004e6a0; RE_NOTES "Gear design") */
static int opt(int n, int field)
{
    if (n < 0 || n >= GEAR_OPTIONS) return 0;
    switch (field) {
    case 0: return gear_option_sum[n].hand;
    case 1: return gear_option_sum[n].stealth;
    case 2: return gear_option_sum[n].speed;
    case 3: return gear_option_sum[n].scap;
    case 4: return gear_option_sum[n].sensor;
    case 5: return gear_option_sum[n].weight;
    default: return gear_option_sum[n].cap;
    }
}

static int wtab(int n) { return n >= 0 && n < GEAR_OPTIONS ? gear_weight_tab[n] : 0; }

/* the option indices of a record kind's 4-bit fields (torso 0x1003d3b9.., arms 0x1003d977.., legs 0x1003dce6..) */
static int fields(int kind, int opts, int idx[5])
{
    static const int t[3][2] = {{0, 6}, {4, 9}, {8, 0xb}};
    static const int a[5][2] = {{0, 0xe}, {4, 0xe}, {8, 0xe}, {12, 0x16}, {16, 0x13}};
    static const int l[5][2] = {{0, 0x18}, {4, 0x18}, {8, 0x18}, {12, 0x1d}, {16, 0x20}};
    const int (*f)[2] = kind == 2 ? t : kind == 3 || kind == 4 ? a : l;
    int n = kind == 2 ? 3 : 5;
    for (int k = 0; k < n; k++) idx[k] = ((opts >> f[k][0]) & 15) + f[k][1];
    return n;
}

static void design(gear *g, const uint8_t *rec, int n)
{
    int speed = 0, cap = 0, stealth = 0, sensor = 0, hand = 0, weight = 0, capacity = 0, armor_sum = 0;
    int torso_armor = 0, torso_full = 1, akind = 0, aqual = 0;
    static const int headbit[11] = {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11};
    for (int k = 0; k < n; k++) {
        const uint8_t *r = rec + (size_t)k * 0x6c;
        int kind = rd_i32(r), id = rd_i32(r + 4), arm = rd_i32(r + 8), o = rd_i32(r + 0xc);
        if (kind != 4 && kind != 6) armor_sum += arm;
        int ok = id >= 0 && id < GEAR_EQUIP_IDS, idx[5], m;
        if (!ok) continue;
        const int e_hand = gear_equip_sum[id].hand, e_st = gear_equip_sum[id].stealth, e_se = gear_equip_sum[id].sensor,
                  e_sp = gear_equip_sum[id].speed, e_sc = gear_equip_sum[id].scap, e_w = gear_equip_sum[id].weight,
                  e_c = gear_equip_sum[id].cap;
        switch (kind) {
        case 1:   /* the head: its flag options (0x1003e20c, 0x1003e35d, 0x1003e4ae) */
            sensor += e_se; stealth += e_st; weight += e_w;
            for (int b = 0; b < 11; b++)
                if (o >> headbit[b] & 1) { sensor += opt(34 + b, 4); stealth += opt(34 + b, 1); weight += wtab(34 + b); }
            g->head_flags = o & 0xfff;
            break;
        case 2:   /* the torso (0x1003d4c5, 0x1003d525, 0x1003d5a1, 0x1003d601, 0x1003d661, 0x1003d6c5) */
            m = fields(2, o, idx);
            hand += e_hand; stealth += e_st; speed += e_sp; cap += e_sc; weight += e_w; capacity += e_c;
            for (int q = 0; q < m; q++) {
                hand += opt(idx[q], 0); stealth += opt(idx[q], 1); speed += opt(idx[q], 2); cap += opt(idx[q], 3);
                weight += wtab(idx[q]); capacity += opt(idx[q], 6);
            }
            if (o >> 12 & 1) weight += 500;   /* 0x10103530 */
            /* 0x1003d306: the torso's armor against its full armor */
            {
                int full = gear_equip_sum[id].armor;
                float rr = full ? (float)arm / (float)full : 0;
                stealth += arm == full ? -3 : rr < 0.75f ? (rr < 0.5f ? 0 : -1) : -2;
                torso_armor = arm; torso_full = full ? full : 1;
            }
            akind = rd_i32(r + 0x64); aqual = rd_i32(r + 0x68);
            g->power_spare = idx[1] == 9;   /* 0x1003d3c7 */
            break;
        case 3:   /* the arms (0x1003db07, 0x1003db97, 0x1003dc0b) */
            m = fields(3, o, idx);
            hand += e_hand; weight += e_w; capacity += e_c;
            for (int q = 0; q < m; q++) { hand += opt(idx[q], 0); weight += wtab(idx[q]); capacity += opt(idx[q], 6); }
            break;
        case 5:   /* the legs (0x1003de76, 0x1003df06, 0x1003df96, 0x1003e00a) */
            m = fields(5, o, idx);
            speed += e_sp; cap += e_sc; weight += e_w; capacity += e_c;
            for (int q = 0; q < m; q++) {
                speed += opt(idx[q], 2); cap += opt(idx[q], 3); weight += wtab(idx[q]); capacity += opt(idx[q], 6);
            }
            break;
        case 7: { /* the engine (0x1003d781, 0x1003d7fc, 0x1003d845, 0x1003d7cc, 0x1003d8c0, 0x1003d8e8) */
            int e = o & 0xffff, oi = (o >> 16) & 0xffff;
            speed += (int)sqrt((double)e) + opt(oi, 2);
            hand += e < 600 ? 0 : e < 0x2ee ? 1 : e < 0x3b6 ? 2 : 3;
            stealth += opt(oi, 1) + (e < 400 ? 1 : e < 600 ? 0 : e < 0x2ee ? -1 : e < 0x3b6 ? -2 : -3);
            cap += e_sc + opt(oi, 3);
            weight += e_w + wtab(oi);
            capacity += e_c + opt(oi, 6);
            break;
        }
        default:
            if (kind >= 8 && kind <= 16) {   /* weapons: 0x1003e6e9, 0x10101d94 */
                weight += gear_equip_sum[id].w;
                capacity += gear_equip_sum[id].wcap;
            }
        }
    }
    weight += armor_sum * wtab(aqual) + wtab(akind);
    float f = capacity > 0 ? (float)weight / (float)capacity : 0;
    /* 0x1003d125: the speed cap by the load and the torso's armor; 0x1003d0a5 the internal factor; 0x1003d251 the
     * speed penalty */
    if (f > 1.26f) cap = 1;
    else {
        int c = f > 1.15 ? -5 : f > 1.1 ? -3 : f > 1.05 ? -2 : f > 1.0f ? -1 : 0;
        float rr = (float)torso_armor / (float)torso_full;
        c += rr == 1.0 ? -3 : rr < 0.9 ? (rr >= 0.75 ? -1 : 0) : -2;
        cap += c;
    }
    g->internal_factor = f > 1.26f ? 0.25f : f > 1.2 ? 0.5f : f > 1.15 ? 0.6f : f > 1.1 ? 0.75f : 1.0f;
    speed += f > 1.26f ? -8 : f > 1.2 ? -6 : f > 1.15 ? -5 : f > 1.1 ? -4 : f > 1.05 ? -3 : f > 1.0f ? -2 : 0;
    if (speed < 1) speed = 1;
    else if (cap < speed) speed = cap;
    g->speed_points = speed;
    g->stealth = stealth;
    g->sensors = sensor;
    g->handling = hand;
    g->weight = weight;
    g->capacity = capacity;
}

int gear_load(hg_archive *a, const char *name, gear *g)
{
    memset(g, 0, sizeof *g);
    size_t len;
    uint8_t *d = hga_load(a, name, "GDF", &len, NULL);
    if (!d) return -1;
    int ok = -1;
    if (len >= 0x88 && rd_u32(d) == 0) {
        size_t nparts = rd_u32(d + 0x84), o = 0x88 + nparts * 0x438;
        if (o + 4 + 0x3c <= len) {
            uint32_t ver = rd_u32(d + o);
            if (ver == 1 || ver == 2) {
                int neq = rd_i32(d + o + 0x24);
                uint32_t hflags = rd_u32(d + o + 0x38);
                size_t p = o + 0x3c;
                ok = 0;
                for (int k = 0; k < neq && p + (size_t)(k + 1) * 0x6c <= len; k++) {
                    const uint8_t *r = d + p + (size_t)k * 0x6c;
                    int kind = rd_i32(r), id = rd_i32(r + 4);
                    int z = ver == 2 ? (kind >= 1 && kind <= 16 ? kind : 0) : v1_zone(kind);
                    g->version = (int)ver;
                    if (ver == 2 && kind == 2) {
                        /* the torso's id picks the crit table (0x1004e6a0's switch) */
                        static const int ct[16] = {1, 1, 2, 3, 3, 4, 4, 5, 1, 1, 2, 3, 3, 4, 4, 5};
                        g->crit_type = id >= 0 && id < 16 ? ct[id] : 0;
                    }
                    if (z) {
                        float ar = (float)rd_i32(r + 8);
                        if (ver == 2) ar += (hflags & 2) ? 10.0f : (hflags & 1) ? 5.0f : 0.0f;   /* 0x10101148 / 44 */
                        g->armor[z - 1] = ar;
                        g->internal[z - 1] = ver == 2 ? v2_internal(kind, id, rd_i32(r + 0xc)) : (float)rd_i32(r + 0xc);
                        g->has_zone[z - 1] = 1;
                    }
                    if (ver != 2) {
                        /* version 1 (0x1004f86d): the records of weapon zones (8 and up) are weapons; the type is the
                         * record's +4, the rounds +0x68 magazines of (+0x30, or +0x34 for 's' mounts: not modelled)
                         * x the burst, the power bonus by +0x64 as for gears */
                        if (z < 8 || id < 0 || id >= GEAR_WEAPON_TYPES || g->nweapons >= GEAR_MAX_WEAPONS) continue;
                        gear_weapon *w = &g->weapon[g->nweapons++];
                        w->kind = kind;
                        w->type = id;
                        w->ammo = rd_i32(r + 0x68) * gear_weapon_stat[id].rounds * gear_weapon_stat[id].burst;
                        int q = rd_i32(r + 0x64);
                        w->power_bonus = q == 1 ? 5 : q == 2 || q == 3 ? 10 : 0;
                        w->name = gear_weapon_name[id];
                        w->icon = gear_weapon_icon[id][0];
                        w->icon_hit = gear_weapon_icon[id][1];
                        continue;
                    }
                    if (kind < 8 || kind > 16 || id < 0 || id >= GEAR_EQUIP_IDS) continue;
                    if (g->nweapons >= GEAR_MAX_WEAPONS) break;
                    gear_weapon *w = &g->weapon[g->nweapons++];
                    w->kind = kind;
                    w->type = gear_weapon_type[id];
                    w->ammo = gear_weapon_ammo[id];
                    {
                        int o = rd_i32(r + 0xc) & 0xffff, q = o < GEAR_OPTIONS ? gear_option[o].ammo : 0;
                        w->power_bonus = q == 1 ? 5 : q == 2 || q == 3 ? 10 : 0;
                    }
                    if (w->type >= 0 && w->type < GEAR_WEAPON_TYPES) {
                        w->name = gear_weapon_name[w->type];
                        w->icon = gear_weapon_icon[w->type][0];
                        w->icon_hit = gear_weapon_icon[w->type][1];
                    }
                }
                if (ver == 2) {
                    int nn = neq;
                    while (nn > 0 && p + (size_t)nn * 0x6c > len) nn--;
                    design(g, d + p, nn);
                    /* the builder's internal factor on every zone (0x1004e6a0 after the records) */
                    for (int z = 0; z < GEAR_ZONES; z++) g->internal[z] = (float)(int)(g->internal[z] * g->internal_factor);
                }
            }
        }
    }
    free(d);
    return ok;
}

static int ok_type(int t) { return t >= 0 && t < GEAR_WEAPON_TYPES; }
int   gear_weapon_kind(int t) { return ok_type(t) ? gear_weapon_stat[t].kind : -1; }
int   gear_weapon_guided(int t) { return ok_type(t) ? gear_weapon_stat[t].guided : 0; }
float gear_weapon_min_range(int t) { return ok_type(t) ? gear_weapon_stat[t].min_range : 0.0f; }
float gear_weapon_max_range(int t) { return ok_type(t) ? gear_weapon_stat[t].max_range : 0.0f; }
