#include "barracks.h"

#include "squad.h"

#include <stdio.h>
#include <string.h>

/* HGSHELL 0x102569f8 (set by 0x101129f6 through the language table, whose keys are the English text) */
static const char *const unit_names[BARRACKS_UNITS] = {
    "21st Black Jackals", "67th Harvest Wind", "11th Prophet's Wish", "29th Northern Star", "81st Templars",
    "31st Grassrunners", "55th StormRiders", "74th Nova Redriders", "33rd Roving Guns", "7th Cat's Paws",
    "17th Sand Demons", "21st Green Hell", "5th Tin Men", "12th Ignus Legionaires", "49th Silver Vipers (MILICIA)",
    "12th Azure Devils (MILICIA)", "99th Skyhawks", "1st Pride of the Republic", "2nd Legion Noire - The Damned",
};

/* 0x101ab9a0 + unit x 0x100 */
static const char *const unit_perks[BARRACKS_UNITS] = {
    "Effective combat priority never exceeds B1.  +10 Victory points for every Garrison completed with a member of "
    "the Black Jackals in the game.",
    "None.",
    "None.",
    "None.",
    "Effective combat priority never falls below a C5.",
    "Can perform Rear Area Raid; Supply Raids with a member of the Grassrunners in the mission return double the "
    "normal amount of supply points.",
    "Can perform the Rear Area Raid or Rear Area Strike Mission.",
    "Effective combat priority never falls below a B5.",
    "May take Rear Area Raid mission, may take Pursue Mission without winning a Battle Mission.",
    "May participate in Rear Area Raid and Rear Area Strike missions.  Minimum Combat Priority of B5 at all times.",
    "Combat Priority never goes above B1.",
    "Combat Priority never goes above B1.",
    "None.",
    "Never falls below C7 combat priority.",
    "Minimum Combat priority is always at least a C1.",
    "May take Rear Area Raid mission.  +1 intelligence point for every recon mission successfully completed with a "
    "member of the 12th Azure Devils in the mission.",
    "May participate in Rear Area Raid.",
    "Minimum combat priority is always at least a B1.",
    "May participate in Rear Area Strike missions.  Minimum of B5 combat priority at all times.  +10 victory points "
    "for any Rear Area Strike missions with a member of The Damned.",
};

/* 0x10254808 (0x100e7725) */
static const char *const rank_names[23] = {
    "Corporal", "Senior Corporal", "Ranger", "Senior Ranger", "Sergeant", "Senior Sergeant", "Sergeant Major",
    "Lieutenant", "Captain", "Major", "Colonel", "Sous-Caporal", "Caporal", "Sous-Sergent", "Sergent",
    "Sous-Adjudant", "Adjudant", "Adjudant-Chef", "Sergent-Major", "Sous-Lieutenant", "Lieutenant",
    "Sous-Commandant", "Commandant",
};

/* 0x102564b8 + flag x 0x10 (0x101129f6) */
static const char *const flag_names[22] = {
    "Speed Increase 1", "Speed Increase 2", "Speed Increase 3", "Control Increase", "Skater", "Auto Target",
    "Missile Warning", "Auto Target Missile", "Land Mine Detection", "Faster Reload", "Recoil Control 1",
    "Recoil Control 2", "Recoil Control 3", "Precision Firing", "Clever Sensors", "Clever ECM", "Smart Radar",
    "Armor Bonus 1", "Armor Bonus 2", "Damage Control", "Secondary Sensors", "Pose",
};

const char *barracks_unit_name(int u) { return u >= 0 && u < BARRACKS_UNITS ? unit_names[u] : ""; }
const char *barracks_unit_perk(int u) { return u >= 0 && u < BARRACKS_UNITS ? unit_perks[u] : ""; }
const char *barracks_rank_name(int r) { return r >= 0 && r < 23 ? rank_names[r] : ""; }

const char *barracks_priority_name(int level)
{
    /* 0x101acda0: 5-byte strings "C9", "C8" .. "C1", "B9" .. "B1", "A9" .. "A1" */
    static char b[27][3];
    if (level < 0 || level > 26) return "";
    b[level][0] = (char)('C' - level / 9);
    b[level][1] = (char)('9' - level % 9);
    b[level][2] = 0;
    return b[level];
}

