#include "front.h"

#include "design.h"
#include "garagepanel.h"
#include "garage.h"
#include "tour.h"

#include "audio.h"
#include "barracks.h"
#include "options.h"
#include "mpscreens.h"
#include "brf.h"
#include "career.h"
#include "debrief.h"
#include "shell.h"
#include "shell_picture.h"
#include "shell_tables.h"
#include "squad.h"

#include "fsutil.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OP_END, OP_GOTO, OP_SETVAR, OP_GOTO_VAR, OP_PUSH, OP_POP, OP_SETSTR, OP_QUIT, OP_LAUNCH, OP_BY_CLASS,
    OP_SELECT_GAME,     /* FUN_100ea5b9: the game list's selection becomes the current game */
    OP_LIST_VAR,        /* FUN_100181c5: a variable from the mission list's selection */
    OP_NEW_GAME,        /* FUN_100ea492: a new game named by the text entry, of kind var 0x29 */
    OP_SET_MISSION,     /* FUN_100e2421: the current game's mission (stat 12) from var a (var b when a is 0x22) */
    OP_MISSION_VAR,     /* FUN_100f0739: var a from the current game's mission */
    OP_SAVE,            /* the career written back (the original saves on leaving the shell, FUN_1012f17b) */
    OP_NARRATE,         /* FUN_100ffb39 -> FUN_10072b30: start the briefing's narration, or stop it */
    OP_SQUAD,           /* a pilot list's arrow (FUN_10076b6f: 0x10076ff7 previous / 0x10076f1a next): a = list, b = dir */
    OP_PILOTS_LOST,     /* FUN_100df73d -> 0x100df783: the wingmen killed in the mission (the counters' +0x142 / +0x144)
                         * die in the game record (+0x25c + pilot x 0x14 = 0) */
    OP_GEAR,            /* a wingman's gear list arrow (FUN_1009a07f): a = list, b = dir; the pilot keeps it */
    OP_ANIM,            /* FUN_10015965: the screen's animation slot a in mode b (3: the next actions wait for it) */
    OP_GARAGE_CAT,      /* B07's category buttons (FUN_10076573 + FUN_100f11d7): the left list shows type a */
    OP_GARAGE_SCROLL,   /* the left list's arrows: a = dir */
    OP_GARAGE_GEAR,     /* the gear name selector's arrows (FUN_100a041c, the briefing's gear list): a = dir */
    OP_GARAGE_ACCEPT,   /* B07's accept (FUN_1013fa9f, event 8): under 5 of top speed the question, else keep and back */
    OP_GARAGE_KEEP,     /* FUN_100f08c0: the selector's gear becomes the game's (the record's +0x14) */
    OP_GARAGE_DELETE,   /* delete piece (0x100f1aca -> the view's 0x100a9db6 -> 0x100bf2c0): the selected item goes */
    OP_BARRACKS_EXIT,   /* FUN_100efccb -> 0x100efd1a: stat 7 = the unit list's selected unit */
    OP_OPTIONS,         /* B64: a = 0 accept (FUN_100e3bae: the spinners saved), 1 cancel (FUN_100e3ed0), 2 defaults */
    OP_TOUR_ROUTE,      /* FUN_100e8f73 (a, b, c, d, e) -> 0x100e9072: story c, tour a (faction 1) / b, multiplayer d / e */
    OP_TOUR_NEW,        /* B08's accept (FUN_100ea492 -> FUN_100ea4fc): a tour game of B08's name, faction and unit */
    OP_TOUR_GEN,        /* FUN_100fcbc3 (0x15, 0x16, 0x17, 0x19, 2, 3) -> 0x100fcc3f: the random mission */
    OP_TOUR_TYPE,       /* FUN_100181c5 (B04's list, 0x17): var 0x17 = the selected mission type */
    OP_TOUR_PICK,       /* B08's spinners: a = 0 faction, 1 unit; b = dir */
    OP_MP,              /* the multiplayer screens' actions (src/mpscreens.h mps_action): a = the action, b = dir */
    OP_TRAIN_SCN,       /* FUN_100e2504 (list, 2): string 2 = the scenario of the training list's mission */
};

typedef struct { int op, a, b, c; const char *s; } action;

/* the table below leaves unused operands out */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

/* the buttons' actions, as the screen builders attach them (FUN_101391b8 B00, FUN_10149529 B20, FUN_101490b5 B19,
 * FUN_1016aae2 B72, FUN_1014765e B15, FUN_1013c185 B05, FUN_10162f23 B62, FUN_1013ee54 B06). Not here yet: the
 * preloading and list-fill actions without a visible effect. */
