#include "mpscreens.h"
#include "randmsn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the lobby's own messages, before the sim takes the session over (the port's; ANET's lobby traffic isn't traced):
 * "CH" + text (chat), "MS" + type u8 + seed u32 (the host's mission), "RD" (a joiner is ready) */
#define MS_TYPES 11

struct mps {
    hg_archive *a;
    char player[17];
    net_find *find;
    net_session ses[NET_MAX_SESSIONS];
    int nses, sel;
    net *n;
    char game[33];
    int count;                 /* B49's player count */
    int type_i;                /* B46's mission type (index into types[]) */
    int ms_type;               /* the mission the host sent */
    unsigned ms_seed;
    int have_ms, ready, launched;
    int ready_ids[NET_MAX_PLAYERS];
    char chat[8][96];
    int nchat;
    char scenario[16];
    char results[1024];
};

/* the multiplayer mission types: the generator's type list (0 .. 9) and free for all (0xd, M99MSCN1) */
static const int types[MS_TYPES] = {0xd, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

mps *mps_open(hg_archive *a, const char *player)
{
    mps *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->a = a;
    snprintf(m->player, sizeof m->player, "%s", player && player[0] ? player : "Player");
    m->count = 4;
    m->sel = 0;
    return m;
}

static void leave(mps *m)
{
    if (m->n) net_close(m->n);
    m->n = NULL;
    m->have_ms = m->ready = m->launched = 0;
    memset(m->ready_ids, 0, sizeof m->ready_ids);
    m->nchat = 0;
}

void mps_close(mps *m)
{
    if (!m) return;
    leave(m);
    net_find_close(m->find);
    free(m);
}

static void chat_add(mps *m, const char *line)
{
    if (m->nchat == 8) { memmove(m->chat[0], m->chat[1], sizeof m->chat[0] * 7); m->nchat = 7; }
    snprintf(m->chat[m->nchat++], sizeof m->chat[0], "%s", line);
}

static const char *type_name(int t)
{
    const char *s = rmg_type_name(t);
    return s && s[0] ? s : t == 0xd ? "Free For All" : "Mission";
}

/* the players by name, the ready ones marked: mine = 1 the local player (allies), 0 the others (enemies: each player
 * his own team, the port's free for all) */
static void roster_text(const mps *m, char *out, size_t n, int mine)
{
    net_player pl[NET_MAX_PLAYERS];
    int k = m->n ? net_players(m->n, pl) : 0;
    out[0] = 0;
    for (int i = 0; i < k; i++) {
        if ((pl[i].id == net_me(m->n)) != mine) continue;
        int rd = 0;
        for (int j = 0; j < NET_MAX_PLAYERS; j++) if (m->ready_ids[j] == pl[i].id) rd = 1;
        size_t l = strlen(out);
        snprintf(out + l, n - l, "%s%s%s\n", pl[i].name, pl[i].id == 1 ? " (host)" : "", rd ? " - ready" : "");
    }
}

static void chat_text(const mps *m, char *out, size_t n)
{
    out[0] = 0;
    for (int i = 0; i < m->nchat; i++) { size_t l = strlen(out); snprintf(out + l, n - l, "%s\n", m->chat[i]); }
}

static void show(mps *m, shell *sh, int state)
{
    char t[1024];
    switch (state) {
    case 0x24:   /* B36: SLIST's inner box (1, 3)-(138, 236) at (31, 50), PLAYR (194, 53), CHATB's CLIST at (65, 357) */
        t[0] = 0;
        for (int i = 0; i < m->nses; i++) {
            size_t l = strlen(t);
            snprintf(t + l, sizeof t - l, "%s%s %d/%d\n", i == m->sel ? ">" : " ", m->ses[i].name, m->ses[i].players,
                     m->ses[i].max);
        }
        if (!m->nses) snprintf(t, sizeof t, "No games found");
        shell_area_set(sh, 0, 32, 53, 169, 286, 0xfe, t);
        if (m->nses && m->sel < m->nses)
            snprintf(t, sizeof t, "%s\n%s:%d", m->ses[m->sel].name, m->ses[m->sel].host, m->ses[m->sel].port);
        else snprintf(t, sizeof t, "%s", m->player);
        shell_area_set(sh, 1, 194, 53, 340, 287, 0xfe, t);
        chat_text(m, t, sizeof t);
        shell_area_set(sh, 2, 65, 357, 495, 440, 0xfe, t);
        break;
    case 0x31:   /* B49 at LOCXY (192, 116): COUNT's inner (37, 1)-(145, 22) at (33, 149) */
        snprintf(t, sizeof t, "%d players", m->count);
        shell_area_set(sh, 0, 192 + 33 + 37, 116 + 149 + 3, 192 + 33 + 145, 116 + 149 + 22, 0xfe, t);
        break;
    case 0x2e: case 0x2f: {   /* B46 / B47: ENEMY (29, 40), FREND (292 / 269, 40), MISSN (144, 178), _DESC (33, 207) */
        roster_text(m, t, sizeof t, 1);
        shell_area_set(sh, 0, state == 0x2e ? 292 : 269, 40, 606, 142, 0xfe, t);
        roster_text(m, t, sizeof t, 0);
        shell_area_set(sh, 1, 29, 40, 223, 142, 0xfe, t);
        int ty = state == 0x2e ? types[m->type_i] : m->have_ms ? m->ms_type : -1;
        shell_area_set(sh, 2, 144 + 4, 178 + 2, 325, 202, 0xfe, ty >= 0 ? type_name(ty) : "Waiting for the host");
        snprintf(t, sizeof t, "Game: %s\nPlayers: %d\n%s", net_session_name(m->n), net_players(m->n, NULL),
                 state == 0x2e ? "Launch when every player is ready." : m->ready ? "Ready." : "Press Ready.");
        shell_area_set(sh, 3, 33 + 4, 207 + 4, 435, 332, 0xfe, t);
        chat_text(m, t, sizeof t);
        shell_area_set(sh, 4, 65 + 1, 357 + 1, 65 + 430, 357 + 84, 0xfe, t);
        break;
    }
    case 0x27:   /* B39: STTUS (56, 47), AFTER (360, 53) */
        shell_area_set(sh, 0, 56 + 4, 47 + 2, 293, 68, 0xfe, "Mission Complete");
        shell_area_set(sh, 1, 360 + 4, 53 + 4, 612, 416, 0xfe, m->results);
        break;
    }
}

void mps_enter(mps *m, shell *sh, int state)
{
    if (!m) return;
    if (state == 0x15) {   /* B21's list: the port's one connection */
        /* the popup at MPLOC (170, 98): _LIST (31, 33), its selection bar SELCT (0, 47)-(198, 78) on the first item */
        shell_area_set(sh, 0, 170 + 31 + 6, 98 + 33 + 47 + 8, 170 + 31 + 198, 98 + 33 + 78, 0xfe, "LAN (UDP/IP)");
        return;
    }
    if (state == 0x24) {
        leave(m);
        if (!m->find) m->find = net_find_open(NET_DEFAULT_PORT);
    }
    if (state == 0x31) {
        char nm[40];
        snprintf(nm, sizeof nm, "%.15s's game", m->player);
        shell_edit_set(sh, 192 + 33 + 2, 116 + 42, 192 + 218, 116 + 60, 0x10, nm);
    }
    if (state == 0x24) shell_edit_set(sh, 65 + 1, 357 + 86, 65 + 430, 357 + 109, 60, "");
    if (state == 0x2e || state == 0x2f) shell_edit_set(sh, 65 + 1, 357 + 86, 65 + 430, 357 + 109, 60, "");
    show(m, sh, state);
}

static int generate(mps *m, int type, unsigned seed)
{
    rmg r;
    memset(&r, 0, sizeof r);
    r.mode = 2;          /* 0x1006540f: +4 = 2, the 'M' templates */
    r.type = type;       /* +0x18 */
    r.side = 0;          /* +0x1c */
    r.p5 = 0;            /* +0x6c */
    r.level = 0;         /* +0xa8 */
    r.seed = seed;
    if (!rmg_generate(&r, m->a)) return 0;
    snprintf(m->scenario, sizeof m->scenario, "%s", r.scenario);
    return 1;
}

int mps_tick(mps *m, shell *sh, int state)
{
    if (!m) return -1;
    if (state == 0x24 && m->find) {
        net_find_poll(m->find);
        m->nses = net_find_list(m->find, m->ses, NET_MAX_SESSIONS);
        if (m->sel >= m->nses) m->sel = m->nses ? m->nses - 1 : 0;
        show(m, sh, state);
        return -1;
    }
    if ((state != 0x2e && state != 0x2f) || !m->n || m->launched) return -1;
    net_poll(m->n);
    if (net_started(m->n) < 0) { chat_add(m, "The host has left."); leave(m); return 0x24; }
    unsigned char b[NET_MAX_MSG];
    int from, l;
    while ((l = net_recv(m->n, &from, b, sizeof b - 1)) >= 0) {
        if (l >= 2 && b[0] == 'C' && b[1] == 'H') {
            b[l] = 0;
            chat_add(m, (const char *)b + 2);
        } else if (l >= 7 && b[0] == 'M' && b[1] == 'S') {
            m->ms_type = b[2];
            m->ms_seed = (unsigned)b[3] | (unsigned)b[4] << 8 | (unsigned)b[5] << 16 | (unsigned)b[6] << 24;
            m->have_ms = 1;
        } else if (l >= 2 && b[0] == 'R' && b[1] == 'D') {
            for (int j = 0; j < NET_MAX_PLAYERS; j++) if (!m->ready_ids[j] || m->ready_ids[j] == from) { m->ready_ids[j] = from; break; }
        }
    }
    show(m, sh, state);
    if (state == 0x2f && net_started(m->n) > 0 && m->have_ms) {
        if (!generate(m, m->ms_type, m->ms_seed)) { chat_add(m, "The mission couldn't be generated."); return -1; }
        m->launched = 1;
        return -2;
    }
    return -1;
}

int mps_action(mps *m, shell *sh, int act, int dir, const char *text)
{
    if (!m) return -1;
    char err[128];
    switch (act) {
    case 0: return 0x3a;   /* B21 accept: the connection's state (the port's one: B58, then the lobby) */
    case 2: {              /* B36 join (FUN_100da0fa -> 0x2f) */
        if (m->sel < 0 || m->sel >= m->nses) return -1;
        m->n = net_join(m->ses[m->sel].host, m->ses[m->sel].port, m->player, err, sizeof err);
        if (!m->n) { chat_add(m, err); show(m, sh, 0x24); return -1; }
        net_set_session(m->n, m->ses[m->sel].name, m->ses[m->sel].max);
        return 0x2f;
    }
    case 3: leave(m); return -1;
    case 4: {              /* B49 create (FUN_100d37b7): the session hosted under the entry's name */
        snprintf(m->game, sizeof m->game, "%s", text && text[0] ? text : m->player);
        m->n = net_host(NET_DEFAULT_PORT, m->player, err, sizeof err);
        if (!m->n) { chat_add(m, err); return 0x24; }
        net_set_session(m->n, m->game, m->count);
        return 0x2e;
    }
    case 6: {              /* B46 launch: the mission generated, its seed to all, the session started */
        if (!m->n) return -1;
        unsigned seed = net_ms() ^ 0x5a17u;
        int ty = types[m->type_i];
        if (!generate(m, ty, seed)) { chat_add(m, "The mission couldn't be generated."); show(m, sh, 0x2e); return -1; }
        unsigned char b[7] = {'M', 'S', (unsigned char)ty, (unsigned char)seed, (unsigned char)(seed >> 8),
                              (unsigned char)(seed >> 16), (unsigned char)(seed >> 24)};
        net_send(m->n, 0, 1, b, 7);
        net_start(m->n);
        for (int i = 0; i < 20; i++) net_poll(m->n);
        m->launched = 1;
        return -2;
    }
    case 7: leave(m); return 0x24;
    case 8:                /* B47 ready */
        if (m->n && !m->ready) { net_send(m->n, 1, 1, "RD", 2); m->ready = 1; show(m, sh, 0x2f); }
        return -1;
    case 9:
        m->sel += dir;
        if (m->sel < 0) m->sel = 0;
        if (m->sel >= m->nses) m->sel = m->nses ? m->nses - 1 : 0;
        show(m, sh, 0x24);
        return -1;
    case 12:
        m->count += dir;
        if (m->count < 2) m->count = 2;
        if (m->count > 8) m->count = 8;   /* the templates' eight start navs */
        show(m, sh, 0x31);
        return -1;
    case 13:
        m->type_i = (m->type_i + dir + MS_TYPES) % MS_TYPES;
        show(m, sh, 0x2e);
        return -1;
    case 14: {
        if (!text || !text[0]) return -1;
        char line[96];
        snprintf(line, sizeof line, "%s: %s", m->player, text);
        chat_add(m, line);
        shell_edit_set(sh, 65 + 1, 357 + 86, 65 + 430, 357 + 109, 60, "");   /* CHATB's CTEXT, emptied */
        show(m, sh, m->n ? (net_is_host(m->n) ? 0x2e : 0x2f) : 0x24);
        if (m->n) {
            unsigned char b[100];
            size_t l = strlen(line);
            b[0] = 'C'; b[1] = 'H';
            memcpy(b + 2, line, l);
            net_send(m->n, 0, 1, b, (int)l + 2);
        }
        return -1;
    }
    }
    return -1;
}

const char *mps_scenario(const mps *m) { return m ? m->scenario : ""; }
net *mps_net(mps *m) { return m && m->launched ? m->n : NULL; }

void mps_over(mps *m, const char *results)
{
    if (!m) return;
    snprintf(m->results, sizeof m->results, "%s", results ? results : "");
    leave(m);
}
