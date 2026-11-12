#include "objectives.h"

#include <ctype.h>
#include <string.h>

static int same_ci(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return !*a && !*b;
}

/* 0x10066794: a loaded nav / vehicle / structure inside BTAG `tag` becomes a target of every objective naming that
 * tag whose mask meets its own (at most 0x28 per objective) */
static void reg(objectives *o, const char *tag, unsigned mask, int type, int idx)
{
    if (!tag[0]) return;
    for (int g = 0; g < 16; g++) {
        const mission_mtbl *t = &o->m->mtbl[g];
        for (int k = 0; k < t->nobj && k < 64; k++) {
            const mission_objective *ob = &t->obj[k];
            obj_state *s = &o->st[g][k];
            if (!(ob->type & mask) || s->ntgt >= OBJ_MAX_TARGETS || !same_ci(tag, ob->tag)) continue;
            s->tgt[s->ntgt].type = type;
            s->tgt[s->ntgt].idx = idx;
            s->ntgt++;
        }
    }
}

void objectives_init(objectives *o, const mission *m, int player_gps, unsigned player_mask)
{
    memset(o, 0, sizeof *o);
    o->m = m;
    /* registration in the loader's order: the records are walked once (INFERRED: navs, then vehicles, then
     * structures; the order only matters past 0x28 targets) */
    for (int i = 0; i < m->nnav && i < 0x80; i++) reg(o, m->nav[i].tag, m->nav[i].mask, OBJT_NAV, i);
    for (int i = 0; i < m->ngps; i++) reg(o, m->gps[i].tag, m->gps[i].mask, OBJT_VEHICLE, i);
    reg(o, "USERSTAR", player_mask, OBJT_VEHICLE, player_gps);
    for (int i = 0; i < m->ntgt; i++) reg(o, m->tgt[i].tag, m->tgt[i].mask, OBJT_STRUCTURE, i);
    /* 0x1006998c at the start: every objective pending, its times unset; the groups running */
    for (int g = 0; g < 16; g++) {
        for (int k = 0; k < 64; k++) {
            o->st[g][k].state = OBJ_PENDING;
            o->st[g][k].start = o->st[g][k].end = -1;
            if (k < m->mtbl[g].nobj) o->st[g][k].visible = m->mtbl[g].obj[k].visible;
        }
        o->gstate[g] = GROUP_RUNNING;
        o->gstart[g] = 0;
        o->current[g] = -1;
    }
}

unsigned objectives_type(const objectives *o, int g, int k)
{
    return o->st[g][k].called ? 0x10000u : o->m->mtbl[g].obj[k].type;
}

static int nobj(const objectives *o, int g) { return g >= 0 && g < 16 ? (o->m->mtbl[g].nobj < 64 ? o->m->mtbl[g].nobj : 64) : 0; }

static int done(int s) { return s == OBJ_DONE || s == OBJ_FAILED; }

/* 0x100683d6 */
static int active(const objectives *o, int g, int k)
{
    const obj_state *s = &o->st[g][k];
    const mission_objective *ob = &o->m->mtbl[g].obj[k];
    if (done(s->state)) return 0;
    unsigned ty = objectives_type(o, g, k);
    if (ty == 0x10 || (ty >= 0x8000 && ty <= 0x8002)) return 1;
    for (int c = 0; c < ob->ncond; c++) {
        int tg = ob->cond[c].group, tk = ob->cond[c].obj;
        int ts = tk < nobj(o, tg) ? o->st[tg][tk].state : OBJ_PENDING;
        int ok = (ob->cond[c].kind == 1 && done(ts)) || (ob->cond[c].kind == 2 && ts == OBJ_DONE) ||
                 (ob->cond[c].kind == 3 && ts == OBJ_FAILED);   /* 0x1006832c */
        if (!ob->all && ok) return 1;
        if (ob->all && !ok) return 0;
    }
    return ob->all;
}

static int target_dead(const obj_world *w, int type, int idx)
{
    if (type == OBJT_VEHICLE) return w->vehicle_dead(w->ctx, idx);
    if (type == OBJT_STRUCTURE) return w->structure_dead(w->ctx, idx);
    return 0;   /* navs can't die (0x10066d3b) */
}