static const struct { int state; const char *stem; action act[8]; } buttons[] = {
    {0x00, "B00STRY", {{OP_SETVAR, 0x29, 0}, {OP_GOTO, 0x14}}},
    {0x00, "B00TOUR", {{OP_SETVAR, 0x29, 1}, {OP_GOTO, 0x01}}},
    {0x00, "B00TRNG", {{OP_GOTO, 0x10}}},
    {0x00, "B00MULT", {{OP_SETVAR, 0x29, 2}, {OP_SETVAR, 0x1a, 0x22}, {OP_GOTO, 0x01}}},
    {0x00, "B00EXIT", {{OP_QUIT}}},
    {0x14, "B20DEL_", {{OP_SETSTR, 0x13, 0, 0, "Are you sure you want to delete this game?"}, {OP_PUSH, 0x48}}},
    {0x14, "B20NEW_", {{OP_GOTO, 0x13}}},
    {0x14, "B20ACCT", {{OP_SELECT_GAME}, {OP_LIST_VAR, 0x1a}, {OP_SETVAR, 2, 0x0f}, {OP_SETVAR, 0x19, -1},
                       {OP_SETVAR, 1, 0x14}, {OP_SAVE}, {OP_GOTO, 0x0e}}},
    {0x14, "B20CNCL", {{OP_GOTO, 0x00}}},
    {0x13, "B19ACCT", {{OP_NEW_GAME}, {OP_SAVE}, {OP_GOTO, 0x14}}},
    {0x13, "B19CNCL", {{OP_GOTO, 0x14}}},
    {0x48, "B72ACCT", {{OP_SETVAR, 0x2a, 1}, {OP_POP}}},
    {0x48, "B72ABRT", {{OP_SETVAR, 0x2a, 0}, {OP_POP}}},
    /* the hub: each door plays its animation first (FUN_10015965 slot 2 / 3 / 4 in mode 3) */
    {0x0f, "B15BRIF", {{OP_ANIM, 2, 3}, {OP_GOTO, 0x05}}},
    {0x0f, "B15BARR", {{OP_SETVAR, 1, 0x0f}, {OP_ANIM, 3, 3}, {OP_GOTO, 0x03}}},
    {0x0f, "B15EXIT", {{OP_ANIM, 4, 3}, {OP_GOTO, 0x14}}},
    {0x05, "B05ABRT", {{OP_BY_CLASS, 0x04, 0x0f, -1}}},
    {0x05, "B05SITU", {{OP_SETVAR, 1, 0x05}, {OP_GOTO, 0x3e}}},
    {0x05, "B05BBRF", {{OP_NARRATE}}},
    {0x05, "B05P1PR", {{OP_SQUAD, 0, -1}}},
    {0x05, "B05P1NT", {{OP_SQUAD, 0, 1}}},
    {0x05, "B05P2PR", {{OP_SQUAD, 1, -1}}},
    {0x05, "B05P2NT", {{OP_SQUAD, 1, 1}}},
    {0x05, "B05PREV", {{OP_GEAR, 2, -1}}},
    {0x05, "B05NEXT", {{OP_GEAR, 2, 1}}},
    {0x05, "B05P1GP", {{OP_GEAR, 0, -1}}},
    {0x05, "B05P1GN", {{OP_GEAR, 0, 1}}},
    {0x05, "B05P2GP", {{OP_GEAR, 1, -1}}},
    {0x05, "B05P2GN", {{OP_GEAR, 1, 1}}},
    {0x05, "B05BILD", {{OP_SETVAR, 1, 0x05}, {OP_SETVAR, 5, 1}, {OP_GOTO, 0x07}}},
    /* the gear editor (FUN_1013fa9f). Not here yet: clear (its confirmation, 0x1023d828), details, delete piece, the
     * armour arrows, and accept's checks and saving (0x100f2585: the low top speed question 0x1023d720,
     * 0x100fc12d, 0x100fb6b8, 0x100f15cf) */
    {0x07, "B07TRSO", {{OP_GARAGE_CAT, 0}}},
    {0x07, "B07ENGN", {{OP_GARAGE_CAT, 1}}},
    {0x07, "B07HEAD", {{OP_GARAGE_CAT, 2}}},
    {0x07, "B07ARMS", {{OP_GARAGE_CAT, 3}}},
    {0x07, "B07LEGS", {{OP_GARAGE_CAT, 4}}},
    {0x07, "B07RFLE", {{OP_GARAGE_CAT, 5}}},
    {0x07, "B07ROCK", {{OP_GARAGE_CAT, 6}}},
    {0x07, "B07GREN", {{OP_GARAGE_CAT, 7}}},
    {0x07, "B07MISS", {{OP_GARAGE_CAT, 8}}},
    {0x07, "B07ZOOK", {{OP_GARAGE_CAT, 9}}},
    {0x07, "B07ENRG", {{OP_GARAGE_CAT, 10}}},
    {0x07, "B07ARBU", {{OP_GARAGE_SCROLL, -1}}},
    {0x07, "B07ARBD", {{OP_GARAGE_SCROLL, 1}}},
    {0x07, "B07G_LT", {{OP_GARAGE_GEAR, -1}}},
    {0x07, "B07G_RT", {{OP_GARAGE_GEAR, 1}}},
    {0x07, "B07ACCT", {{OP_GARAGE_ACCEPT}}},
    {0x07, "B07CNCL", {{OP_GOTO_VAR, 1}}},
    {0x07, "B07DEL_", {{OP_GARAGE_DELETE}}},
    /* clear (0x19b): var 0x2b = 0, the question (0x1023d828), the confirmation popup; its yes clears (pop below) */
    {0x07, "B07CLR_", {{OP_SETVAR, 0x2b, 0}, {OP_SETSTR, 0x13, 0, 0, "Are you sure you want to clear your gear?"}, {OP_PUSH, 0x48}}},
    {0x05, "B05LNCH", {{OP_SETVAR, 0, 0x06}, {OP_SET_MISSION, 0x1a, 0x17}, {OP_SAVE}, {OP_LAUNCH}}},
    {0x06, "B06LNCH", {{OP_SETVAR, 0, 0x06}, {OP_SET_MISSION, 0x1a, 0x17}, {OP_SAVE}, {OP_LAUNCH}}},
    {0x06, "B06ABRT", {{OP_GOTO, 0x14}}},
    {0x06, "B06CONT", {{OP_PILOTS_LOST}, {OP_MISSION_VAR, 0x1a}, {OP_SETVAR, 2, 0x05}, {OP_SETVAR, 1, 0x06}, {OP_BY_CLASS, 0x04, 0x0e, 0x0e}}},
    {0x03, "B03EXIT", {{OP_BARRACKS_EXIT}, {OP_GOTO_VAR, 1}}},
    /* training (FUN_10147d38, B16): launch = var 0 = 0x11, string 2 = the mission's scenario (FUN_100e2504), launch;
     * the briefing button plays the narration (FUN_100ffb39); abort to the main menu */
    {0x10, "B16BBRF", {{OP_NARRATE}}},
    {0x10, "B16LNCH", {{OP_SETVAR, 0, 0x11}, {OP_TRAIN_SCN}, {OP_LAUNCH}}},
    {0x10, "B16ABRT", {{OP_GOTO, 0x00}}},
    /* the training debriefing (FUN_10148c3b, B17): launch again (var 0 = 0x11, FUN_100df6b3), abort to training */
    {0x11, "B06LNCH", {{OP_SETVAR, 0, 0x11}, {OP_LAUNCH}}},
    {0x11, "B17ABRT", {{OP_GOTO, 0x10}}},
    /* the options popup (FUN_10163311, B63, opened by Esc): credits 0x43, environment (var 0x1e = 0, 0x40), return to
     * the main menu, quit, cancel (the state in var 3). Controls (var 0x1e = 1, 0x41, B65) isn't done */
    {0x3f, "B63CRED", {{OP_GOTO, 0x43}}},
    {0x3f, "B63ENVR", {{OP_SETVAR, 0x1e, 0}, {OP_GOTO, 0x40}}},
    /* B63's return: FUN_10019126 runs the game object's (the shell's +0x44, FUN_1011e6cb, vtable 0x101c5bc8) second
     * method, FUN_1002abb8: an open multiplayer session closed (dpDestroy); nothing in single-player */
    {0x3f, "B63RETN", {{OP_GOTO, 0x00}}},
    {0x3f, "B63QUIT", {{OP_QUIT}}},
    {0x3f, "B63CNCL", {{OP_POP}}},
    /* the environment screen (FUN_10163a91, B64) */
    {0x40, "B64ACCT", {{OP_OPTIONS, 0}, {OP_GOTO_VAR, 3}}},
    {0x40, "B64CNCL", {{OP_OPTIONS, 1}, {OP_GOTO_VAR, 3}}},
    {0x40, "B64DFLT", {{OP_OPTIONS, 2}}},
    /* the credits (FUN_10169569, B67) */
    {0x43, "B67CNCL", {{OP_GOTO_VAR, 3}}},
    /* the Tour of Duty: B01 (FUN_101398eb) its games; B08 (FUN_10145504) a new one; B02 / B09 (FUN_1013a75a /
     * FUN_10145eff) the hubs; B04 (FUN_1013b3e7) the mission choice, then B05 and B06 as the story's */
    {0x01, "B01DEL_", {{OP_SETSTR, 0x13, 0, 0, "Are you sure you want to delete this game?"}, {OP_PUSH, 0x48}}},
    /* the multiplayer screens (FUN_1014a59f B21, FUN_1015118b B36, FUN_1015d831 B49, FUN_101581dd B46, FUN_1015af7c
     * B47, FUN_10152777 B39; src/mpscreens.h) */
    {0x15, "B21ACCT", {{OP_MP, 0}}},
    {0x15, "B21CNCL", {{OP_GOTO, 0x01}}},
    {0x24, "B36CREA", {{OP_GOTO, 0x31}}},
    {0x24, "B36JOIN", {{OP_MP, 2}}},
    {0x24, "B36BARR", {{OP_SETVAR, 0x24, 1}, {OP_GOTO, 0x03}}},
    {0x24, "B36ABRT", {{OP_MP, 3}, {OP_GOTO, 0x01}}},
    {0x24, "B36S_UP", {{OP_MP, 9, -1}}},
    {0x24, "B36S_DN", {{OP_MP, 9, 1}}},
    {0x31, "B49CREA", {{OP_MP, 4}}},
    {0x31, "B49CNCL", {{OP_GOTO, 0x24}}},
    {0x31, "B49C_UP", {{OP_MP, 12, 1}}},
    {0x31, "B49C_DN", {{OP_MP, 12, -1}}},
    {0x2e, "B46LNCH", {{OP_MP, 6}}},
    {0x2e, "B46ABRT", {{OP_MP, 7}}},
    {0x2e, "B46M_LT", {{OP_MP, 13, -1}}},
    {0x2e, "B46M_RT", {{OP_MP, 13, 1}}},
    {0x2f, "B47REDY", {{OP_MP, 8}}},
    {0x2f, "B47ABRT", {{OP_MP, 7}}},
    {0x27, "B39CONT", {{OP_GOTO, 0x24}}},
    {0x27, "B39ABRT", {{OP_SETVAR, 0x29, 2}, {OP_GOTO, 0x01}}},
    {0x01, "B01ACCT", {{OP_SELECT_GAME}, {OP_SETVAR, 0x15, -2}, {OP_TOUR_ROUTE, 2, 9, -1}}},
    {0x01, "B01NEW_", {{OP_SETVAR, 1, 1}, {OP_GOTO, 0x08}}},
    {0x01, "B01ABRT", {{OP_GOTO, 0x00}}},
    {0x08, "B08ACCT", {{OP_TOUR_NEW}, {OP_SAVE}, {OP_GOTO_VAR, 1}}},
    {0x08, "B08CNCL", {{OP_GOTO_VAR, 1}}},
    {0x08, "B08ALLT", {{OP_TOUR_PICK, 0, -1}}},
    {0x08, "B08ALRT", {{OP_TOUR_PICK, 0, 1}}},
    {0x08, "B08PREV", {{OP_TOUR_PICK, 1, -1}}},
    {0x08, "B08NEXT", {{OP_TOUR_PICK, 1, 1}}},
    {0x02, "B02BARR", {{OP_ANIM, 2, 3}, {OP_SETVAR, 1, 2}, {OP_GOTO, 0x03}}},
    {0x02, "B02TTDC", {{OP_ANIM, 3, 3}, {OP_GOTO, 0x04}}},
    {0x02, "B02CHAR", {{OP_ANIM, 4, 3}, {OP_GOTO, 0x01}}},
    {0x09, "B09BARR", {{OP_ANIM, 4, 3}, {OP_SETVAR, 1, 9}, {OP_GOTO, 0x03}}},
    {0x09, "B09TTDC", {{OP_ANIM, 1, 4}, {OP_ANIM, 2, 3}, {OP_GOTO, 0x04}}},
    {0x09, "B09CHAR", {{OP_ANIM, 3, 3}, {OP_GOTO, 0x01}}},
    {0x04, "B04ACCT", {{OP_TOUR_TYPE}, {OP_TOUR_GEN}, {OP_GOTO, 0x05}}},
    {0x04, "B04EXIT", {{OP_TOUR_ROUTE, 2, 9, -1}}},
};

struct front {
    hg_archive *a;
    debrief_in  result;         /* the sim's objectives, kills and deaths for the debriefing (hgmsn.cfg / HGCAR) */
    shell      *sh;
    career      k;
    char        path[1100];
    int         cur;            /* the current game, -1 none */
    int         var[64];
    char        str[64][128];
    int         state;
    int         under;          /* the state under a popup state (0x13 over 0x14, a pushed one) */
    int         stack[8], nstack;
    int         request;
    mps        *mp;             /* the multiplayer screens' session (src/mpscreens.h), made on first use */
    int         success;        /* the last mission's outcome */
    char        scenario[16];
    squad       sq;             /* the wingmen the launch takes (src/squad.h) */
    int         sq_l[2][11], sq_n[2], sq_north, sq_cur, sq_mask, sq_set;
    char        gl[64][32], gl_disp[64][32];  /* the gear list (0x1009a38b): its names as the game record keeps them
                                               * ("GACHEETA", a folder's "MYGEAR.GDF") and display names */
    int         ngl, gsel[2], pgsel;
    char        pgear[32];       /* the player's gear (the game record's +0x14 string, the port's desc) */
    int         sq_lost[2];     /* the wingmen killed in the last mission */
    int         g_cat, g_top, g_sel;   /* the gear editor: the category shown, the list's first item, the design */
    design      g_des;                 /* the design being edited (src/design.h) */
    int         g_drag;                /* the item being dragged from the list, -1 none */
    int         g_pick;                /* the design item selected on the model (the view's +0x92), -1 none */
    int         g_ids[128], g_n;       /* the list's items */
    float       g_turn;                /* the design's turn about y, degrees (0x1011b5fd: 180, the arrow keys) */
    int         g_root;                /* the equipment id the view's root object was made from, -1 none */
    int         g_list_key;            /* the skeleton and side the list was filled for */
    int         g_mouse;               /* the mouse button is held on the view (+0x9e) */
    int         key_left, key_right;   /* the arrow keys held (GetKeyboardState, FUN_100f1516) */
    char        b_gear[32];            /* the gear the briefing's view shows */
    int         b_dark, b_nolaunch;    /* its tech over the game's level; the launch disabled (0x100fcb39) */
    float       b_turn;                /* its turn about y, degrees */
    char        movie[16];
    const action *pending;      /* actions waiting for a mode-3 animation (FUN_1003236d) */
    int         npending;
    int         list_game[64];  /* the game list's items -> career index */
    /* sound: the screen's loop (an index into the shell's WAV table, 0x101ddc50) and the narration */
    hg_wave     loop_wave[7];
    int         loop_tried[7];
    int         loop_cur, loop_voice;
    hg_wave     voice_wave;
    int         voice;
    int         narr_on, narr_part;
    uint64_t    now, narr_start;
    int         nlist_game;
    int         training;
    int         t_faction, t_unit;   /* B08's spinners */
    int         t_types[16], t_ntypes, t_sel;   /* B04's mission types */
    rmg         t_rmg;          /* the last generated mission */
    int         t_ok;       /* the launch is training's (string 2's scenario, no squad, no career update) */
    shell_barracks bar;          /* the barracks' contents (src/barracks.h) */
    options     opt;             /* hgdif.cfg / hgsnd.cfg (src/options.h) */
    char        dir[1100];       /* the game folder */
    uint64_t    credits_t0;
};

/* the gear editor's 3D view (0x101e175a) */
#define GARAGE_VX0 233
#define GARAGE_VY0 88
#define GARAGE_VX1 469
#define GARAGE_VY1 461

static int is_popup(int state)
{
    return state == 0x13 || state == 0x48 || state == 0x3f || state == 0x15 || state == 0x31;   /* B21 / B49: SHP popups */
}

static const char *screen_of(int state)
{
    static char b[16];
    switch (state) {
    case 0x13: return "B19UNDER";
    case 0x48: return "B72UNDER";
    default: snprintf(b, sizeof b, "B%02dUNDER", state); return b;
    }
}

