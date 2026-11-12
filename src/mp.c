#include "mp.h"
#include "gear_tables.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#define MAXV 64
#define DA_MS 0x24        /* 0x10110138 */
#define DROP_MS 0x38e     /* 0x10031a86 */

typedef struct {
    int  unit;            /* local unit, -1 none */
    int  slot;            /* the player's slot, -1 a mission vehicle */
    int  remote;          /* simulated elsewhere */
    int  last_clock;      /* the newest DA clock taken (0x107caba0) */
    unsigned last_ms;     /* when its last DA came (0x107cab60) */
    int  dropped, dead_seen;
    unsigned char dm[GEAR_ZONES];   /* the last DM sent / taken (0x107caa85) */
    int  prev_state[GEAR_MAX_WEAPONS];
    unsigned we;          /* weapons that started firing, not yet sent (+0xbf5) */
    int  attacker;        /* the last one to strike it (+0x14c), wire index, -1 none */
} mpv;

struct mp {
    net *n;
    units *us;
    world *w;
    hg_archive *a;
    int base, nv;
    mpv v[MAXV];                       /* by wire index */
    int local;                         /* the local player's wire index */
    int me_slot, nplayers;
    net_player pl[NET_MAX_PLAYERS];
    int go_in[NET_MAX_PLAYERS];        /* 0x107cb430: the master's record of who is in */
    int started, ended, go_sent;
    unsigned next_da, last_go;
    float lag;                         /* 0x107cb42c */
    int now_clock;                     /* this machine's sim clock as the frame began */
    int nwe;
    short d[NET_MAX_PLAYERS][NET_MAX_PLAYERS];   /* 0x1028cc2c: deaths [killer][victim] */
    char msg[96];
    int have_msg;
};