/* 0x10066f05 for a nav target: is the group's leader inside it? (marks it visited for the player's group) */
static int at_nav(objectives *o, const obj_world *w, int nav, int g, int player_group)
{
    float x, y, z;
    int flat;
    if (nav < 0 || nav >= o->m->nnav || !w->leader(w->ctx, g, &x, &y, &z, &flat)) return 0;
    const mission_nav *v = &o->m->nav[nav];
    float r = v->radius > 0 ? v->radius : 20000.0f;   /* 0x10101444 / 20000 */
    float dx = x - v->x, dy = flat ? 0.0f : y - v->y, dz = z - v->z;
    if (r * r < dx * dx + dy * dy + dz * dz) return 0;
    /* the visit counts for the player once its gear runs (flag 0x2000, set by 0x100620b8 at state 2) */
    if (g == player_group && o->player_running && v->visit && nav < 0x80) {
        if (!((o->nav_seen[nav] >> g) & 1) && w->nav_reached) w->nav_reached(w->ctx, nav);
        o->nav_seen[nav] |= 1u << g;
    }
    return 1;
}

static void set_state(objectives *o, const obj_world *w, int g, int k, int st)
{
    obj_state *s = &o->st[g][k];
    s->state = st;
    if (st != OBJ_PENDING) s->end = o->now;
    if ((st == OBJ_DONE || st == OBJ_FAILED || st == OBJ_FAILED_AI) && w->announce) w->announce(w->ctx, g, k, st);
}

static void group_end(objectives *o, int g, int st)
{
    if (g < 0 || g >= 16) return;
    o->gstate[g] = st;
    o->gend[g] = o->now;
}

/* 0x100684db */
static void step(objectives *o, const obj_world *w, int g, int k, int player_group)
{
    obj_state *s = &o->st[g][k];
    const mission_objective *ob = &o->m->mtbl[g].obj[k];
    if (done(s->state) || !active(o, g, k)) return;
    if (s->start < 0) s->start = o->now;
    int ok = 1, all_hit = 1;
    unsigned t = objectives_type(o, g, k);
    if (t == 8 || t == 0x20) {
        int any = 0;
        for (int i = 0; i < s->ntgt && !any; i++)
            if (s->tgt[i].type == OBJT_NAV && s->tgt[i].idx < 0x80) any = (o->nav_seen[s->tgt[i].idx] >> g) & 1;
        ok = any;
        if (t == 0x20)
            for (int i = 0; i < nobj(o, g); i++)
                if (i != k && o->m->mtbl[g].obj[i].must && o->st[g][i].state != OBJ_DONE) ok = 0;
    } else if (t == 1 || t == 2) {
        for (int i = 0; i < s->ntgt; i++) ok = ok && target_dead(w, s->tgt[i].type, s->tgt[i].idx);
    } else if (t == 4) {
        for (int i = 0; i < s->ntgt; i++) {
            int dead = target_dead(w, s->tgt[i].type, s->tgt[i].idx);
            all_hit = all_hit && dead;
            ok = ok && !dead;
        }
        if (!s->ntgt) all_hit = 0;   /* bVar5 stays set with no targets: INFERRED unreachable */
        all_hit = 0;                 /* bVar5 only matters on the network */
    } else if (t == 0x100) {
        if (g != player_group && o->current[g] != k) ok = 0;
        for (int i = 0; i < s->ntgt; i++)
            if (s->tgt[i].type == OBJT_NAV) ok = ok && at_nav(o, w, s->tgt[i].idx, g, player_group);
            else ok = 0;   /* vehicle / object targets (0x200 / 0x400 forms): not modelled */
    } else if (t == 0x2000) {
        for (int i = 0; i < s->ntgt; i++)
            if (s->tgt[i].type == OBJT_NAV) ok = ok && !at_nav(o, w, s->tgt[i].idx, g, player_group);
    } else if (t == 0x4000) {
        ok = 0;
        for (int i = 0; i < s->ntgt; i++)
            if (s->tgt[i].type == OBJT_VEHICLE && w->hit_by && w->hit_by(w->ctx, s->tgt[i].idx, g)) ok = 1;
    } else if (t >= 0x8000 && t <= 0x8002) {
        ok = 0;
    }
    int timed_out = ob->limit >= 0 && s->start >= 0 && ob->limit < o->now - s->start;
    int st = OBJ_PENDING;
    int ag = ob->arg_group, ak = ob->arg_obj;
    switch (t) {
    case 4:
        if (timed_out) st = ok ? OBJ_DONE : OBJ_FAILED;
        else {
            if (!ok) st = g == player_group ? OBJ_FAILED : OBJ_FAILED_AI;
            if (all_hit) st = OBJ_FAILED;
        }
        break;
    case 0: case 0x200: case 0x400: case 0x800:
        if (timed_out) st = OBJ_DONE;
        break;
    case 0x10000:
        if (ag >= 0 && ag < 16) group_end(o, ag, GROUP_WON);
        st = OBJ_DONE;
        break;
    case 0x20000:
        if (ag >= 0 && ag < 16) group_end(o, ag, GROUP_LOST);
        st = OBJ_DONE;
        break;
    case 0x40000:
        if (ag >= 0 && ag < 16 && ak >= 0 && ak < nobj(o, ag)) { o->st[ag][ak].visible = !o->st[ag][ak].visible; st = OBJ_DONE; }
        break;
    case 0x80000: case 0x100000:
        if (ag >= 0 && ag < 16 && ak >= 0 && ak < nobj(o, ag) && !done(o->st[ag][ak].state)) {
            set_state(o, w, ag, ak, t == 0x80000 ? OBJ_FAILED : OBJ_DONE);
            st = OBJ_DONE;
        }
        break;
    case 0x200000:
        if (ag >= 0 && ag < 16) {
            for (int i = 0; i < nobj(o, ag); i++) {
                o->st[ag][i].state = OBJ_PENDING;
                o->st[ag][i].start = o->st[ag][i].end = -1;
            }
            o->gstate[ag] = GROUP_RUNNING;
            st = OBJ_DONE;
        }
        break;
    case 0x400000:
        if (ag >= 0 && ag < 16 && ak >= 0 && ak < nobj(o, ag)) {
            o->st[ag][ak].state = OBJ_PENDING;
            o->st[ag][ak].start = o->st[ag][ak].end = -1;
            st = OBJ_DONE;
        }
        break;
    default:
        if (ok) st = OBJ_DONE;
        else if (timed_out) st = OBJ_FAILED;
    }
    if (st != OBJ_PENDING) set_state(o, w, g, k, st);
    else s->state = OBJ_PENDING;
}