static void save(front *f)
{
    if (career_save(&f->k, f->path)) fprintf(stderr, "could not write %s\n", f->path);
}

static int game_class(const front *f)
{
    return f->cur >= 0 && f->cur < f->k.count ? career_class(f->k.c[f->cur].type) : 0;
}

/* B20's lists: the games of the kind chosen in the main menu (FUN_100ea1b2), the current one selected; the
 * selected game's missions reached, 1 .. stat 10, by their titles, the current one (stat 12) selected (FUN_100f0a5f) */
static void fill_missions(front *f)
{
    int g = shell_list_selected(f->sh, "B20CLIST");
    const char *items[64];
    int n = 0, sel = 0;
    if (g >= 0 && g < f->nlist_game) {
        const career_char *c = &f->k.c[f->list_game[g]];
        if (career_class(c->type) == 0) {
            for (int i = 1; i <= c->stat[CAREER_REACHED] && i < SHELL_CAMPAIGN && n < 64; i++) items[n++] = shell_campaign[i].title;
            sel = c->stat[CAREER_MISSION] - 1;
        }
    }
    shell_list_set(f->sh, "B20MLIST", items, n, sel);
}

/* B01's info boxes (FUN_100ebab2 -> FUN_100ebbde, on the game list's selection; FUN_101398eb's FUN_100140a7 boxes
 * 0x1d / 0x1f / 0x21 / 0x24, their inner rectangles, font 9, 0xfe): missions (stat 3), kills (stat 6), the rank's
 * name (0x10254808 + stat 2 x 0x10), the allegiance (FUN_10111766: "North" 0x10254d60 / "South" 0x10254778); empty
 * with no game selected */
static void b01_info(front *f)
{
    int g = shell_list_selected(f->sh, "B01CLIST");
    const career_char *c = g >= 0 && g < f->nlist_game ? &f->k.c[f->list_game[g]] : NULL;
    char m[16], k[16];
    if (c) { snprintf(m, sizeof m, "%d", c->stat[3]); snprintf(k, sizeof k, "%d", c->stat[6]); }
    shell_tbox_set(f->sh, 0, 314, 135, 410, 158, c ? m : "");
    shell_tbox_set(f->sh, 1, 430, 135, 526, 158, c ? k : "");
    shell_tbox_set(f->sh, 2, 314, 74, 521, 96, c ? barracks_rank_name(c->stat[CAREER_RANK]) : "");
    shell_tbox_set(f->sh, 3, 353, 187, 457, 206, c ? (tour_faction(c->type) == 1 ? "North" : "South") : "");
}

static void fill_games(front *f)
{
    const char *items[64];
    int n = 0, sel = 0;
    for (int i = 0; i < f->k.count && n < 64; i++) {
        if (career_class(f->k.c[i].type) != f->var[0x29]) continue;
        if (i == f->cur) sel = n;
        f->list_game[n] = i;
        items[n++] = f->k.c[i].name;
    }
    f->nlist_game = n;
    if (f->state == 0x01) {
        shell_list_set(f->sh, "B01CLIST", items, n, sel);
        b01_info(f);
        return;
    }
    shell_list_set(f->sh, "B20CLIST", items, n, sel);
    fill_missions(f);
}

/* each screen's looping sound (FUN_100277c5's fourth argument; FUN_10029a8a plays it on entering the state):
 * an index into the WAV table (1 AFXSHABK, 2 AFXSHABR, 3 AFXSHADB, 4 AFXSHANB, 5 AFXSHASB, 6 AFXSHAVB); -1 keeps
 * the one playing (the popups and the screens without one) */
static int screen_sound(int state)
{
    static const signed char snd[0x4c] = {
        [0x00] = 6, [0x01] = 6, [0x02] = 4, [0x03] = 1, [0x05] = 2, [0x06] = 3, [0x09] = 5, [0x0c] = 2, [0x0f] = 6,
        [0x10] = 2, [0x11] = 3, [0x14] = 6, [0x18] = 2, [0x19] = 2, [0x27] = 3, [0x2e] = 2, [0x2f] = 2,
    };
    return state >= 0 && state < 0x4c && snd[state] ? snd[state] : -1;
}

static const char *const sound_names[7] = {"", "AFXSHABK", "AFXSHABR", "AFXSHADB", "AFXSHANB", "AFXSHASB", "AFXSHAVB"};

static int load_wave(front *f, const char *name, hg_wave *w)
{
    size_t len;
    uint8_t *d = hga_load(f->a, name, "WAV", &len, NULL);
    /* FUN_1003293b / FUN_10032bb2: a 22,050 Hz 8-bit mono buffer holding the whole record, header included */
    int r = d ? wave_raw8(d, len, 22050, w) : -1;
    free(d);
    return r;
}

/* FUN_1003293b: the sound looped at -15 dB (DirectSound volume -1500); the buffer holds the whole file, header
 * included, so its 44 header bytes play (a click) at every pass */
static void play_loop(front *f, int idx)
{
    if (idx == -1 || idx == f->loop_cur) return;
    audio_stop(f->loop_voice);
    f->loop_voice = 0;
    f->loop_cur = idx;
    if (idx <= 0 || idx > 6) return;
    if (!f->loop_tried[idx]) { f->loop_tried[idx] = 1; load_wave(f, sound_names[idx], &f->loop_wave[idx]); }
    f->loop_voice = audio_play(&f->loop_wave[idx], 0.1778f, 1);
}

static void voice_stop(front *f)
{
    audio_stop(f->voice);
    f->voice = 0;
    wave_free(&f->voice_wave);
}

/* FUN_10032bb2: one sound at a time on the voice channel, full volume */
static void play_voice(front *f, const char *name)
{
    voice_stop(f);
    if (!load_wave(f, name, &f->voice_wave)) f->voice = audio_play(&f->voice_wave, 1.0f, 0);
}

static void narration_stop(front *f)
{
    f->narr_on = 0;
    voice_stop(f);
}

/* FUN_10072f4e: part i starts once the parts before it have run their seconds; each plays its WAV. After the last,
 * the narration ends. (The briefing's 3D units on the map during it are not done.) */
static void narration_tick(front *f)
{
    const hg_brf *b = shell_brf(f->sh);
    if (!f->narr_on || !b) return;
    /* whole seconds since the start (clock() / 1000); the part whose start has been reached */
    int64_t el = (int64_t)((f->now - f->narr_start) / 1000), t = 0;
    int part = 0;
    while (part < b->nnarration && t < el) t += b->narration[part++].seconds;
    if (el < t) part--;
    if (part >= b->nnarration) { f->narr_on = 0; return; }    /* done; the last WAV plays out */
    if (part <= f->narr_part) return;
    f->narr_part = part;
    if (b->narration[part].wav[0]) play_voice(f, b->narration[part].wav);
}

static void squad_refresh(front *f);
static void garage_refresh(front *f);
static void garage_view(front *f);

/* what a 3D view of design g draws besides its GDF's parts, and where its root object goes:
 * - the view draws each item's own ODF models (0x1011d8bd); the GDF differs only at a grenade's record, where it has
 *   POF_HWLH (design.c fill) and the view the grenade's model (its ODF's first, as every part's);
 * - the root item (a torso or a strider) at its view place (FUN_100bec8a: 0x101b0012 / 16, and 0x101b001a x 1.5) */
static void briefing_gear(front *f);
static void view_of(const design *g, char extra[96][9], float place[3])
{
    memset(extra, 0, 96 * 9);
    for (int r = 0; r < 96; r++) {
        int it = g->rec_item[r];
        if (it < 0 || it >= g->nitem) continue;
        int id = g->item[it].id;
        if (id != 0x79 && id != 0x7a && id != 0x7b) continue;
        snprintf(extra[r], 9, "%.8s", shell_equip_code[id]);
        for (char *c = extra[r]; *c; c++) *c = (char)tolower((unsigned char)*c);
    }
    place[0] = place[1] = place[2] = 0.0f;
    for (int i = 0; i < g->nitem; i++) {
        int root = g->item[i].id;
        if (g->item[i].parent >= 0 || root < 0 || root >= SHELL_EQUIP_IDS) continue;
        place[0] = shell_equip_view[root][0];
        place[1] = shell_equip_view[root][1];
        place[2] = shell_equip_view[root][2] * 1.5f;
        break;
    }
}
static void garage_controls(front *f);
static int same_name(const char *g, const char *n);
static void garage_load(front *f);
static void garage_save(front *f);
static void run(front *f, const action *act);
static void garage_drop(front *f, int id, int x, int y);
static void fill_gears(front *f);

/* B20's accept and delete (FUN_10149529's actions 6 / 7, event 0x21, FUN_10017c1d): disabled (+0x48) while the game
 * list has no selection (FUN_10002110 -1), enabled (+0x4c) otherwise */
static void b20_controls(front *f)
{
    int none = shell_list_selected(f->sh, "B20CLIST") < 0;
    shell_disable(f->sh, "B20ACCT", none);
    shell_disable(f->sh, "B20DEL_", none);
}

/* B16's list (FUN_10147d38): campaign entries 0x23 .. 0x26 by their titles (0x101ace60 + entry x 0x7f + 0x1b), the
 * first selected on building (vtable +0x60(0)); its events 0x13 / 8 (the arrows): var 0x1a = the item's entry
 * (FUN_100181c5), the briefing of var 0x1a loaded into the summary box (FUN_100ff9d0), the narration stopped
 * (FUN_100ffab5) */
static void training_select(front *f)
{
    int i = shell_list_selected(f->sh, "B16_LIST");
    if (i < 0) return;
    f->var[0x1a] = 0x23 + i;
    if (f->narr_on) narration_stop(f);
    shell_set_mission(f->sh, f->var[0x1a]);
}

static void training_fill(front *f)
{
    const char *items[4];
    for (int i = 0; i < 4; i++) items[i] = shell_campaign[0x23 + i].title;
    shell_list_set(f->sh, "B16_LIST", items, 4, 0);
    training_select(f);
}

/* the save header's last selections (FUN_10136c8c / FUN_10136e50: the game's place in the list, kept per class: the
 * header's first word tour of duty, second multiplayer, third the story) */
static int last_slot(int cls)
{
    return cls == 0 ? 2 : cls == 1 ? 0 : 1;
}

static void set_cur(front *f, int i)
{
    f->cur = i;
    if (i >= 0 && i < f->k.count) f->k.last[last_slot(career_class(f->k.c[i].type))] = i;
}

/* leaving the main menu (FUN_101391b8's screen event 1, FUN_100eabb6 -> FUN_10136f6d): the current game becomes the
 * last one selected of the class var 0x29 names; FUN_10136e50 walks the list that many places, so -1 (none yet)
 * lands on the first game, and a place past the end leaves the current game as it is */
static void last_game(front *f)
{
    int c = f->var[0x29], i = f->k.last[last_slot(c < 0 || c > 2 ? 0 : c)];
    if (i < 0) i = 0;
    if (i < f->k.count) f->cur = i;
}

