#include "garagepanel.h"

#include "barracks.h"
#include "garage.h"
#include "shell_picture.h"
#include "shell_tables.h"

#include <stdio.h>

static void spinners(shell *sh, int hp, int type)
{
    static const char *const hpb[5] = {"B07HPSP", "B07HPDS", "B07HPDF", "B07HPUS", "B07HPUF"};
    static const char *const tyb[3] = {"B07_TYP", "B07TYLT", "B07TYRT"};
    for (int i = 0; i < 5; i++) { if (hp) shell_show(sh, hpb[i]); else shell_hide(sh, hpb[i]); }
    for (int i = 0; i < 3; i++) { if (type) shell_show(sh, tyb[i]); else shell_hide(sh, tyb[i]); }
}

void garage_panel(shell *sh, const design *g, int pick, int level)
{
    char prty[40];
    snprintf(prty, sizeof prty, "Combat Priority %s", barracks_priority_name(level));
    const char *weap[16];
    int col[16], nw = 0;
    int ids[DESIGN_ITEMS], ar[DESIGN_ITEMS];
    unsigned op[DESIGN_ITEMS];
    for (int k = 0; k < g->nitem; k++) {
        int id = g->item[k].id, t = garage_type(id);
        ids[k] = id; ar[k] = g->item[k].armor; op[k] = (unsigned)g->item[k].options;
        if (t >= 5 && t <= 10 && nw < 16) {
            int v = (int)((unsigned)g->item[k].options >> 16);
            weap[nw] = id >= 0 && id < SHELL_EQUIP_IDS ? shell_equip_short[id] : "";
            col[nw++] = v == 0 ? 0x0f : v == 1 ? 0xb7 : v == 2 ? 0x07 : 0xfe;
        }
    }
    static char text[256];
    text[0] = 0;
    int sel = pick >= 0 && pick < g->nitem;
    if (!sel && g->nitem) garage_design_text(ids, ar, op, g->nitem, g->b30, g->b31, text, sizeof text);
    shell_garage_panel(sh, prty, weap, col, nw, text);
    int hp = 0, type = 0;
    if (sel) {
        int id = g->item[pick].id, t = garage_type(id);
        if (t == 1) hp = type = 1;
        else if (t >= 5 && id >= 0 && id < SHELL_EQUIP_IDS) {
            int a = shell_equip_field[id][8];   /* +0x3e */
            type = a >= 2 && a <= 4;
        }
    }
    spinners(sh, hp, type);
}
