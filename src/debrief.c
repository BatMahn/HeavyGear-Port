#include "debrief.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* the record's +0x138 + mission x 4 (0x101129bf / 0x101129d9): the in-memory record's +0x13c is the file's 128
 * bytes (FUN_10111ce0: param_1 + 0x4f), so mission m (1 .. 32) keeps its best kills at b13c[(m - 1) x 4] */
static int best_get(const career_char *c, int m)
{
    if (m < 1 || m > 32) return 0;
    int32_t v;
    memcpy(&v, c->b13c + (m - 1) * 4, 4);
    return v;
}

static void best_set(career_char *c, int m, int v)
{
    if (m < 1 || m > 32) return;
    int32_t x = v;
    memcpy(c->b13c + (m - 1) * 4, &x, 4);
}

/* 0x1011275d: medal counter i (the record's +0xac + i x 4) + 1; five of 2 make one of 3; five of 8 one of 9, four of 9
 * one of 10; 6 is a flag (1) */
void debrief_medal_count(career_char *c, int i)
{
    if (i < 0 || i >= 14) return;
    c->aac[i]++;
    switch (i) {
    case 2: if (c->aac[2] == 5) { c->aac[2] = 0; c->aac[3]++; } break;
    case 6: c->aac[6] = 1; break;
    case 8: if (c->aac[8] == 5) { c->aac[8] = 0; c->aac[9]++; } /* fall through */
    case 9: if (c->aac[9] == 4) { c->aac[9] = 0; c->aac[10]++; } break;
    }
}

/* 0x100e042c: on a counted success, the faction's (0x10111766: 0 for types 1 / 4) counters by the kills */
static void medal_counts(career_char *c, int kills)
{
    int fac = c->type == 1 || c->type == 4 ? 0 : 1;
    if (fac == 1) {
        for (int i = 0; i < kills; i++) debrief_medal_count(c, 2);
        if (c->stat[11] == 6 || c->stat[11] == 8) debrief_medal_count(c, 0);
    } else {
        for (int i = 0; i < kills; i++) debrief_medal_count(c, 8);
        if (c->stat[7] == 0x12 && c->aac[6] == 0) debrief_medal_count(c, 6);
    }
}