static void spinners_show(front *f)
{
    for (int k = 0; k < OPT_COUNT; k++) {
        const char *const *t;
        int n = options_items(k, &t);
        shell_set_spinner(f->sh, options_stem(k), t[f->opt.sel[k] % n]);
    }
}

/* the credits' lines (0x101c7b60: 0x78-byte lines up to one starting with '|'), joined by "\n" */
static const char *credits_text(void);

static void tour_run_gen(front *f);
static void tour_types(front *f);
static void tour_b08(front *f);

static int mp_state(int st) { return st == 0x15 || st == 0x24 || st == 0x31 || st == 0x2e || st == 0x2f || st == 0x27; }

static void enter(front *f, int state)
{
    if (state == 0x3a) state = 0x24;   /* B58 (FUN_1015e872): the session layer opened, then B36 */
    if (mp_state(state) && !f->mp)
        f->mp = mps_open(f->a, f->cur >= 0 && f->cur < f->k.count ? f->k.c[f->cur].name : "Player");
    if (f->narr_on) narration_stop(f);
    if (!is_popup(state)) play_loop(f, screen_sound(state == 0x0e ? f->var[2] : state));
    if (is_popup(state)) {
        /* drawn over the screen under it (FUN_10028f08) */
        if (!is_popup(f->state)) f->under = f->state;
        f->state = state;
        shell_popup(f->sh, screen_of(state));
        /* the builders' rectangles: B19's entry at (72, 53) in the popup at (159, 163), its text area 173 x 22,
         * 16 characters (FUN_101490b5); B72's text at (28, 20) in the popup at (192, 152), 193 x 90 (FUN_1016aae2) */
        if (state == 0x13) shell_edit_set(f->sh, 159 + 72, 163 + 53, 159 + 72 + 173, 163 + 53 + 22, 0x10, "");
        if (state == 0x48) shell_box_set(f->sh, 192 + 28, 152 + 20, 192 + 28 + 193, 152 + 20 + 90, f->str[0x13]);
        if (mp_state(state)) mps_enter(f->mp, f->sh, state);
        return;
    }
    if (state == 0x0e) {
        /* the transition (FUN_10147527): the campaign entry var 0x1a's cutscene (0x100ffd56) when there is one,
         * then the state in var 2. The story's end: entry 0x20 (ZL_CUT15) becomes 0x21 (ZL_CUT16) when the game's
         * stat 11 (0x101120af(0xb): the record's +0x24 + 11 x 4) is 0x20, var 1 is 6 (from the debriefing) and the
         * last mission's outcome (HGCAR +0x10, 0x1012e519) is 2, a success */
        int m = f->var[0x1a];
        const career_char *gc = f->cur >= 0 && f->cur < f->k.count ? &f->k.c[f->cur] : NULL;
        if (m == 0x20 && gc && gc->stat[11] == 0x20 && f->var[1] == 6 && f->success) m = 0x21;
        if (m >= 0 && m < SHELL_CAMPAIGN && shell_campaign[m].movie && shell_campaign[m].movie[0]) {
            snprintf(f->movie, sizeof f->movie, "%s", shell_campaign[m].movie);
            if (f->request == FRONT_RUN) f->request = FRONT_MOVIE;
        }
        state = f->var[2];
    }
    f->state = state;
    if (shell_goto(f->sh, screen_of(state))) { fprintf(stderr, "no screen for state 0x%x\n", state); return; }
    if (state == 0x14) { fill_games(f); b20_controls(f); }
    if (mp_state(state)) mps_enter(f->mp, f->sh, state);
    if (state == 0x01) fill_games(f);
    if (state == 0x08) {
        /* B08: the name (FUN_1000fe8f in B08_TEXT's place), the allegiance and the unit */
        /* FUN_10145504: the entry (FUN_10013137) at (72, 43), 173 x 22, 15 characters; the allegiance spinner
         * (FUN_100edc85 -> FUN_100edcdd, var 0x29 non-zero: North (1), South (0)) starts on North, the unit spinner
         * (FUN_100edd82 -> FUN_100edde3) on its faction's default */
        shell_edit_set(f->sh, 72, 43, 72 + 173, 43 + 22, 0xf, "");
        f->t_faction = 1;
        f->t_unit = tour_units(f->t_faction, NULL);
        tour_b08(f);
    }
    if (state == 0x04) {
        /* B04 on entering: var 0x16 = 0, var 0x1a = 0x22 (the random mission), the list filled (0x100fcfcf) and
         * the generator run for its first entry */
        f->var[0x16] = 0;
        f->var[0x1a] = 0x22;
        tour_types(f);
        if (f->t_ntypes > 0) { f->var[0x17] = f->t_types[0]; tour_run_gen(f); f->t_sel = 0; }
    }
    if (state == 0x10) training_fill(f);
    if (state == 0x40) spinners_show(f);
    if (state == 0x43) { f->credits_t0 = f->now; shell_set_credits(f->sh, credits_text(), 0); }
    if (state == 0x03) {
        /* the barracks (FUN_1013ae22): its controls filled from the current game on entering */
        barracks_build(f->cur >= 0 && f->cur < f->k.count ? &f->k.c[f->cur] : NULL, &f->bar);
        shell_set_barracks(f->sh, &f->bar);
    }
    if (state == 0x11) shell_set_mission(f->sh, f->var[0x1a]);   /* B17: the mission's debriefing */
    /* the debriefing's continue and abort share a place: continue after a success or outside the story, abort after
     * a story mission failed (FUN_100e2386) */
    if (state == 0x06) shell_hide(f->sh, game_class(f) != 0 || f->success ? "B06ABRT" : "B06CONT");
    if (state == 0x05 || state == 0x3e || state == 0x06) {
        /* the briefing loaded for var 0x1a (FUN_100ffc4d at B20's accept, FUN_100e7fe4 after the debriefing); the
         * random mission's is the generator's (it fills the game's briefing, 0x1012e5dc) */
        if (f->var[0x1a] == 0x22 && f->t_ok) shell_set_brf(f->sh, 0x22, &f->t_rmg.brf);
        else shell_set_mission(f->sh, f->var[0x1a]);
        /* the map's front line: the game's own outside the story (FUN_10112881) */
        shell_set_front(f->sh, f->var[0x1a] == 0x22 && f->cur >= 0 && game_class(f) == 1 ? f->k.c[f->cur].front : NULL);
    }
    /* the squad panel (0x100ef574 on entering, again only when the game or the mask changed): the wingmen */
    if (state == 0x05) front_squad_update(f);
    if (state == 0x07) {
        /* entering the gear editor (FUN_1013fa9f's actions 0x15 - 0x1c, run in turn): var 4 = 0; then, when var 5 is
         * set (B05's build sets it), string 6 = the game's gear (0x10111eb0), the selector refilled (0x1009a38b), its
         * entry of that name selected (0x1009b039; else its first), the game's gear = the selector's (0x10111e94),
         * strings 5 / 6 = its names, and the view loads string 6 (0x100f1706). The category and the list's place stay
         * from the last visit (the controls are built once); an empty design shows the torso's (garage_controls) */
        f->var[4] = 0;
        if (f->var[5]) {
            if (!f->sq_set) front_squad_update(f);
            career_char *c = f->cur >= 0 && f->cur < f->k.count ? &f->k.c[f->cur] : NULL;
            fill_gears(f);
            f->g_sel = f->pgsel;
            for (int j = 0; c && c->desc[0] && j < f->ngl; j++) if (same_name(c->desc, f->gl[j])) { f->g_sel = j; break; }
            if (c && f->ngl > 0) snprintf(c->desc, sizeof c->desc, "%s", f->gl[f->g_sel]);
            if (f->ngl > 0) f->pgsel = f->g_sel;
            garage_load(f);
        }
        garage_refresh(f);
    }
}

static void pop(front *f)
{
    if (!is_popup(f->state)) return;
    shell_popup_close(f->sh);
    int was = f->state;
    f->state = f->under;
    /* B07 back from clear's question (FUN_10016b10: var 0x2b 0 and the answer var 0x2a 1): the design is cleared
     * (0x100f19f0 -> the view's 0x100a8bbf -> 0x100c0c6a) */
    if (was == 0x48 && f->state == 0x07 && f->var[0x2b] == 0 && f->var[0x2a] == 1) {
        design_clear(&f->g_des);
        f->g_pick = -1;
        if (!design_build(&f->g_des, f->a)) garage_refresh(f);
    }
    /* B07 back from accept's question (FUN_10016b10: var 0x2b 1 and the answer var 0x2a 1): accept's chain,
     * 0x100fc12d / 0x100fb6b8 / 0x100f15cf / 0x100f0871 (the save and the game's gear) and var 1's state */
    if (was == 0x48 && f->state == 0x07 && f->var[0x2b] == 1 && f->var[0x2a] == 1) {
        static const action yes[2] = {{OP_GARAGE_KEEP}, {OP_GOTO_VAR, 1}};
        action a[3] = {yes[0], yes[1], {OP_END}};
        run(f, a);
        return;
    }
    /* B20 back from its confirmation: delete the selected game when accepted */
    if (was == 0x48 && f->state == 0x14 && f->var[0x2a] == 1) {
        int g = shell_list_selected(f->sh, "B20CLIST");
        if (g >= 0 && g < f->nlist_game) {
            int i = f->list_game[g];
            career_delete(&f->k, i);
            if (f->cur == i) f->cur = -1;
            else if (f->cur > i) f->cur--;
            save(f);
        }
        fill_games(f);
    }
}

/* B04 (FUN_1013b3e7): the list's mission types (0x100fcfcf), their names (0x10254d80); the generator runs for the
 * selected one (FUN_1010900f's actions on a change: var 0x17, 0x100fcc3f, the summary FUN_100e9290) */
static void tour_run_gen(front *f)
{
    career_char *c = f->cur >= 0 && f->cur < f->k.count ? &f->k.c[f->cur] : NULL;
    f->t_ok = 0;
    if (!c) return;
    int level = squad_tech_level(c->type, c->stat[0], c->a8c[tour_faction(c->type) == 1 ? 2 : 6], c->stat[7]);
    int alone = 0;
    f->t_ok = tour_generate(f->a, c, level, f->var[0x15], f->var[0x16], f->var[0x17], &f->t_rmg, &alone);
    if (!f->t_ok) { fprintf(stderr, "tour: the mission generator failed (type %d)\n", f->var[0x17]); return; }
    /* 0x100fcc3f's results: string var 2 the scenario, string var 3 the type's name, var 0x19 the region, stats
     * 14 / 15 the region and local_24 */
    snprintf(f->str[2], sizeof f->str[2], "%s", f->t_rmg.scenario);
    snprintf(f->str[3], sizeof f->str[3], "%s", f->t_rmg.movie >= 0 ? rmg_type_name(f->t_rmg.movie) : "");
    f->var[0x19] = f->t_rmg.region;
    c->stat[14] = f->t_rmg.region;
    c->stat[15] = alone;
}