int objectives_tick(objectives *o, int sim_ticks, const obj_world *w, int player_group)
{
    o->now = sim_ticks / 0xb5;
    for (int g = 0; g < 16; g++) {
        const mission_mtbl *t = &o->m->mtbl[g];
        if (!t->present) continue;
        for (int k = 0; k < nobj(o, g); k++) step(o, w, g, k, player_group);
        if (o->gstate[g] == GROUP_RUNNING) {
            int won = 1, lost = 0;
            for (int k = 0; k < nobj(o, g); k++) {
                int s = o->st[g][k].state;
                if (!t->obj[k].must || s == OBJ_DONE) continue;
                won = 0;
                if (s == OBJ_FAILED || s == OBJ_FAILED_AI) lost = 1;
            }
            if (won) group_end(o, g, GROUP_WON);
            else if (lost) group_end(o, g, GROUP_LOST);
            else if (t->limit > 0 && t->limit <= o->now - o->gstart[g]) group_end(o, g, GROUP_TIMEOUT);
        }
        /* the group's current objective: its first active one of the order kinds (mask below 0x10000) */
        o->current[g] = -1;
        for (int k = 0; k < nobj(o, g); k++)
            if (active(o, g, k) && !(objectives_type(o, g, k) & 0xffff0000u)) { o->current[g] = k; break; }
    }
    return player_group >= 0 && player_group < 16 ? o->gstate[player_group] : GROUP_RUNNING;
}

int objectives_radio_slot(const objectives *o, int pg, int slot)
{
    int got = -1;
    for (int k = 0; k < nobj(o, pg); k++)
        if (o->st[pg][k].visible && objectives_type(o, pg, k) == 0x8000u + (unsigned)slot) got = k;
    return got;
}

void objectives_call(objectives *o, int pg, int k)
{
    if (k > 0 && k < nobj(o, pg)) o->st[pg][k].called = 1;
}