int barracks_side(int type)
{
    return type == 1 || type == 4 ? 0 : 1;
}

static void cat(char *dst, size_t cap, const char *s)
{
    size_t n = strlen(dst);
    if (n + 1 < cap) snprintf(dst + n, cap - n, "%s", s);
}

void barracks_build(const career_char *c, shell_barracks *b)
{
    memset(b, 0, sizeof *b);
    b->rank_pic = -1;
    b->unit_sel = -1;
    if (!c) return;
    int side = barracks_side(c->type), cls = career_class(c->type);
    /* the rank (0x100eb738) */
    int r = c->stat[2];
    if (r >= 0 && r <= 10) b->rank_pic = 0x7b - r;
    else if (r >= 11 && r <= 22) b->rank_pic = 0x87 - (r - 11);
    /* the medals (0x100eaebd), in its order */
    static const int south[8][2] = {{6, 0xa9}, {7, 0xaf}, {8, 0x9d}, {9, 0xa0}, {10, 0xa3}, {11, 0xb2}, {12, 0xac}, {13, 0xa6}};
    static const int north[7][2] = {{0, 0x8e}, {1, 0x91}, {2, 0x94}, {3, 0x97}, {4, 0x8b}, {5, 0x88}, {13, 0x9a}};
    const int (*m)[2] = side == 0 ? south : north;
    int nm = side == 0 ? 8 : 7;
    for (int i = 0; i < nm; i++)
        if (c->aac[m[i][0]] > 0) {
            b->medal_pic[b->nmedal] = m[i][1];
            b->medal_n[b->nmedal++] = c->aac[m[i][0]];
        }
    /* the abilities (0x100ecd04) */
    const int32_t *fl = c->flag;
    int lines = 0;
#define LINE(k) do { cat(b->ai, sizeof b->ai, flag_names[k]); cat(b->ai, sizeof b->ai, "\n"); lines++; } while (0)
    if (fl[2]) LINE(2); else if (fl[1]) LINE(1); else if (fl[0]) LINE(0);
    for (int k = 3; k <= 9; k++) if (fl[k]) LINE(k);
    if (fl[12]) LINE(12); else if (fl[11]) LINE(11); else if (fl[10]) LINE(10);
    for (int k = 13; k <= 16; k++) if (fl[k]) LINE(k);
    if (fl[18]) LINE(18); else if (fl[17]) LINE(17);
    for (int k = 19; k <= 21; k++) if (fl[k]) LINE(k);
#undef LINE
    if (!lines) cat(b->ai, sizeof b->ai, "<NONE>");
    /* the statistics (0x100efde4): the story's player is Edward Scott (0x102566f8), others the record's name */
    int level = squad_tech_level(c->type, c->stat[0], c->a8c[side == 1 ? 2 : 6], c->stat[7]);
    snprintf(b->stats, sizeof b->stats,
             "Name:  %s\nScore:  %d\nMissions Won/Lost:  %d/%d\nEnemy Kills:  %d\nRank:  %s\nCombat Priority:  %s",
             cls == 0 ? "Edward Scott" : c->name, (int)c->stat[0], (int)c->stat[4], (int)(c->stat[3] - c->stat[4]),
             (int)c->stat[6], barracks_rank_name(r), barracks_priority_name(level));
    /* the units (0x100edff3) */
    int first = 0, last = -1;
    if (side == 1 && cls == 0) { first = last = 1; }
    else if (side == 1) { first = 0; last = 9; }
    else { first = 10; last = 18; }
    for (int u = first; u <= last && b->nunit < BARRACKS_UNITS; u++) {
        if (!(side == 1 && cls == 0) && !((uint32_t)c->stat[8] >> u & 1)) continue;
        int i = b->nunit++;
        b->unit[i] = u;
        b->unit_pic[i] = 0x5e + u;
        snprintf(b->unit_text[i], sizeof b->unit_text[i], "%s\n\nPerks:  %s", unit_names[u], unit_perks[u]);
    }
    for (int i = 0; i < b->nunit; i++) if (b->unit[i] == c->stat[7]) b->unit_sel = i;
    if (b->unit_sel < 0 && b->nunit) b->unit_sel = 0;
}