static void tour_types(front *f)
{
    career_char *c = f->cur >= 0 && f->cur < f->k.count ? &f->k.c[f->cur] : NULL;
    const char *items[16];
    f->t_ntypes = 0;
    if (c) {
        int level = squad_tech_level(c->type, c->stat[0], c->a8c[tour_faction(c->type) == 1 ? 2 : 6], c->stat[7]);
        f->t_ntypes = tour_mission_types(c, level, f->t_types);
    }
    for (int i = 0; i < f->t_ntypes; i++) items[i] = rmg_type_name(f->t_types[i]);
    f->t_sel = -1;
    shell_list_set(f->sh, "B04_LIST", items, f->t_ntypes, 0);
}

static void tour_b08(front *f)
{
    static char line[64];
    snprintf(line, sizeof line, "%s", f->t_faction == 1 ? "North" : "South");
    const char *items[1] = {line};
    shell_list_set(f->sh, "B08ALLEG", items, 1, 0);
    const char *u[1] = {barracks_unit_name(f->t_unit)};
    shell_list_set(f->sh, "B08ULIST", u, 1, 0);
}

static void run(front *f, const action *act)
{
    for (int i = 0; i < 8 && act[i].op != OP_END; i++) {
        const action *x = &act[i];
        switch (x->op) {
        case OP_ANIM:
            shell_anim_play(f->sh, x->a, x->b);
            /* no sound: the animations' SMKs have no audio tracks (and FUN_10031e3d opens them with Smacker's sound
             * off), and nothing in the binaries names the archive's b15d1smk / b15d2smk / b15ucsmk WAVs: the shell's
             * sounds are the six AFXSHA* loops (0x101ddc50) and the briefings' voices */
            if (x->b == 3) {
                /* FUN_1003236d: the shell runs the animations until it ends, then the actions go on */
                f->pending = act + i + 1;
                f->npending = 8 - i - 1;
                return;
            }
            break;
        case OP_GOTO:
            if (is_popup(f->state)) shell_popup_close(f->sh);
            if (f->state == 0x00 && x->a != 0x00) last_game(f);
            enter(f, x->a);
            break;
        case OP_GOTO_VAR: enter(f, f->var[x->a]); break;
        case OP_SETVAR: f->var[x->a] = x->b; break;
        case OP_SETSTR: snprintf(f->str[x->a], sizeof f->str[x->a], "%s", x->s); break;
        case OP_PUSH: enter(f, x->a); break;
        case OP_POP: pop(f); break;
        case OP_QUIT: f->request = FRONT_QUIT; break;
        case OP_LAUNCH: f->request = FRONT_LAUNCH; break;
        case OP_BY_CLASS: {
            /* FUN_100e8d0a / FUN_100e7fe4: by the current game's class: tour a, story b, multiplayer c */
            int c = game_class(f), to = c == 1 ? x->a : c == 0 ? x->b : x->c;
            if (to >= 0) enter(f, to);
            break;
        }
        case OP_SELECT_GAME: {
            int g = shell_list_selected(f->sh, "B20CLIST");
            if (g >= 0 && g < f->nlist_game) set_cur(f, f->list_game[g]);
            break;
        }
        case OP_LIST_VAR: {
            int m = shell_list_selected(f->sh, "B20MLIST");
            if (m >= 0) f->var[x->a] = m + 1;
            break;
        }
        case OP_NEW_GAME: {
            const char *name = shell_edit_text(f->sh);
            if (f->var[0x29] == 0 && name[0]) {
                int i = career_new_story(&f->k, name);
                if (i >= 0) set_cur(f, i);
            }
            break;
        }
        case OP_SET_MISSION:
            f->training = 0;
            if (f->cur >= 0) {
                int m = f->var[x->a];
                if (m == 0x22) m = f->var[x->b];
                if (m > 0) f->k.c[f->cur].stat[CAREER_MISSION] = m;
            }
            break;
        case OP_MISSION_VAR:
            if (f->cur >= 0) f->var[x->a] = f->k.c[f->cur].stat[CAREER_MISSION];
            break;
        case OP_SAVE: save(f); break;
        case OP_SQUAD:
            if (x->a >= 0 && x->a < 2 && f->sq_n[x->a] > 0) {
                f->sq.pilot[x->a] = squad_step(f->sq_l[x->a], f->sq_n[x->a], f->sq.pilot[x->a], f->sq.pilot[!x->a], x->b);
                squad_refresh(f);
            }
            break;
        case OP_PILOTS_LOST:
            for (int i = 0; i < 2; i++)
                if (f->sq_lost[i] && f->sq.pilot[i] >= 0 && f->cur >= 0 && f->cur < f->k.count)
                    memset(f->k.c[f->cur].r25c + f->sq.pilot[i] * 20, 0, 4);
            f->sq_lost[0] = f->sq_lost[1] = 0;
            break;
        case OP_GEAR:
            if (x->a == 2 && f->ngl > 0) {
                /* the player's list (0x100f08c0 keeps its current gear as the game's) */
                f->pgsel = ((f->pgsel + x->b) % f->ngl + f->ngl) % f->ngl;
                if (f->cur >= 0 && f->cur < f->k.count)
                    snprintf(f->k.c[f->cur].desc, sizeof f->k.c[0].desc, "%s", f->gl[f->pgsel]);
                squad_refresh(f);
            } else if (x->a >= 0 && x->a < 2 && f->ngl > 0 && f->sq.pilot[x->a] >= 0) {
                f->gsel[x->a] = ((f->gsel[x->a] + x->b) % f->ngl + f->ngl) % f->ngl;
                if (f->cur >= 0 && f->cur < f->k.count) {
                    career_char *c = &f->k.c[f->cur];
                    snprintf(c->str[f->sq.pilot[x->a]], sizeof c->str[0], "%s", f->gl[f->gsel[x->a]]);
                }
                squad_refresh(f);
            }
            break;
        case OP_GARAGE_CAT: f->g_cat = x->a; f->g_top = 0; garage_refresh(f); break;
        case OP_GARAGE_SCROLL: f->g_top += x->a; garage_refresh(f); break;
        case OP_GARAGE_GEAR:
            if (f->ngl > 0) { f->g_sel = ((f->g_sel + x->a) % f->ngl + f->ngl) % f->ngl; garage_load(f); garage_refresh(f); }
            break;
        case OP_GARAGE_DELETE:
            if (f->g_pick >= 0 && f->g_pick < f->g_des.nitem) {
                design_remove(&f->g_des, f->g_pick);
                f->g_des.dirty = 1;
                f->g_pick = -1;
                if (!design_build(&f->g_des, f->a)) garage_refresh(f);
            }
            break;
        case OP_GARAGE_ACCEPT: {
            /* the actions 0x100f2585 test the view's top speed (0x100a7346, garage_design_stats) against 5: under,
             * var 0x2b = 1, the question (0x1023d720) in string var 0x13 and the popup 0x48; else 0x100fc12d,
             * 0x100fb6b8, 0x100f15cf (the save), 0x100f0871 (the game's gear) and var 1's state (0x10014cdf) */
            int ids[DESIGN_ITEMS], ar[DESIGN_ITEMS], sp = 0;
            unsigned op[DESIGN_ITEMS];
            for (int k = 0; k < f->g_des.nitem; k++) {
                ids[k] = f->g_des.item[k].id; ar[k] = f->g_des.item[k].armor; op[k] = (unsigned)f->g_des.item[k].options;
            }
            garage_design_stats(ids, ar, op, f->g_des.nitem, f->g_des.b30, f->g_des.b31, &sp, NULL, NULL);
            action a[4] = {{OP_END}, {OP_END}, {OP_END}, {OP_END}};
            if (sp < 5) {
                a[0] = (action){OP_SETVAR, 0x2b, 1};
                a[1] = (action){OP_SETSTR, 0x13, 0, 0, "Are you sure you want to accept this gear with such a low maximum speed?"};
                a[2] = (action){OP_PUSH, 0x48};
            } else {
                a[0] = (action){OP_GARAGE_KEEP};
                a[1] = (action){OP_GOTO_VAR, 1};
            }
            run(f, a);
            return;
        }
        case OP_GARAGE_KEEP:
            garage_save(f);
            if (f->ngl > 0) {
                f->pgsel = f->g_sel;
                if (f->cur >= 0 && f->cur < f->k.count)
                    snprintf(f->k.c[f->cur].desc, sizeof f->k.c[0].desc, "%s", f->gl[f->g_sel]);
                squad_refresh(f);
            }
            break;
        case OP_BARRACKS_EXIT:
            if (f->cur >= 0 && f->cur < f->k.count && f->bar.unit_sel >= 0 && f->bar.unit_sel < f->bar.nunit)
                f->k.c[f->cur].stat[7] = f->bar.unit[f->bar.unit_sel];
            break;
        case OP_OPTIONS:
            if (x->a == 0) { if (options_save(&f->opt, f->dir)) fprintf(stderr, "could not write the options\n"); }
            else if (x->a == 1) options_from_blocks(&f->opt);
            else {
                /* FUN_100e76b6 -> 0x100e7962: the row of 0x101c5888 for the machine's speed; a fast CPU's (200 and
                 * up: the row of 198) turns every detail on, the speakers off */
                static const int on[] = {OPT_PA, OPT_SH, OPT_ML, OPT_OT, OPT_ST, OPT_TF};
                for (size_t i = 0; i < sizeof on / sizeof on[0]; i++) f->opt.sel[on[i]] = 1;
                f->opt.sel[OPT_DS] = 0;
            }
            spinners_show(f);
            break;
        case OP_TOUR_ROUTE: {
            int c = game_class(f), fac = f->cur >= 0 && f->cur < f->k.count ? tour_faction(f->k.c[f->cur].type) : 1;
            int to = c == 0 ? x->c : c == 1 ? (fac == 1 ? x->a : x->b) : 0x15;   /* multiplayer: B21 (0x10139f9e's d / e) */
            if (f->var[0x15] == -2) f->var[0x15] = shell_list_selected(f->sh, "B01CLIST");   /* FUN_10016026 */
            if (to >= 0) enter(f, to);
            break;
        }
        case OP_TOUR_NEW: {
            const char *name = shell_edit_text(f->sh);
            if (name[0]) {
                int i = tour_new_game(&f->k, name, f->t_faction, f->t_unit);
                if (i >= 0) set_cur(f, i);
            }
            break;
        }
        case OP_TOUR_PICK:
            if (x->a == 0) { f->t_faction = !f->t_faction; f->t_unit = tour_units(f->t_faction, NULL); }
            else {
                /* the spinner's arrows (0x10012ab6): previous / next among the faction's units, round */
                int us[8], n = 0, k = 0;
                tour_units(f->t_faction, us);
                while (n < 8 && us[n] >= 0) n++;
                for (int i = 0; i < n; i++) if (us[i] == f->t_unit) k = i;
                f->t_unit = us[((k + x->b) % n + n) % n];
            }
            tour_b08(f);
            break;
        case OP_TOUR_TYPE: {
            int i = shell_list_selected(f->sh, "B04_LIST");
            if (i >= 0 && i < f->t_ntypes) f->var[0x17] = f->t_types[i];
            break;
        }
        case OP_TOUR_GEN: tour_run_gen(f); break;
        case OP_MP: {
            if (!f->mp) f->mp = mps_open(f->a, f->cur >= 0 && f->cur < f->k.count ? f->k.c[f->cur].name : "Player");
            int to = mps_action(f->mp, f->sh, x->a, x->b, shell_edit_text(f->sh));
            if (to == -2) f->request = FRONT_LAUNCH;
            else if (to >= 0) {
                if (is_popup(f->state)) shell_popup_close(f->sh);
                enter(f, to);
            }
            break;
        }
        case OP_TRAIN_SCN: {
            int m = f->var[0x1a];
            snprintf(f->str[2], sizeof f->str[2], "%s", m >= 0 && m < SHELL_CAMPAIGN ? shell_campaign[m].scenario : "");
            f->training = 1;
            break;
        }
        case OP_NARRATE:
            if (f->narr_on) narration_stop(f);
            else if (shell_brf(f->sh)) { f->narr_on = 1; f->narr_part = -1; f->narr_start = f->now; narration_tick(f); }
            break;
        }
    }
}