int debrief_score(career_char *c, int cls, int outcome, const debrief_in *in, debrief_list *out)
{
    int kills = in ? in->kills : 0, deaths = in ? in->deaths : 0;
    int complete_pays = 1, kills_before = 0, bonus = 0, kill_bonus = 0, death_pen = 0, objs = 0;
    if (c && cls == 0) {
        int mission = c->stat[11];
        int first = c->stat[10] == mission;   /* bVar7, read before the reached mission moves on */
        if (outcome == 2) {
            complete_pays = first;
            if (first) {
                /* case 2 under (tour || bVar7): 0x100e00be .. 0x100e0154 */
                c->stat[4]++; c->stat[5]++;
                bonus += 100;
                c->stat[0x10]++; c->stat[0x11]++; c->stat[3]++;
                c->stat[0x19] = (int32_t)((uint32_t)c->stat[0x19] << 6 | (uint32_t)(kills & 0x3f));
                medal_counts(c, kills);
                /* the done objectives of order 1 / 2, 100 each (0x100e12df) */
                for (int i = 0; in && i < in->n; i++)
                    if (in->o[i].done && (in->o[i].order == 1 || in->o[i].order == 2)) objs += 100;
                /* 0x100e01eb: stat 0x12 counts on while HGCAR +0x138 (u16) is above 0, else it goes back to 0 */
                if (in && (in->crit_zones & 0xffff) > 0) c->stat[0x12]++;
                else c->stat[0x12] = 0;
            }
            /* 0x100e1398: kills above the mission's best */
            int prev = best_get(c, mission);
            if (kills > prev) {
                kills_before = prev;
                kill_bonus = (kills - prev) * 50;
                c->stat[6] += kills - prev;
                best_set(c, mission, kills);
            } else kills_before = kills;
        } else {
            /* 0x100e118d: the other outcomes */
            if (c->stat[0x10] == mission) c->stat[0x10]++;
            c->stat[0x11] = 0; c->stat[5] = 0; c->stat[0x12] = 0; c->stat[0x13] = 0; c->stat[0x17] = 0;
            c->stat[0x19] = (int32_t)((uint32_t)c->stat[0x19] << 6);
        }
        c->stat[0] += bonus + kill_bonus + death_pen + objs;   /* 0x100e145b */
    } else if (c && cls == 1) {
        /* the tour of duty (0x10111178a = 1): stat 11 is the mission type played */
        int type = c->stat[11];
        if (outcome == 2) {
            /* 0x100e003f: the type's bonus */
            bonus += type == 6 ? 200 : type == 7 ? 50 : type == 8 ? 150 : 0;
            /* 0x100e00be .. 0x100e0154 */
            c->stat[4]++; c->stat[5]++;
            bonus += 100;
            c->stat[0x10]++; c->stat[0x11]++; c->stat[3]++;
            c->stat[0x19] = (int32_t)((uint32_t)c->stat[0x19] << 6 | (uint32_t)(kills & 0x3f));
            /* 0x100e0159: the campaign stats by type (byte table 0x100e2142, jumps 0x100e212e) */
            switch (type) {
            case 0: c->stat[0x15]++; c->stat[0x16] += kills; break;
            case 5: c->stat[0x18]++; break;
            case 7: c->stat[0x14]++; break;
            case 0xc: c->stat[0x17] = kills; break;
            }
            if (in && (in->crit_zones & 0xffff) > 0) c->stat[0x12]++;
            else c->stat[0x12] = 0;
            medal_counts(c, kills);
        } else {
            /* 0x100e118d */
            if (c->stat[0x10] == type) c->stat[0x10]++;
            c->stat[0x11] = 0; c->stat[5] = 0; c->stat[0x12] = 0; c->stat[0x13] = 0; c->stat[0x17] = 0;
            c->stat[3]++;
            c->stat[0x19] = (int32_t)((uint32_t)c->stat[0x19] << 6);
            switch (type) {   /* 0x100e124a */
            case 0: c->stat[0x15] = 0; c->stat[0x16] = 0; break;
            case 5: c->stat[0x18] = 0; break;
            case 7: c->stat[0x14] = 0; break;
            }
        }
        /* 0x100e12df: every done objective of order 1 / 2 pays 100, whatever the outcome */
        for (int i = 0; in && i < in->n; i++)
            if (in->o[i].done && (in->o[i].order == 1 || in->o[i].order == 2)) objs += 100;
        /* 0x100e143b: every kill 50, every death -50 */
        kill_bonus = kills * 50;
        death_pen = deaths * -50;
        c->stat[0] += bonus + kill_bonus + death_pen + objs;
    }
    if (out) {
        memset(out, 0, sizeof *out);
        /* 0x100e35cc: the objectives with text and order 1 or 2 */
        for (int i = 0; in && i < in->n && out->n < DEBRIEF_MAX; i++) {
            const debrief_obj *o = &in->o[i];
            if (!o->text[0] || (o->order != 1 && o->order != 2)) continue;
            snprintf(out->item[out->n].text, sizeof out->item[out->n].text, "%s", o->text);
            out->item[out->n].cat = o->done ? 2 : 1;
            out->item[out->n].value = o->done && complete_pays ? 100 : 0;   /* 0x100b9fad, [0x35] and [0x37] */
            out->n++;
        }
        out->bonus = bonus;                         /* var 0x1d */
        out->kill_bonus = (kills - kills_before) * 50;
        out->death_penalty = deaths * -50;
        out->total = out->bonus + out->kill_bonus + out->death_penalty;
        for (int i = 0; i < out->n; i++) out->total += out->item[i].value;
        out->career = c ? c->stat[0] : 0;
        out->valid = 1;
    }
    return bonus + kill_bonus + death_pen + objs;
}