static void p16(unsigned char *p, int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void p32(unsigned char *p, int v) { p16(p, v & 0xffff); p16(p + 2, (int)((unsigned)v >> 16)); }
static int g16(const unsigned char *p) { return p[0] | p[1] << 8; }
static int g32(const unsigned char *p) { return (int)((unsigned)g16(p) | (unsigned)g16(p + 2) << 16); }
static int fx(float f) { return (int)floorf(f * 65536.0f + 0.5f); }   /* x 65536 + 0.5, __ftol */
static float unfx(int v) { return (float)v * (1.0f / 65536.0f); }

static int wire_of_unit(const mp *m, int unit)
{
    for (int i = 0; i < m->nv; i++) if (m->v[i].unit == unit && unit >= 0) return i;
    return -1;
}

static int slot_of_id(const mp *m, int id)
{
    for (int i = 0; i < m->nplayers; i++) if (m->pl[i].id == id) return i;
    return -1;
}

static void say(mp *m, const char *s)
{
    snprintf(m->msg, sizeof m->msg, "%s", s);
    m->have_msg = 1;
    fprintf(stderr, "mp: %s\n", s);
}

mp *mp_open(net *n, units *us, world *w, hg_archive *a, int base)
{
    mp *m = calloc(1, sizeof *m);
    m->n = n;
    m->us = us;
    m->w = w;
    m->a = a;
    m->base = base;
    m->nplayers = net_players(n, m->pl);
    m->me_slot = slot_of_id(m, net_me(n));
    /* the mission's own vehicles: wire 0 .. base - 1; the master runs them */
    for (int i = 0; i < base && i < MAXV; i++) {
        m->v[i].unit = i;
        m->v[i].slot = -1;
        m->v[i].remote = !net_is_host(n);
        m->v[i].attacker = -1;
    }
    m->nv = base;
    for (int k = 0; k < m->nplayers && base + k < MAXV; k++) {
        mpv *v = &m->v[base + k];
        v->unit = -1;
        v->slot = k;
        v->remote = k != m->me_slot;
        v->attacker = -1;
    }
    m->nv = base + m->nplayers;
    m->local = base + (m->me_slot < 0 ? 0 : m->me_slot);
    if (!net_is_host(n))
        for (int i = 0; i < base; i++) us->u[i].remote = 1;
    return m;
}

void mp_close(mp *m) { free(m); }
int  mp_slot(const mp *m) { return m->me_slot; }
int  mp_count(const mp *m) { return m->nplayers; }

/* the multiplayer templates' player places: M99MSCN1 includes start00 .. start07, a nav each (the record's name
 * its tag); slot k takes start<k> (the port's pairing: the original's EN<nn>STAR records, written by the shell for
 * the session's players, aren't traced) */
static int start_nav(const mp *m, int slot, float *x, float *z, float *h)
{
    const mission *ms = world_mission(m->w);
    char want[16];
    snprintf(want, sizeof want, "start%02d", slot);
    for (int i = 0; i < ms->nnav; i++)
        if (!strcasecmp(ms->nav[i].tag, want)) {
            *x = ms->nav[i].x; *z = ms->nav[i].z;
            if (h) *h = ms->nav[i].heading;
            return 1;
        }
    return 0;
}

int mp_has_starts(const mp *m)
{
    float x, z;
    return start_nav(m, 0, &x, &z, NULL);
}

void mp_start_pos(const mp *m, int slot, float *x, float *z, float heading)
{
    if (start_nav(m, slot, x, z, NULL)) return;
    float r = heading * 3.14159265f / 180.0f, off = 1500.0f * (float)slot;
    *x += off * cosf(r);
    *z -= off * sinf(r);
}

void mp_set_local(mp *m, int unit)
{
    m->v[m->local].unit = unit;
}

int mp_add_remotes(mp *m, const char *gdf, const sim_gear *proto)
{
    int added = 0;
    for (int k = 0; k < m->nplayers; k++) {
        if (k == m->me_slot) continue;
        sim_gear g = *proto;
        const mission_start st = mission_group_start(world_mission(m->w), m->us->pgroup);
        g.x = st.found ? st.x : proto->x;
        g.z = st.found ? st.z : proto->z;
        float hd = st.found ? st.heading : proto->heading;
        mp_start_pos(m, k, &g.x, &g.z, hd);
        if (mp_has_starts(m)) {   /* on the ground at its start nav */
            int gf;
            float gh = world_ground_height(m->w, g.x, g.z, &gf);
            if (gf) g.y = gh + g.stand;
        }
        g.heading = hd;
        int u = units_add_player(m->us, m->w, m->a, gdf, 1, 6.0f, &g);
        if (u < 0) continue;
        m->us->u[u].player = 0;     /* drawn, not the local player */
        m->us->u[u].remote = 1;
        m->us->u[u].group = 15 - k; /* each slot its own team */
        m->v[m->base + k].unit = u;
        m->v[m->base + k].last_ms = net_ms();
        added++;
    }
    return added;
}

int mp_started(const mp *m) { return m->started; }
int mp_remote(const mp *m, int unit)
{
    int i = wire_of_unit(m, unit);
    return i >= 0 && m->v[i].remote;
}

/* ---- sending */

static void send_da(mp *m, int wi, int clock)
{
    mpv *v = &m->v[wi];
    if (v->unit < 0) return;
    const unit *u = &m->us->u[v->unit];
    unsigned char b[0x42];
    memset(b, 0, sizeof b);
    b[0] = 'D'; b[1] = 'A';
    b[2] = (unsigned char)wi;
    int tw = u->target >= 0 ? wire_of_unit(m, u->target) : -1;
    p16(b + 3, tw >= 0 ? 0x200 | tw : 0x1000);
    p16(b + 5, v->attacker >= 0 ? v->attacker : 0xffff);
    int state = u->cb.dead ? 4 : 2;   /* +0x4f0: 2 running, 4 destroyed */
    p16(b + 7, state | (u->s.skating ? 2 : 0) << 12);
    p32(b + 0xa, clock);
    p32(b + 0xe, (int)u->s.x); p32(b + 0x12, (int)u->s.y); p32(b + 0x16, (int)u->s.z);
    p32(b + 0x1a, fx(u->s.pitch.cur)); p32(b + 0x1e, fx(u->s.heading)); p32(b + 0x22, fx(u->s.roll.cur));
    p32(b + 0x26, (int)u->s.stand); p32(b + 0x2a, (int)u->s.stand);
    p32(b + 0x2e, fx(u->s.speed)); p32(b + 0x32, fx(u->s.throttle)); p32(b + 0x36, fx(u->s.turn_rate));
    /* +0x3a / +0x3e: the gear's +8 / +0x18, not kept by the port's sim (0) */
    net_send(m->n, 0, 0, b, 0x42);
}

static void send_we(mp *m, int wi)
{
    mpv *v = &m->v[wi];
    if (!v->we) return;
    unsigned char b[5] = {'W', 'E', (unsigned char)wi, 0, 0};
    p16(b + 3, (int)v->we);
    v->we = 0;
    net_send(m->n, 0, 0, b, 5);
}

static void send_dm(mp *m, int wi)
{
    mpv *v = &m->v[wi];
    if (v->unit < 0) return;
    const combatant *c = &m->us->u[v->unit].cb;
    unsigned char b[0x16];
    b[0] = 'D'; b[1] = 'M';
    b[2] = (unsigned char)wi;
    p16(b + 3, v->attacker >= 0 ? v->attacker : 0xffff);
    int changed = 0;
    for (int z = 0; z < GEAR_ZONES; z++) {
        int lv = 0;
        if (c->ztotal[z]) lv = 15 - (int)((c->internal[z] * 0.5f + c->armor[z] * 0.5f) * 3.0f / (float)c->ztotal[z]);
        if (lv < 0) lv = 0;
        if (lv > 15) lv = 15;
        unsigned char x = (unsigned char)(c->internal[z] >= 1e-7f && !c->zone_gone[z] ? lv : lv | 0x10);
        b[5 + z] = x;
        if (x != v->dm[z]) { v->dm[z] = x; changed = 1; }
    }
    if (changed) net_send(m->n, 0, 1, b, 0x16);
}

static void send_ki(mp *m)
{
    if (m->me_slot < 0) return;
    unsigned char b[0xc];
    memset(b, 0, sizeof b);
    b[0] = 'K'; b[1] = 'I';
    for (int k = 0; k < m->nplayers && k < 10; k++) b[2 + k] = (unsigned char)m->d[k][m->me_slot];
    net_send(m->n, 0, 1, b, 0xc);
}

/* ---- receiving */

static float ground(mp *m, float x, float z)
{
    int f;
    float h = world_ground_height(m->w, x, z, &f);
    return f ? h : -1e9f;
}

static void take_da(mp *m, const unsigned char *b, int len, int from, int *clock_out)
{
    if (len < 0x42) return;
    int wi = b[2];
    if (wi >= m->nv || !m->v[wi].remote || m->v[wi].unit < 0) return;
    mpv *v = &m->v[wi];
    int clk = g32(b + 0xa);
    if (clk < v->last_clock) return;       /* an older state */
    v->last_clock = clk;
    v->last_ms = net_ms();
    if (from == 1 && clock_out) *clock_out = clk;   /* the master's clock (0x10032cbe, DAT_10131524 == 2) */
    unit *u = &m->us->u[v->unit];
    if (v->dropped) {   /* back again (0x10031a86: shown) */
        v->dropped = 0;
        for (int k = 0; k < u->gm.nparts; k++) world_show_object(m->w, u->obj[k], 1);
    }
    int tc = g16(b + 3);
    u->target = (tc & 0xf00) == 0x200 && (tc & 0xff) < m->nv ? m->v[tc & 0xff].unit : -1;
    int att = g16(b + 5);
    v->attacker = att < m->nv ? att : -1;
    int flags = g16(b + 7);
    u->s.x = (float)g32(b + 0xe); u->s.y = (float)g32(b + 0x12); u->s.z = (float)g32(b + 0x16);
    u->s.pitch.cur = unfx(g32(b + 0x1a)); u->s.heading = unfx(g32(b + 0x1e)); u->s.roll.cur = unfx(g32(b + 0x22));
    u->s.stand = (float)g32(b + 0x26);
    u->s.speed = unfx(g32(b + 0x2e)); u->s.throttle = unfx(g32(b + 0x32)); u->s.turn_rate = unfx(g32(b + 0x36));
    u->s.skating = (flags >> 12 & 2) != 0;
    /* the lag (0x10032cbe): 0.2 new + 0.8 old, held to 0x24 ticks */
    float lag = (float)(m->now_clock - clk);   /* ticks behind: a joiner runs on the master's clock */
    if (lag < 0) lag = 0;
    m->lag = m->lag == 0 ? lag : lag * 0.2f + m->lag * 0.8f;
    if (m->lag > (float)DA_MS) m->lag = DA_MS;
    float r = u->s.heading * 3.14159265f / 180.0f;
    float nx = u->s.x + sinf(r) * u->s.speed * m->lag, nz = u->s.z + cosf(r) * u->s.speed * m->lag;
    if (u->s.y - u->s.stand - ground(m, nx, nz) < 60.0f) { u->s.x = nx; u->s.z = nz; }
    int state = flags & 0xf;
    if ((state == 4 || state == 5) && !v->dead_seen) {
        v->dead_seen = 1;
        u->cb.dead = 1;
        char t[96];
        int ks = v->attacker >= 0 ? m->v[v->attacker].slot : -1;
        const char *vn = v->slot >= 0 ? m->pl[v->slot].name : u->gear;
        if (ks < 0) snprintf(t, sizeof t, "'%s' destroyed", vn);
        else if (v->attacker == wi) snprintf(t, sizeof t, "'%s' self-destructed", vn);
        else snprintf(t, sizeof t, "'%s' destroyed by '%s'", vn, m->pl[ks].name);
        say(m, t);
    }
}

static void take_we(mp *m, const unsigned char *b, int len)
{
    if (len < 5) return;
    int wi = b[2];
    if (wi >= m->nv || !m->v[wi].remote || m->v[wi].unit < 0) return;
    combatant *c = &m->us->u[m->v[wi].unit].cb;
    int mask = g16(b + 3);
    m->nwe++;
    for (int k = 0; k < 8 && k < c->gr.nweapons; k++, mask >>= 1)
        if ((mask & 1) && c->state[k] >= 0) {   /* +0x4fc = k, the weapon's +8 = 2 (0x10033ec2) */
            int t = c->gr.weapon[k].type;
            c->sel = k;
            c->state[k] = 2;
            c->left[k] = t >= 0 && t < GEAR_WEAPON_TYPES ? gear_weapon_stat[t].burst : 1;
            c->timer[k] = 0;
        }
}

static void take_dm(mp *m, const unsigned char *b, int len)
{
    if (len < 0x16) return;
    int wi = b[2];
    if (wi >= m->nv || !m->v[wi].remote || m->v[wi].unit < 0) return;
    mpv *v = &m->v[wi];
    combatant *c = &m->us->u[v->unit].cb;
    int att = g16(b + 3);
    v->attacker = att < m->nv ? att : -1;
    for (int z = 0; z < GEAR_ZONES; z++) {
        int x = b[5 + z];
        v->dm[z] = (unsigned char)x;
        /* the level shown: (15 - level) x zone +0x38 / 3 (0x10033a8a) as the internal and armor alike */
        float left = (float)(15 - (x & 0xf)) * (float)c->ztotal[z] / 3.0f;
        if (!c->zone_gone[z] && c->ztotal[z] > 0) { c->armor[z] = left; c->internal[z] = left; }
        /* only zones the vehicle has (the original's absent zones carry +0x36 0x2000 already) */
        if ((x & 0x10) && !c->zone_gone[z] && c->ztotal[z] > 0) combat_destroy_zone(c, z + 1);
    }
}

static void take_ki(mp *m, const unsigned char *b, int len, int from)
{
    int s = slot_of_id(m, from);
    if (len < 0xc || s < 0) return;
    for (int k = 0; k < m->nplayers && k < 10; k++) m->d[k][s] = b[2 + k];
}

static void dispatch(mp *m, int from, const unsigned char *b, int len, int *clock_out)
{
    if (len < 2) return;
    char t[3] = {(char)b[0], (char)b[1], 0};
    if (!strcmp(t, "DA")) take_da(m, b, len, from, clock_out);
    else if (!strcmp(t, "WE")) take_we(m, b, len);
    else if (!strcmp(t, "DM")) take_dm(m, b, len);
    else if (!strcmp(t, "KI")) take_ki(m, b, len, from);
    else if (!strcmp(t, "GO") && len >= 2) {
        int s = slot_of_id(m, from);
        if (net_is_host(m->n)) { if (s >= 0) m->go_in[s] = 1; }
        else if (from == 1) m->started = 1;   /* the master's go */
    } else if (!strcmp(t, "EG")) {
        m->ended = 1;
    }
}

void mp_frame(mp *m, int sim_clock, int *clock_out)
{
    m->now_clock = sim_clock;
    net_poll(m->n);
    unsigned char b[NET_MAX_MSG];
    int from, l;
    while ((l = net_recv(m->n, &from, b, sizeof b)) >= 0) dispatch(m, from, b, l, clock_out);
    if (net_started(m->n) < 0) m->ended = 1;
    unsigned now = net_ms();
    if (!m->started) {
        /* 0x10035673 / 0x100356ce: a joiner says GO to the master (again until answered); the master, once all are
         * in, says GO to everyone and starts */
        if (net_is_host(m->n)) {
            m->go_in[m->me_slot < 0 ? 0 : m->me_slot] = 1;
            int all = 1;
            m->nplayers = net_players(m->n, m->pl);
            for (int k = 0; k < m->nplayers; k++) if (!m->go_in[k]) all = 0;
            if (all) {
                unsigned char g[2] = {'G', 'O'};
                net_send(m->n, 0, 1, g, 2);
                m->started = 1;
            }
        } else if (now - m->last_go > 500) {
            unsigned char g[2] = {'G', 'O'};
            net_send(m->n, 1, 1, g, 2);
            m->last_go = now;
        }
        return;
    }
    if ((int)(now - m->next_da) >= 0) {
        m->next_da = now + DA_MS;
        for (int i = 0; i < m->nv; i++) {
            if (m->v[i].remote || m->v[i].unit < 0) continue;
            send_da(m, i, sim_clock);
            send_we(m, i);
            send_dm(m, i);
        }
        /* the silent ones (0x10031a86: 0x38e ms): hidden and dropped */
        for (int i = 0; i < m->nv; i++) {
            mpv *v = &m->v[i];
            if (!v->remote || v->unit < 0 || v->dropped || v->slot < 0) continue;
            if (now - v->last_ms > DROP_MS * 4) {
                unit *u = &m->us->u[v->unit];
                v->dropped = 1;
                for (int k = 0; k < u->gm.nparts; k++) world_show_object(m->w, u->obj[k], 0);
                char t[64];
                snprintf(t, sizeof t, "'%s' has left", m->pl[v->slot].name);
                say(m, t);
            }
        }
    }
}

static void mount(void *ctx, int kind, float from[3])
{
    const unit *u = ctx;
    gearmodel_mount(&u->gm, u->s.x, u->s.y, u->s.z, u->s.heading, kind, from);
}

void mp_tick(mp *m, combat_shots *s)
{
    for (int i = 0; i < m->nv; i++) {
        mpv *v = &m->v[i];
        if (v->unit < 0) continue;
        unit *u = &m->us->u[v->unit];
        combatant *c = &u->cb;
        if (!v->remote) {   /* the weapons that started a burst this tick (+0xbf5) */
            for (int k = 0; k < c->gr.nweapons && k < 8; k++) {
                if (c->state[k] == 2 && v->prev_state[k] != 2) v->we |= 1u << k;
                v->prev_state[k] = c->state[k];
            }
            continue;
        }
        if (c->dead || v->dropped) continue;
        /* the remote vehicle's bursts go on here (every machine flies every shot) */
        float from[3] = {u->s.x, u->s.y, u->s.z}, to[3];
        if (u->target >= 0 && u->target < m->us->n) {
            const unit *t = &m->us->u[u->target];
            to[0] = t->s.x; to[1] = t->s.y - t->s.stand * 0.5f; to[2] = t->s.z;
        } else {
            float r = u->s.heading * 3.14159265f / 180.0f, p = u->s.pitch.cur * 3.14159265f / 180.0f;
            to[0] = u->s.x + sinf(r) * cosf(p) * 150000.0f;
            to[1] = u->s.y + sinf(-p) * 150000.0f;
            to[2] = u->s.z + cosf(r) * cosf(p) * 150000.0f;
        }
        combat_weapons_tick(c, v->unit, 0, from, to, mount, u, s);
    }
}

void mp_killed(mp *m, int victim, int owner)
{
    int wv = wire_of_unit(m, victim), wo = owner >= 0 ? wire_of_unit(m, owner) : -1;
    if (wv < 0 || m->v[wv].remote) return;
    m->v[wv].attacker = wo;
    int vs = m->v[wv].slot, ks = wo >= 0 ? m->v[wo].slot : -1;
    if (vs >= 0) {
        if (ks < 0) ks = vs;   /* no player struck it: counted as its own */
        m->d[ks][vs]++;        /* 0x10059e9d */
        send_ki(m);
    }
    char t[96];
    const char *vn = vs >= 0 ? m->pl[vs].name : m->us->u[victim].gear;
    if (wo < 0 || m->v[wo].slot < 0) snprintf(t, sizeof t, "'%s' destroyed", vn);
    else if (wo == wv) snprintf(t, sizeof t, "'%s' self-destructed", vn);
    else snprintf(t, sizeof t, "'%s' destroyed by '%s'", vn, m->pl[m->v[wo].slot].name);
    say(m, t);
}

void mp_end(mp *m)
{
    if (m->ended) return;
    unsigned char g[2] = {'E', 'G'};
    net_send(m->n, 0, 1, g, 2);
    m->ended = 1;
    for (int i = 0; i < 20; i++) net_poll(m->n);   /* push it out */
}

int mp_ended(const mp *m) { return m->ended; }

int mp_score(const mp *m, int slot)
{
    /* 0x10035105: a vehicle's kills of the other groups' vehicles, less those of its own group's (itself too) */
    int sc = 0;
    for (int c = 0; c < m->nplayers; c++) sc += c == slot ? -m->d[slot][c] : m->d[slot][c];
    return sc;
}

const char *mp_name(const mp *m, int slot) { return slot >= 0 && slot < m->nplayers ? m->pl[slot].name : ""; }

const char *mp_message(mp *m)
{
    if (!m->have_msg) return NULL;
    m->have_msg = 0;
    return m->msg;
}