front *front_open(hg_archive *a, const char *game_dir)
{
    front *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    f->a = a;
    f->sh = shell_open(a);
    snprintf(f->path, sizeof f->path, "%s/CHARINFO.HG1", game_dir);
    snprintf(f->dir, sizeof f->dir, "%s", game_dir);
    options_load(&f->opt, game_dir);
    if (career_load(&f->k, f->path)) fprintf(stderr, "%s: unreadable, starting without games\n", f->path);
    f->cur = -1;
    for (int i = 0; i < f->k.count; i++) if (career_class(f->k.c[i].type) == 0) { f->cur = i; break; }
    f->var[0x1a] = 1;
    f->loop_cur = 0;
    f->g_drag = f->g_pick = f->g_root = -1;
    f->g_list_key = -1;
    enter(f, 0);
    return f;
}

void front_keys(front *f, int left, int right)
{
    f->key_left = left;
    f->key_right = right;
}

void front_tick(front *f, uint64_t ms)
{
    f->now = ms;
    if (f->mp && mp_state(f->state)) {
        int to = mps_tick(f->mp, f->sh, f->state);
        if (to == -2) f->request = FRONT_LAUNCH;
        else if (to >= 0) enter(f, to);
    }
    if (f->state == 0x43) shell_set_credits(f->sh, credits_text(), (int)((ms - f->credits_t0) / 20) * 2);
    /* the gear editor's idle actions 0xe / 0xf (FUN_1013fa9f -> FUN_100f1516, run on every pass of the shell's loop,
     * message 0x20): while Right / Left is held, the design turns 7 / -7 degrees about y (0x100bf3e0 -> 0x1011d6eb ->
     * 0x1004a555), unless an item is dragged over the view (+0x9e) or the view is empty (+0x7e). So the speed is the
     * loop's: one step a front_tick here */
    /* the briefing's gear turns 7 degrees on every pass (FUN_1013c185's idle action 0xf: FUN_100f1446 -> 0x100bf3e0) */
    if (f->state == 0x05) {
        f->b_turn += 7.0f;
        shell_briefing_gear_turn(f->sh, f->b_turn);
    }
    shell_garage_spin(f->sh, f->state == 0x07, f->g_ids, f->g_n, f->g_drag, ms);
    if (f->state == 0x07 && !f->g_mouse && f->g_des.gdf && (f->key_right || f->key_left)) {
        f->g_turn += (f->key_right ? 7.0f : 0.0f) - (f->key_left ? 7.0f : 0.0f);
        garage_view(f);
    }
    if (f->state == 0x04) {
        /* B04's selection change (FUN_1010900f): var 0x17, the generator, the summary */
        int i = shell_list_selected(f->sh, "B04_LIST");
        if (i >= 0 && i != f->t_sel && i < f->t_ntypes) {
            f->t_sel = i;
            f->var[0x17] = f->t_types[i];
            tour_run_gen(f);
            if (f->t_ok) shell_set_brf(f->sh, 0x22, &f->t_rmg.brf);
        }
    }
    narration_tick(f);
    shell_anim_tick(f->sh, ms);
    /* the actions after a mode-3 animation, once it has ended */
    if (f->pending && !shell_anim_busy(f->sh)) {
        action rest[8] = {{0}};
        for (int i = 0; i < f->npending && i < 8; i++) rest[i] = f->pending[i];
        f->pending = NULL;
        run(f, rest);
    }
}

void front_sound_pause(front *f)
{
    narration_stop(f);
    audio_stop(f->loop_voice);
    f->loop_cur = 0;
    f->loop_voice = 0;
}

net *front_net(front *f) { return f && f->mp ? mps_net(f->mp) : NULL; }

void front_mp_over(front *f, const char *results)
{
    if (!f || !f->mp) return;
    mps_over(f->mp, results);
    enter(f, 0x27);   /* B39, the multiplayer debriefing */
}

void front_close(front *f)
{
    if (!f) return;
    mps_close(f->mp);
    front_sound_pause(f);
    design_free(&f->g_des);
    for (int i = 0; i < 7; i++) wave_free(&f->loop_wave[i]);
    shell_close(f->sh);
    career_free(&f->k);
    free(f);
}

void front_mouse(front *f, int x, int y, int down)
{
    if (f->pending) return;     /* the shell plays the animation; clicks wait */
    const char *b = shell_mouse(f->sh, x, y, down);
    if (f->state == 0x14) b20_controls(f);   /* after every event (event 0x21) */
    if (f->state == 0x01) b01_info(f);       /* FUN_100ebbde: the boxes follow the list's selection */
    /* the situation screen: a click anywhere returns to the state in var 1 */
    if (f->state == 0x3e && !down && !b) { enter(f, f->var[1]); return; }
    if (f->state == 0x07) {
        /* the view's rectangle (0x101e175a, FUN_1013fa9f's FUN_100a6bb5): a drag from the list entering it is event 0xf
         * (FUN_10027bea: the mouse captured by another control), which deselects and sets +0x9e (no turning); leaving
         * or dropping, 0x11, clears it (FUN_100c0cb2) */
        int in_view = x >= GARAGE_VX0 && x <= GARAGE_VX1 && y >= GARAGE_VY0 && y <= GARAGE_VY1;
        int over = f->g_drag >= 0 && in_view && down;
        if (over && !f->g_mouse) { f->g_pick = -1; garage_controls(f); }
        f->g_mouse = over;
    }
    if (f->state == 0x07 && !b) {
        /* the gear editor: an item dragged from the list (FUN_100b2dac) and dropped on the model (FUN_100c515d) */
        int in_view = x >= GARAGE_VX0 && x <= GARAGE_VX1 && y >= GARAGE_VY0 && y <= GARAGE_VY1;
        if (down && f->g_drag < 0) {
            int i = shell_garage_pick(f->sh, x, y);
            if (i >= 0 && f->g_top + i < f->g_n) f->g_drag = f->g_ids[f->g_top + i];
            else if (in_view) {
                /* a part of the model clicked (event 8, FUN_100c0cb2): its item is selected */
                int r = shell_garage_model_pick(f->sh, x, y);
                f->g_pick = r >= 0 && r < 96 ? f->g_des.rec_item[r] : -1;
                garage_controls(f);
            }
        } else if (!down && f->g_drag >= 0) {
            int id = f->g_drag;
            f->g_drag = -1;
            if (in_view) garage_drop(f, id, x, y);
        }
        return;
    }
    if (f->state == 0x07 && !down) f->g_drag = -1;
    if (!b) return;
    char stem[16];
    snprintf(stem, sizeof stem, "%s", b);
    if (f->state == 0x14) {
        /* B20: the game list's arrows show the selected game's missions */
        if (!strcmp(stem, "B20_UP_") || !strcmp(stem, "B20DOWN")) fill_missions(f);
    }
    if (f->state == 0x10 && (!strcmp(stem, "B16_UP_") || !strcmp(stem, "B16_DN_"))) training_select(f);
    if (f->state == 0x40) {
        /* the spinners' arrows (0x10012ab6: previous / next, round) */
        for (int k = 0; k < OPT_COUNT; k++) {
            char l[16], r[16];
            snprintf(l, sizeof l, "%sLT", options_stem(k));
            snprintf(r, sizeof r, "%sRT", options_stem(k));
            if (strcmp(stem, l) && strcmp(stem, r)) continue;
            const char *const *t;
            int n = options_items(k, &t), d = !strcmp(stem, l) ? -1 : 1;
            f->opt.sel[k] = ((f->opt.sel[k] + d) % n + n) % n;
            spinners_show(f);
        }
    }
    if (f->state == 0x03 && f->bar.nunit > 0 && (!strcmp(stem, "B03PREV") || !strcmp(stem, "B03NEXT"))) {
        /* the unit list's arrows (0x10012ab6: previous / next, round) */
        int n = f->bar.nunit, d = !strcmp(stem, "B03PREV") ? -1 : 1;
        f->bar.unit_sel = ((f->bar.unit_sel + d) % n + n) % n;
        shell_set_barracks(f->sh, &f->bar);
    }
    for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; i++)
        if (buttons[i].state == f->state && !strcmp(buttons[i].stem, stem)) { run(f, buttons[i].act); return; }
}

void front_key(front *f, int vk)
{
    if (vk == 0x1b) {
        /* Esc (the shell's loop, 0x101735b0 -> the table at 0x10173bbb): states 8, 0x13, 0x40 .. 0x43 ignore it,
         * the options popup (0x3f) goes back to var 3, any other state opens it (var 3 = the state) */
        int st = f->state;
        if (st == 0x3f) { pop(f); return; }
        if (st == 8 || st == 0x13 || (st >= 0x40 && st <= 0x43) || is_popup(st) || f->pending) return;
        f->var[3] = st;
        enter(f, 0x3f);
        return;
    }
    if (vk == 0x0d && f->mp && (f->state == 0x24 || f->state == 0x2e || f->state == 0x2f)) {   /* chat: the entry sent */
        mps_action(f->mp, f->sh, 14, 0, shell_edit_text(f->sh));
        return;
    }
    shell_key(f->sh, vk);
    if (f->state == 0x14) b20_controls(f);
}

void front_char(front *f, int ch)
{
    shell_char(f->sh, ch);
}

int front_poll(front *f)
{
    int r = f->request;
    f->request = FRONT_RUN;
    if (r == FRONT_LAUNCH && f->mp && mps_net(f->mp)) {   /* a multiplayer game: the generated mission */
        snprintf(f->scenario, sizeof f->scenario, "%.15s", mps_scenario(f->mp));
        return r;
    }
    if (r == FRONT_LAUNCH && f->training) {
        snprintf(f->scenario, sizeof f->scenario, "%.15s", f->str[2][0] ? f->str[2] : "M33_SCN1");
        return r;
    }
    if (r == FRONT_LAUNCH && f->var[0x1a] == 0x22 && game_class(f) == 1 && f->t_ok) {
        snprintf(f->scenario, sizeof f->scenario, "%.15s", f->t_rmg.scenario);   /* string var 2 */
        return r;
    }
    if (r == FRONT_LAUNCH) {
        /* the scenario of the game's mission (stat 12), or of var 0x1a without a game */
        int m = f->cur >= 0 ? f->k.c[f->cur].stat[CAREER_MISSION] : f->var[0x1a];
        const char *s = m >= 0 && m < SHELL_CAMPAIGN && shell_campaign[m].scenario[0] ? shell_campaign[m].scenario : "M01_SCN1";
        snprintf(f->scenario, sizeof f->scenario, "%s", s);
    }
    return r;
}

static int same_name(const char *g, const char *n)
{
    size_t i = 0;
    for (; n[i]; i++) if (tolower((unsigned char)g[i]) != tolower((unsigned char)n[i])) return 0;
    return g[i] == 0 || g[i] == '.';
}

/* a gear list name as the archive loader takes it: lower case, without ".GDF" */
static void gear_base(char *out, size_t n, const char *g)
{
    size_t k = 0;
    for (; g[k] && g[k] != '.' && k + 1 < n; k++) out[k] = (char)tolower((unsigned char)g[k]);
    out[k] = 0;
}

static void squad_refresh(front *f)
{
    career_char *c = f->cur >= 0 && f->cur < f->k.count ? &f->k.c[f->cur] : NULL;
    int north = f->sq_north;
    for (int i = 0; i < 2; i++) {
        int k = f->sq.pilot[i];
        f->sq.gear[i][0] = 0;
        if (k < 0 || f->ngl <= 0) continue;
        /* 0x100efa6f: the pilot's gear (the game record's string k) selects the list's entry; none: the list's
         * current entry (fill_gears: its last, or its first when the folder has GDFs) becomes his */
        const char *g = c ? c->str[k] : "";
        int at = -1;
        for (int j = 0; j < f->ngl && g[0]; j++)
            if (same_name(g, f->gl[j])) at = j;
        if (at >= 0) f->gsel[i] = at;
        else if (c) snprintf(c->str[k], sizeof c->str[k], "%s", f->gl[f->gsel[i]]);
        gear_base(f->sq.gear[i], sizeof f->sq.gear[i], f->gl[f->gsel[i]]);
    }
    squad_voices(&f->sq, north);
    /* the player's: the game's gear selects the list's entry, else the current one becomes the game's */
    if (f->ngl > 0) {
        int at = -1;
        for (int j = 0; c && j < f->ngl && c->desc[0]; j++) if (same_name(c->desc, f->gl[j])) at = j;
        if (at >= 0) f->pgsel = at;
        else if (c) snprintf(c->desc, sizeof c->desc, "%s", f->gl[f->pgsel]);
        gear_base(f->pgear, sizeof f->pgear, f->gl[f->pgsel]);
        if (strcmp(f->b_gear, f->pgear)) { snprintf(f->b_gear, sizeof f->b_gear, "%s", f->pgear); briefing_gear(f); }
    }
    shell_briefing_gear_dark(f->sh, f->b_dark);
    shell_disable(f->sh, "B05LNCH", f->ngl <= 0 || f->b_nolaunch);
    const char *gp = f->ngl > 0 ? f->gl_disp[f->pgsel] : "";
    shell_set_squad(f->sh, squad_pilot_name(north, f->sq.pilot[0]), squad_portrait(north, 0, f->sq.pilot[0]),
                    f->sq.pilot[0] >= 0 && f->ngl ? f->gl_disp[f->gsel[0]] : "",
                    squad_pilot_name(north, f->sq.pilot[1]), squad_portrait(north, 1, f->sq.pilot[1]),
                    f->sq.pilot[1] >= 0 && f->ngl ? f->gl_disp[f->gsel[1]] : "", gp);
}

/* the gear list (0x1009a38b): the stock gears (0x101c56f0), then the game folder's own GDFs ("*.GDF" but USERGEAR.GDF
 * and the stock ones' files), each when it is a gear of the side (+0xb8) with its extra data (+0xd0) and its tech
 * within the game's level. The folder's are kept by their file name, in findfirst's order (the file system's: by
 * name, upper-cased, on NTFS; the port sorts so) */
static int add_gear(front *f, const char *name, const uint8_t *d, size_t len, int level)
{
    int side, t = -1;
    char nm[32];
    if (d) t = squad_gear_info(d, len, &side, nm, sizeof nm);
    if (t < 0 || side != (f->sq_north ? 0 : 1) || t > level || f->ngl >= 64) return 0;
    snprintf(f->gl[f->ngl], sizeof f->gl[0], "%s", name);
    snprintf(f->gl_disp[f->ngl], sizeof f->gl_disp[0], "%s", nm);
    f->ngl++;
    return 1;
}

static int by_name(const void *a, const void *b)
{
    const char *x = *(const char *const *)a, *y = *(const char *const *)b;
    for (; *x && toupper((unsigned char)*x) == toupper((unsigned char)*y); x++, y++) {}
    return toupper((unsigned char)*x) - toupper((unsigned char)*y);
}

static void fill_gears(front *f)
{
    career_char *c = f->cur >= 0 && f->cur < f->k.count ? &f->k.c[f->cur] : NULL;
    int level = c ? squad_tech_level(c->type, c->stat[0], c->a8c[f->sq_north ? 2 : 6], c->stat[7]) : 0;
    f->ngl = 0;
    for (int i = 0; squad_stock_gear(i); i++) {
        size_t len;
        uint8_t *d = hga_load(f->a, squad_stock_gear(i), "GDF", &len, NULL);
        add_gear(f, squad_stock_gear(i), d, len, level);
        free(d);
    }
    char *names[256];
    int nn = 0, any = 0;
    DIR *dir = opendir(hga_dir(f->a));
    for (struct dirent *e; dir && (e = readdir(dir)) && nn < 256;) {
        size_t m = strlen(e->d_name);
        if (m < 5 || m >= 32 || !same_name(".gdf", e->d_name + m - 4)) continue;
        any = 1;
        if (same_name("USERGEAR.GDF", e->d_name)) continue;
        int stock = 0;
        for (int i = 0; squad_stock_gear(i); i++) {
            char sf[16];
            snprintf(sf, sizeof sf, "%s.GDF", squad_stock_gear(i));
            if (same_name(sf, e->d_name)) stock = 1;
        }
        if (!stock && (names[nn] = malloc(m + 1))) memcpy(names[nn++], e->d_name, m + 1);
    }
    if (dir) closedir(dir);
    qsort(names, (size_t)nn, sizeof *names, by_name);
    for (int i = 0; i < nn; i++) {
        char path[1100];
        size_t len;
        fs_join(path, sizeof path, hga_dir(f->a), names[i]);
        uint8_t *d = fs_read_all(path, &len);
        add_gear(f, names[i], d, len, level);
        free(d);
        free(names[i]);
    }
    /* the list's current entry: each item is added after the current one and becomes it (0x10026927), and only when
     * findfirst found a "*.GDF" (any, USERGEAR.GDF and the stock names too) is the list rewound to its first
     * (0x10026dd8): so the last stock gear when the folder has no GDF, else the first */
    f->gsel[0] = f->gsel[1] = f->pgsel = any || f->ngl <= 0 ? 0 : f->ngl - 1;
}

/* the gear editor's screen: the design in the selector (its parts from its GDF), the category's items */
static void garage_load(front *f)
{
    design_free(&f->g_des);
    f->g_drag = -1;
    f->g_pick = -1;
    f->g_root = -1;
    if (f->ngl <= 0) return;
    char b[32];
    gear_base(b, sizeof b, f->gl[f->g_sel]);
    /* 0x100c0bf3 -> 0x100c0a61: the design rebuilt from the GDF's trailing data */
    if (design_load(&f->g_des, f->a, b) || design_build(&f->g_des, f->a)) design_free(&f->g_des);
}

/* accept: a changed design is written to the game folder (0x100fb719's name) and becomes the selector's */
static void garage_save(front *f)
{
    if (!f->g_des.dirty || f->ngl <= 0) return;
    char file[64];
    design_save_name(hga_dir(f->a), f->gl[f->g_sel], f->gl_disp[f->g_sel], file, sizeof file);
    if (design_write(&f->g_des, hga_dir(f->a), file)) { fprintf(stderr, "could not write %s\n", file); return; }
    f->g_des.dirty = 0;
    fill_gears(f);
    for (int j = 0; j < f->ngl; j++) if (same_name(f->gl[j], file) || same_name(file, f->gl[j])) f->g_sel = j;
}

/* an item dropped on the model at (x, y): put on the design where it fits nearest (src/design.h) */
static void garage_drop(front *f, int id, int x, int y)
{
    if (!f->g_des.nitem && garage_type(id) != 0) return;
    static float pos[96][2];
    for (int r = 0; r < 96; r++)
        if (shell_garage_screen(f->sh, r, &pos[r][0], &pos[r][1])) { pos[r][0] = 1e9f; pos[r][1] = 1e9f; }
    if (!design_drop(&f->g_des, f->a, id, (float)x, (float)y, (const float (*)[2])pos) && !design_build(&f->g_des, f->a)) {
        if (garage_type(id) == 0) f->g_root = -1;   /* a new design: a new root object (0x1011b5fd turns it 180) */
        garage_refresh(f);
    }
}

/* the editor's buttons by the design (FUN_1013fa9f's actions 9 - 0xb and 0x1e, run on entering and after every event,
 * FUN_10027e23): details (0x101089a7) with a piece selected on a GEAR.VDF design, delete piece (0x101088de) with a
 * piece selected, clear (0x10108b46) with a design; the categories (0x100f2d9d): an empty design has only the torso's
 * (its button clicked: the torso list), a strider's (MAMMOTH / NAGA.VDF) no engine, head, arms or legs */
static void garage_controls(front *f)
{
    static const char *const parts[4] = {"B07ENGN", "B07HEAD", "B07ARMS", "B07LEGS"};
    static const char *const weapons[6] = {"B07RFLE", "B07ROCK", "B07GREN", "B07MISS", "B07ZOOK", "B07ENRG"};
    int empty = !f->g_des.nitem, model = f->g_des.model;
    if (empty && f->g_cat != 0) { f->g_cat = 0; f->g_top = 0; }
    for (int i = 0; i < 4; i++) shell_disable(f->sh, parts[i], empty || model != 1);
    for (int i = 0; i < 6; i++) shell_disable(f->sh, weapons[i], empty);
    int sel = f->g_pick >= 0 && f->g_pick < f->g_des.nitem;
    shell_disable(f->sh, "B07DETL", !(sel && model == 1));
    shell_disable(f->sh, "B07DEL_", !sel);
    shell_disable(f->sh, "B07CLR_", empty);
    career_char *c = f->cur >= 0 && f->cur < f->k.count ? &f->k.c[f->cur] : NULL;
    garage_panel(f->sh, &f->g_des, sel ? f->g_pick : -1,
                 c ? squad_tech_level(c->type, c->stat[0], c->a8c[f->sq_north ? 2 : 6], c->stat[7]) : 0);
}

/* the view's root object: the design's root item (a torso or a strider), placed at its view place (FUN_100bec8a:
 * 0x101b0012 / 16, and 0x101b001a x 1.5) and turned 180 degrees when made (0x1011b5fd) */
static void garage_view(front *f)
{
    int root = -1;
    for (int i = 0; i < f->g_des.nitem; i++) if (f->g_des.item[i].parent < 0) { root = f->g_des.item[i].id; break; }
    if (root != f->g_root) { f->g_root = root; f->g_turn = 180.0f; }
    static char extra[96][9];
    float place[3];
    view_of(&f->g_des, extra, place);
    shell_garage_view(f->sh, place, f->g_turn);
}

/* the briefing's view of the player's gear (B05's view, FUN_1008f28f: its +0x4c 0x1008f81d loads the GDF and rebuilds
 * its design, 0x100c0a61), and its weapons: the equipment records of types 5..10 in order (FUN_1008f502) */
static void briefing_gear(front *f)
{
    design pd;
    memset(&pd, 0, sizeof pd);
    if (!f->pgear[0] || design_load(&pd, f->a, f->pgear) || design_build(&pd, f->a)) {
        design_free(&pd);
        shell_set_briefing_gear(f->sh, NULL, 0, NULL, NULL, NULL, 0);
        f->b_dark = 0;
        f->b_nolaunch = f->ngl <= 0;   /* 0x100fcb39 */
        return;
    }
    static char extra[96][9];
    float place[3];
    view_of(&pd, extra, place);
    const char *weap[16];
    int nw = 0;
    size_t np = pd.len >= 0x88 ? (size_t)(pd.gdf[0x84] | pd.gdf[0x85] << 8 | pd.gdf[0x86] << 16 | (unsigned)pd.gdf[0x87] << 24) : 0;
    size_t o = 0x88 + np * 0x438;
    if (o + 0x3c <= pd.len) {
        const uint8_t *gb = pd.gdf + o;
        int neq = gb[0x24] | gb[0x25] << 8 | gb[0x26] << 16 | gb[0x27] << 24;
        for (int k = 0; k < neq && nw < 16 && o + 0x3c + (size_t)(k + 1) * 0x6c <= pd.len; k++) {
            const uint8_t *r = gb + 0x3c + (size_t)k * 0x6c;
            int id = r[4] | r[5] << 8 | r[6] << 16 | r[7] << 24;
            if (id >= 0 && id < SHELL_EQUIP_IDS && shell_equip_type[id] >= 5 && shell_equip_type[id] <= 10)
                weap[nw++] = shell_equip_short[id];
        }
    }
    shell_set_briefing_gear(f->sh, pd.gdf, pd.len, (const char (*)[9])extra, place, weap, nw);
    /* the view's tech (+0xb8, 0x1005bb6d at its load 0x1008f81d) over the game's level (+0xbc, FUN_1008f4c3): drawn
     * dark under the priority message (FUN_1008f502); B05's action 0x100fcb39 enables the launch only while the
     * player's list's entry is within the level (0x1009abd5: -1 with none) */
    {
        career_char *c = f->cur >= 0 && f->cur < f->k.count ? &f->k.c[f->cur] : NULL;
        int level = c ? squad_tech_level(c->type, c->stat[0], c->a8c[f->sq_north ? 2 : 6], c->stat[7]) : 0, side;
        int tech = squad_gear_info(pd.gdf, pd.len, &side, NULL, 0);
        f->b_dark = tech > level;
        f->b_nolaunch = f->ngl <= 0 || tech < 0 || tech > level;
    }
    f->b_turn = 180.0f;
    design_free(&pd);
}

/* the gear editor's screen: the design in the selector, the category's items */
static void garage_refresh(front *f)
{
    static const char *const cat_stem[GARAGE_CATS] = {"B07TRSO", "B07ENGN", "B07HEAD", "B07ARMS", "B07LEGS", "B07RFLE",
                                                      "B07ROCK", "B07GREN", "B07MISS", "B07ZOOK", "B07ENRG"};
    garage_controls(f);
    /* the list is refilled, its place reset, when the design's skeleton (+0x6e) or the side changes (0x100f2d9d) */
    int key = (f->g_des.model << 1) | (f->sq_north ? 1 : 0);
    if (key != f->g_list_key) { f->g_list_key = key; f->g_top = 0; }
    career_char *c = f->cur >= 0 && f->cur < f->k.count ? &f->k.c[f->cur] : NULL;
    int level = c ? squad_tech_level(c->type, c->stat[0], c->a8c[f->sq_north ? 2 : 6], c->stat[7]) : 0;
    int n = garage_list(f->g_cat, f->sq_north, f->g_des.model ? f->g_des.model : 1, level, f->g_ids, 128);
    f->g_n = n;
    if (f->g_top > n - 1) f->g_top = n - 1;
    if (f->g_top < 0) f->g_top = 0;
    static char texts[128][256];
    const char *items[128];
    for (int i = 0; i < n; i++) { garage_item_text(f->g_ids[i], texts[i], sizeof texts[i]); items[i] = texts[i]; }
    static char extra[96][9];
    float place[3];
    view_of(&f->g_des, extra, place);
    shell_set_garage(f->sh, cat_stem[f->g_cat], items + f->g_top, n - f->g_top, f->ngl > 0 ? f->gl_disp[f->g_sel] : "",
                     f->g_des.gdf, f->g_des.len, (const char (*)[9])extra);
    shell_garage_ids(f->sh, f->g_ids + f->g_top, n - f->g_top);
    garage_view(f);
}

void front_squad_lost(front *f, int first, int second)
{
    f->sq_lost[0] = first != 0;
    f->sq_lost[1] = second != 0;
}

void front_squad_update(front *f)
{
    const hg_brf *b = shell_brf(f->sh);
    int alive[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, north = 1;   /* no game: every pilot */
    career_char *c = f->cur >= 0 && f->cur < f->k.count ? &f->k.c[f->cur] : NULL;
    if (c) {
        for (int k = 0; k < 10; k++) alive[k] = c->r25c[k * 20] | c->r25c[k * 20 + 1] | c->r25c[k * 20 + 2] | c->r25c[k * 20 + 3];
        north = !(c->type == 1 || c->type == 4);   /* FUN_10111766 */
    }
    f->sq_north = north;
    int mask = b ? b->squad : 0;
    /* 0x100ef574 refills the lists only when the game or the mask differs from the last time */
    if (!f->sq_set || f->sq_cur != f->cur || f->sq_mask != mask) {
        f->sq_set = 1; f->sq_cur = f->cur; f->sq_mask = mask;
        squad_lists(mask, alive, f->sq_l[0], &f->sq_n[0], f->sq_l[1], &f->sq_n[1]);
        squad_default(&f->sq, mask, alive, north);
        fill_gears(f);
    }
    squad_refresh(f);
}

const char *front_player_gear(const front *f)
{
    if (f->training) return NULL;   /* the mission's own player record */
    return f->pgear;
}

const squad *front_squad(const front *f)
{
    /* training: no USERSTAR is written (the scenarios don't include it), so no wingmen */
    static const squad none = {{SQUAD_NONE1, SQUAD_NONE2}, {0, 0}, {"", ""}};
    if (f->training) return &none;
    return &f->sq;
}

const char *front_movie(const front *f)
{
    return f->movie;
}

const char *front_scenario(const front *f)
{
    return f->scenario;
}

void front_mission_over(front *f, int success)
{
    shell_set_outcome(f->sh, success ? 2 : 1);
    f->success = success;
    /* the career's update (0x100df8e4) is B06's and B39's action (FUN_100df883), not B17's: training changes nothing */
    if (!f->training && f->cur >= 0 && game_class(f) == 0) {
        /* HGSHELL 0x100df8e4 (the debriefing): stat 11 = stat 12; on the story's success (HGCAR +0x10 == 2) the
         * reached mission (stat 10) goes up when it was the one played and below 0x20, then the current (stat 12)
         * goes up while it is below the reached; then the score (src/debrief.h) and the save (0x101373bf) */
        career_char *c = &f->k.c[f->cur];
        int reached = c->stat[CAREER_REACHED];
        c->stat[11] = c->stat[CAREER_MISSION];
        int first = reached == c->stat[11];
        if (success) {
            if (first && reached < 0x20) c->stat[CAREER_REACHED] = reached + 1;
            if (c->stat[CAREER_MISSION] < c->stat[CAREER_REACHED]) c->stat[CAREER_MISSION]++;
        }
        int r10 = c->stat[CAREER_REACHED];
        c->stat[CAREER_REACHED] = reached;   /* the scoring reads bVar7, taken before the move */
        debrief_list l;
        debrief_score(c, 0, success ? 2 : 1, &f->result, &l);
        c->stat[CAREER_REACHED] = r10;
        shell_set_score(f->sh, &l);
        save(f);
    } else if (!f->training && f->cur >= 0 && game_class(f) == 1) {
        /* the tour of duty's case of 0x100df8e4: stat 11 = stat 12 (the mission type played), then its scoring */
        career_char *c = &f->k.c[f->cur];
        c->stat[11] = c->stat[CAREER_MISSION];
        debrief_list l;
        debrief_score(c, 1, success ? 2 : 1, &f->result, &l);
        shell_set_score(f->sh, &l);
        save(f);
    } else {
        debrief_list l;
        debrief_score(NULL, game_class(f), success ? 2 : 1, &f->result, &l);
        shell_set_score(f->sh, &l);
    }
    enter(f, f->var[0] ? f->var[0] : 0x05);
}

void front_draw(front *f, uint32_t *out)
{
    shell_draw(f->sh, out);
}

void front_state(front *f, int state)
{
    enter(f, state);
}

const char *front_screen(const front *f)
{
    return shell_popup_name(f->sh) ? shell_popup_name(f->sh) : shell_screen(f->sh);
}

void front_mission_stats(front *f, const char *text)
{
    shell_set_stats(f->sh, text);
}

void front_mission_result(front *f, const debrief_in *in)
{
    if (in) f->result = *in;
    else memset(&f->result, 0, sizeof f->result);
}

void front_mission_objectives(front *f, const char *text)
{
    shell_set_objectives(f->sh, text);
}

const options *front_options(const front *f)
{
    return &f->opt;
}

#include "credits.h"
static const char *credits_text(void)
{
    return hg_credits;
}
