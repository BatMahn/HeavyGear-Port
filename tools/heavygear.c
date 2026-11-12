/*
 * heavygear - the port's game: the front end (shell) and, from the briefing's launch button, the mission.
 *   heavygear <game folder>
 *   HG_SCRIPT="..." heavygear <game folder>   no window: runs a script of ';'-separated steps:
 *       c x y (click), d x y (press), m x y (move / release), t text (type), k code (key, Windows code), K l r (the
 *       arrow keys held: Left, Right 0 / 1), S n (go to state n), s file.ppm (screenshot),
 *       w ms (let time pass)
 *   HG_MISSION=m01_scn1 / HG_MOVIE=ZL_CUT01   straight into a mission / play a cutscene first
 * In a mission: the player's gear (1-9 / 0 throttle, W / S step it, Left / Right turn, mouse drag looks; ` a free
 * camera, L look, H HUD; GAMEKEY.MAP's F1 mission channel, F2-F7 squad mates, F12 objectives); Esc returns to the
 * shell as a failure, Ctrl+F12 as a success (the mission ends by itself when its objectives decide it).
 */
#include "../src/fsutil.h"
#include "../src/hgprj.h"
#include "../src/hud.h"
#include "../src/sim.h"
#include "../src/audio.h"
#include "../src/front.h"
#include "../src/movie.h"
#include "../src/world.h"
#include "../src/units.h"
#include "../src/structures.h"
#include "../src/objectives.h"
#include "../src/radio.h"
#include "../src/sfx.h"
#include "../src/effects.h"
#include "../src/squad.h"
#include "../src/brf.h"
#include "../src/gear_tables.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_ppm(const char *path, const uint32_t *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        unsigned char rgb[3] = {(unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8), (unsigned char)px[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void load_skygnd(world *w, const char *game)
{
    char p[1200];
    int own = !fs_find_ci(game, "Patch", p, sizeof p);
    const char *base = SDL_GetBasePath();
    int ok = -1;
    if (own && !fs_find_ci(game, "SKYGND.PAR", p, sizeof p)) ok = world_set_skygnd(w, p);
    if (ok) { snprintf(p, sizeof p, "%sgamedata/patch12/SKYGND.PAR", base ? base : ""); ok = world_set_skygnd(w, p); }
    if (ok) ok = world_set_skygnd(w, "gamedata/patch12/SKYGND.PAR");
    if (ok && !fs_find_ci(game, "SKYGND.PAR", p, sizeof p)) world_set_skygnd(w, p);
}

/* the shots' world: the mission's units (the player's gear among them), its structures (ids from STRUCT_ID) and
 * the ground */
#define STRUCT_ID 10000
#define STATIC_ID 20000   /* another mission object (rocks, walls ...): it stops the shot, like the ground */
typedef struct { world *w; units *us; structures *st; } battle;

static int snd_cockpit = 1, snd_player = -1;
static int struct_down = -1;   /* the structure the shot just brought down (for its explosion) */
/* the visible effects (src/effects.h): world objects for the pool */
static effects fx;
static battle *fx_battle;
static int fx_add(void *ctx, const char *model) { return world_add_effect((world *)ctx, model); }
static void fx_set(void *ctx, int h, int on, float x, float y, float z, float age) { world_set_effect((world *)ctx, h, on, x, y, z, age); }
/* start an effect and, if it started, its sound there (0x1001b3a1: +8 > 0, the chance +0xc) - quiet in the cockpit
 * view unless `loud` */
static int fx_play(int e, float x, float y, float z, int follow, int loud)
{
    if (!effects_start(&fx, e, x, y, z, follow)) return 0;
    if (gear_effect[e].gate <= 0 || !gear_effect[e].sound[0]) return 1;
    if (gear_effect[e].chance != -1 && rand() % 100 >= gear_effect[e].chance) return 1;
    sfx_at(gear_effect[e].sound, x, y, z, snd_cockpit && !loud, 0x32);
    return 1;
}

static float fx_ground(void *ctx, float x, float z) { int found; return world_ground_height((world *)ctx, x, z, &found); }
static float fx_radius(void *ctx, int h)
{
    float mn[3], mx[3];
    if (world_object_bounds((world *)ctx, h, mn, mx)) return 0;
    float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
    return 0.5f * SDL_sqrtf(dx * dx + dy * dy + dz * dz);   /* INFERRED: the box's, for the bounding sphere's */
}
static void fx_turn(void *ctx, int h, float yaw, float pitch, float roll) { world_turn_object((world *)ctx, h, yaw, pitch, roll); }

static void on_killed(void *ctx, int victim, int owner);

/* an explosion's harm (0x1001c75c: each vehicle whose origin is within its size (+0x53c, a gear's standing height)
 * and the radius takes power on zones 2..6 and a quarter on the head, as a shot of multiplier 0.1, chip 0, mode 1;
 * 0x1001c96a: each structure whose bounding sphere the radius reaches takes the power) */
static void fx_splash(void *ctx, int owner, float x, float y, float z, float radius, float power, int vehicles)
{
    (void)ctx;
    battle *b = fx_battle;
    if (!b) return;
    unsigned p = (unsigned)(int)power;   /* __ftol */
    for (int i = b->us->n - 1; vehicles && i >= 0; i--) {
        unit *u = &b->us->u[i];
        if (u->cb.dead) continue;
        float dx = u->s.x - x, dy = u->s.y - y, dz = u->s.z - z;
        if (SDL_sqrtf(dx * dx + dy * dy + dz * dz) >= u->gm.stand + radius) continue;
        int killed = 0;
        for (int zn = 2; zn <= 6 && !killed; zn++) killed = combat_damage_raw(&u->cb, (int)p, 0.1f, 0, 1, zn);
        if (!killed) killed = combat_damage_raw(&u->cb, (int)(p >> 2), 0.1f, 0, 1, 1);
        if (killed) on_killed(b, i, owner);
    }
    for (int i = b->st->n - 1; i >= 0; i--) {
        if (b->st->s[i].dead) continue;
        float mn[3], mx[3], ox, oy, oz, rs, rc;
        int in = b->st->s[i].inst;
        if (world_object_bounds(b->w, in, mn, mx) || world_object_pose(b->w, in, &ox, &oy, &oz, &rs, &rc)) continue;
        float lx = (mn[0] + mx[0]) / 2, ly = (mn[1] + mx[1]) / 2, lz = (mn[2] + mx[2]) / 2, r = 0;
        float c[3] = {ox + lx * rc + lz * rs, oy + ly, oz - lx * rs + lz * rc};
        for (int k = 0; k < 3; k++) r += (mx[k] - mn[k]) * (mx[k] - mn[k]);
        r = SDL_sqrtf(r) / 2;   /* INFERRED: the box's half diagonal for the bounding sphere */
        float dx = c[0] - x, dy = c[1] - y, dz = c[2] - z;
        if (SDL_sqrtf(dx * dx + dy * dy + dz * dz) >= r + radius) continue;
        if (structures_damage(b->st, b->w, i, (int)power)) {
            int was = fx.owner;
            fx.owner = owner;
            fx_play(0x32, c[0], c[1], c[2], -1, 0);
            fx.owner = was;
        }
    }
}

static int fx_vehicle_pos(void *ctx, int u, float p[3])
{
    (void)ctx;
    if (!fx_battle || u < 0 || u >= fx_battle->us->n || fx_battle->us->u[u].cb.dead) return 0;
    const unit *v = &fx_battle->us->u[u];
    p[0] = v->s.x; p[1] = v->s.y; p[2] = v->s.z;
    return 1;
}
/* the debriefing's statistics: HGSHELL 0x100e27c9's lines from the sim's counters (0x1028cae0, written to HGCAR.CFG):
 * Vehicle Kills the player's kills of side-1 vehicles of kinds 1, 2, 3, 4, 5, 8, 10 (+0x48..; 0x10059e9d), Hit Rate
 * its shots striking side-1 vehicles (+0x15, 0x100191d7) x 100 / its shots (+0x13, at least 1; held to 100), Hits
 * Taken every shot striking its vehicle (+0x1b), Squad-mate Kills the kills of side-1 vehicles of kinds 1, 2, 3, 8,
 * 10 by the squad's slots 1 and 2 (+0x8a..), Squad-mates Killed those two slots' deaths (+0x142 / +0x144) */
static struct { int kills, shots, hits, taken, mate_kills; } stat;
static int stat_mates_lost, stat_slot_lost[3];
static char stat_objectives[2048];

static int battle_hit(void *ctx, int owner, const float p0[3], const float p1[3], float *t, int *zone)
{
    battle *b = ctx;
    float ts = 2;
    int v = units_hit(b->us, b->w, owner, p0, p1, t, zone);
    int s = structures_hit(b->st, b->w, p0, p1, &ts);
    if (s >= 0 && (v < 0 || ts < *t)) { *t = ts; *zone = 0; v = STRUCT_ID + s; }
    /* INFERRED: every other object stops shots by its polygons (the original's scene test, 0x100229d9) */
    float to = 2;
    int o = world_ray(b->w, p0, p1, &to);
    if (o >= 0 && (v < 0 || to < *t)) { *t = to; *zone = 0; v = STATIC_ID + o; }
    return v;
}

static float battle_ground(void *ctx, float x, float z)
{
    int f;
    float h = world_ground_height(((battle *)ctx)->w, x, z, &f);
    return f ? h : -1e9f;
}

static combatant *battle_victim(void *ctx, int id)
{
    battle *b = ctx;
    return id >= 0 && id < b->us->n ? &b->us->u[id].cb : NULL;
}

static void battle_hit_other(void *ctx, int id, int owner, int power)
{
    battle *b = ctx;
    (void)owner;
    if (id >= STRUCT_ID && id < STATIC_ID && structures_damage(b->st, b->w, id - STRUCT_ID, power)) struct_down = id;
}

static int battle_target_pos(void *ctx, int id, float out[3])
{
    battle *b = ctx;
    if (id < 0 || id >= b->us->n || b->us->u[id].cb.dead) return 0;
    const unit *u = &b->us->u[id];
    out[0] = u->s.x; out[1] = u->s.y; out[2] = u->s.z;   /* the vehicle's position (+0x54), as the original aims */
    return 1;
}

static void battle_hit_vehicle(void *ctx, int id, int owner)
{
    battle *b = ctx;
    if (id < 0 || id >= b->us->n || owner < 0 || owner >= b->us->n) return;
    const unit *o = &b->us->u[owner];
    b->us->u[id].hit_groups |= 1u << ((o->player ? 0 : o->group) & 15);
    if (o->player && b->us->u[id].side == 1) stat.hits++;
}

/* the objectives' view of the sim */
static int ob_vehicle_dead(void *ctx, int gps)
{
    battle *b = ctx;
    int i = units_by_gps(b->us, gps);
    return i >= 0 && b->us->u[i].cb.dead;
}

static int ob_structure_dead(void *ctx, int tgt)
{
    battle *b = ctx;
    return tgt >= 0 && tgt < b->st->n && b->st->s[tgt].dead;
}

static int ob_leader(void *ctx, int group, float *x, float *y, float *z, int *flat)
{
    battle *b = ctx;
    int i = units_leader(b->us, group);
    if (i < 0) return 0;
    const unit *u = &b->us->u[i];
    *x = u->s.x; *y = u->s.y; *z = u->s.z;
    *flat = u->gm.vclass == 5 || u->gm.vclass == 8;   /* 0x10066eeb */
    return 1;
}

static int ob_hit_by(void *ctx, int gps, int group)
{
    battle *b = ctx;
    int i = units_by_gps(b->us, gps);
    if (i < 0 || !((b->us->u[i].hit_groups >> group) & 1)) return 0;
    b->us->u[i].hit_groups &= ~(1u << group);
    return 1;
}

/* the sim's voices (src/radio.h): sounds from the archive, "WAV" (the language variant, WAVX, first) */
typedef struct { hg_archive *a; hg_wave wave; int voice; } voices;

static int voice_load(hg_archive *a, const char *name, hg_wave *w)
{
    size_t len;
    uint8_t *d = hga_load(a, name, "WAV", &len, NULL);
    int r = d ? wave_parse(d, len, w) : -1;
    free(d);
    return r;
}

static int voice_exists(void *ctx, const char *name)
{
    hg_wave w;
    if (voice_load(((voices *)ctx)->a, name, &w)) return 0;
    wave_free(&w);
    return 1;
}

static int voice_start(void *ctx, const char *name)
{
    voices *v = ctx;
    audio_stop(v->voice);
    v->voice = 0;
    wave_free(&v->wave);
    if (voice_load(v->a, name, &v->wave)) {
        fprintf(stderr, "voice %s: not found\n", name);
        return 0;
    }
    /* full volume (0x10016f28 passes the voice volume DAT_10108c28) */
    v->voice = audio_play(&v->wave, 1.0f, 0);
    if (v->voice) fprintf(stderr, "voice %s\n", name);
    return v->voice;
}

static int voice_busy(void *ctx, int h) { (void)ctx; return audio_playing(h); }

/* the faction's letter in a voice name (0x10067cfc / 0x100680d0): a name holding "avon" gets 's' as its fourth
 * letter for a South player, one holding "avos" 'n' for a North one */
static void voice_side(char *name, int faction)
{
    if (strlen(name) < 4) return;
    if (faction == 1 && strstr(name, "avon")) name[3] = 's';
    else if (faction == 0 && strstr(name, "avos")) name[3] = 'n';
}

/* the gear computer's reports (0x100176df): subject (0x1010a4a0, 44 per faction) and verdict (0x1010a3e0, 4) */
static const char *const computer_subject[44] = {
    "IRED", "LOWL", "APLT", "IDTR", "SENS", "CAMR", "ENGE", "ENGE", "ARM_", "LEG_", "GDMC", "FIRE", "TORS", "TORL",
    "HEAD", "COCK", "SATL", "WDED", "MGU_", "ACA_", "LAS_", "PAC_", "RGU_", "FGU_", "HG__", "GLA_", "DPA_", "BZK_",
    "MTR_", "ROC_", "SC__", "MIS_", "INCM", "GRPF", "GDED", "TDED", "VDED", "ADED", "EDED", "SATC", "HYDR", "INTW",
    "POWL", "JJET"};
static const char *const computer_verdict[4] = {"DISE", "ENGA", "DAMG", "DEAD"};

static void computer_say(radio *r, int faction, int subject, int verdict)
{
    char a[16], b[16] = "";
    const char *f = faction ? "AVOS" : "AVON";
    snprintf(a, sizeof a, "%s%s", f, computer_subject[subject]);
    if (verdict >= 0) snprintf(b, sizeof b, "%s%s", f, computer_verdict[verdict]);
    radio_say(r, a, b);
}

static int player_faction;   /* the player gear's +0x613: 0 North, 1 South (the port's player is North) */

/* the player's gear's computer (only once it is running, state 2: 0x1005b5c6 checks +0x4f0) */
static int player_active;

static void player_report(void *ctx, int subject, int verdict)
{
    if (player_active && subject >= 0 && subject < 44) computer_say(ctx, player_faction, subject, verdict);
}

/* the original speaks the objective's voice and shows "<text> completed / failed" for the player's group's
 * objectives (0x10067cfc; the text only for visible ones, the voice always), once each (0x1028fc60) */
static char hud_msg[96];
static Uint64 hud_msg_at;
static radio *ob_radio;
static unsigned char ob_said[64];

static void ob_announce(void *ctx, int group, int k, int state)
{
    battle *b = ctx;
    const mission *m = world_mission(b->w);
    if (group != 0 || k >= m->mtbl[0].nobj || k >= 64 || ob_said[k]) return;
    if (state != OBJ_DONE && state != OBJ_FAILED) return;
    ob_said[k] = 1;
    const mission_objective *o = &m->mtbl[0].obj[k];
    if (o->visible && o->text[0] && o->text[0] != ' ') {
        snprintf(hud_msg, sizeof hud_msg, "%s %s", o->text, state == OBJ_DONE ? "completed" : "failed");
        hud_msg_at = SDL_GetTicks();
        fprintf(stderr, "%s\n", hud_msg);
    }
    char name[16];
    snprintf(name, sizeof name, "%s", state == OBJ_DONE ? o->ok_voice : o->fail_voice);
    voice_side(name, player_faction);
    if (ob_radio && name[0]) radio_say(ob_radio, name, NULL);
}

/* the shots' sounds: a shot's sound leaves with it (0x100ba550: the player's own flat in the cockpit, others placed
 * and 10 dB down in the cockpit view); where it strikes, its effect's sound (0x1001b3a1) */


static void snd_fire(void *ctx, int owner, int type, const float at[3])
{
    (void)ctx;
    if (owner == snd_player) stat.shots++;
    const gear_weapon_stats *st = &gear_weapon_stat[type];
    /* the muzzle flash (0x100ba550: the weapon's +0x18; the original fixes it to the weapon on the vehicle, here at
     * the shot's start) */
    effects_start(&fx, st->flash, at[0], at[1], at[2], -1);
    if (!(st->flags & 3) || !st->sound[0]) return;
    if (owner == snd_player && snd_cockpit) sfx_flat(st->sound, 0, 0, st->flags & 2 ? 0x432 : 0x32);
    else sfx_at(st->sound, at[0], at[1], at[2], snd_cockpit, st->flags & 2 ? 0x432 : 0x32);
}

static void snd_impact(void *ctx, int owner, int type, int victim, int pen, const float at[3])
{
    battle *b = ctx;
    unsigned fl = 0;
    if (victim >= 0 && victim == snd_player) stat.taken++;
    /* a vehicle's own hit sound (0x1005c1f8): pierced, the weapon's +0xc2; deflected, AFXWISTB (the player's: AFXWISTB
     * or AFXWISHR at -20 dB). The player's flat at priority 0x50, others placed at twice their offset, priority 10 */
    if (pen >= 0) {
        const char *hs = pen ? gear_weapon_stat[type].hit_sound : "AFXWISTB";
        if (victim == snd_player) {
            if (pen) sfx_flat(hs, 0, 0, 0x50);
            else sfx_flat(rand() % 2 ? "AFXWISTB" : "AFXWISHR", -2000, 0, 0x50);
        } else {
            const unit *u = &b->us->u[victim];
            float lx, ly, lz;
            sfx_listener_pos(&lx, &ly, &lz);
            sfx_at(hs, lx - 2 * (lx - u->s.x), ly - 2 * (ly - u->s.y), lz - 2 * (lz - u->s.z), snd_cockpit, 10);
        }
    }
    if (victim >= 0 && victim < b->us->n) {
        fl |= COMBAT_HIT_VEHICLE;
        if (victim == snd_player) fl |= COMBAT_HIT_PLAYER;
    }
    if (victim < 0 || victim >= STATIC_ID) fl |= COMBAT_HIT_GROUND;   /* INFERRED: what the original flags 0x800 */
    int kind = gear_weapon_stat[type].kind;
    int e = combat_effect(kind, fl, snd_cockpit);
    /* a structure brought down (0x1001cc6c): its explosion - 0x32, the big building one (the GT's flags that pick 0x24
     * or 0x16 instead aren't kept: INFERRED) */
    fx.owner = owner;
    if (victim >= STRUCT_ID && victim == struct_down) {
        struct_down = -1;
        fx_play(0x32, at[0], at[1], at[2], -1, 0);
    }
    /* the effect, and only if it started its sound; quiet unless the player was struck (INFERRED: 0x101316ec read as
     * the cockpit view) */
    if (e >= 0)
        fx_play(e, at[0], at[1] - combat_effect_drop(kind, fl, snd_cockpit), at[2], (fl & COMBAT_HIT_VEHICLE) ? victim : -1,
                (fl & COMBAT_HIT_PLAYER) != 0);
    fx.owner = -2;
}

static void ob_nav_reached(void *ctx, int nav)
{
    (void)ctx; (void)nav;
    sfx_flat("AFXHDSAT", -2500, 0, 0x50);
}

/* the squad's slot n (0x1000edfa: the vehicle of the player's group with +0x10 == n; here its GPS order, INFERRED) */
static int squad_slot(const units *us, int i)
{
    if (snd_player < 0 || i < 0 || i >= us->n || us->u[i].player || us->u[i].group != us->u[snd_player].group) return -1;
    return us->u[i].slot;
}

static void on_killed(void *ctx, int victim, int owner)
{
    battle *b = ctx;
    if (victim < 0 || victim >= b->us->n) return;
    int vs = squad_slot(b->us, victim), k = b->us->u[victim].gm.vclass;
    if (vs == 1 || vs == 2) { stat_mates_lost++; stat_slot_lost[vs] = 1; }
    if (owner < 0 || owner >= b->us->n || b->us->u[victim].side != 1) return;
    int os = squad_slot(b->us, owner);
    if (owner == snd_player && (k == 1 || k == 2 || k == 3 || k == 4 || k == 5 || k == 8 || k == 10)) stat.kills++;
    /* (the original also asks the victim's +0x18 & 0x1400, not traced: taken as set) */
    else if ((os == 1 || os == 2) && (k == 1 || k == 2 || k == 3 || k == 8 || k == 10)) stat.mate_kills++;
}

typedef struct { const gearmodel *gm; const sim_gear *g; } mount_ctx;

static void gear_mount(void *ctx, int kind, float from[3])
{
    const mount_ctx *m = ctx;
    gearmodel_mount(m->gm, m->g->x, m->g->y, m->g->z, m->g->heading, kind, from);
}

static float ground_at(void *ctx, float x, float z)
{
    int found;
    return world_ground_height((const world *)ctx, x, z, &found);
}

/* the HUD's targeting (the sim's INPUTW.MAP: T next enemy, Shift+T previous, F next friend, Q the one under the
 * reticle; N / Shift+N the navs). The original keeps the target in the vehicle's +0xec and the nav in +0x114
 * (none at the start). The order the cycling takes (here: the vehicle table's) isn't traced. */
static int next_target(const units *us, int pi, int cur, int dir, int friendly)
{
    int n = us->n;
    for (int k = 1; k <= n; k++) {
        int i = ((cur < 0 ? (dir > 0 ? -1 : 0) : cur) + dir * k + n * 4) % n;
        const unit *u = &us->u[i];
        if (i == pi || u->cb.dead || !u->placed) continue;
        if ((u->side == us->u[pi].side) != friendly) continue;
        return i;
    }
    return -1;
}

static int next_nav(const mission *m, int cur, int dir)
{
    int n = m->nnav;
    for (int k = 1; k <= n; k++) {
        int i = ((cur < 0 ? (dir > 0 ? -1 : 0) : cur) + dir * k + n * 4) % n;
        if (m->nav[i].group == 0 && m->nav[i].visit) return i;   /* the player's group's shown navs (0x100889c9) */
    }
    return -1;
}

/* the player's n-th squad mate (1..6, 0x1000edfa): the player's group's other live vehicles in their order */
static int squad_mate(const units *us, int n)
{
    for (int i = 0; i < us->n; i++) {
        const unit *u = &us->u[i];
        if (u->player || u->group != 0 || !u->placed || u->cb.dead) continue;
        if (--n == 0) return i;
    }
    return -1;
}

/* a squad mate's reply (0x100173e1): line `line` of its voice set, the set from its record's voice (0x10017766,
 * src/squad.h); slot 4 and those past it have none. Line 0 alternates between the set's first two (not used by the
 * orders) */
static void mate_reply(radio *rd, int slot, int voice, int line)
{
    if (slot < 1 || slot > 4 || slot == 4 || line < 0 || line > 9) return;   /* 0x10017766 skips slot 4 */
    int set = squad_voice_set(voice);
    if (set < 0 || set >= 20) return;
    char nm[12];
    snprintf(nm, sizeof nm, "%s", gear_voice_set[set][line]);
    for (char *c = nm; *c; c++) *c = (char)tolower((unsigned char)*c);
    radio_say(rd, nm, NULL);
}

/* an order from a squad mate channel (0x1000f2b0, the items' codes 2, 0xd, 7, 0xe, 0xf, 5): whether it takes it and
 * its reply (0x10017566: accepted attack line 2, flank 4, defend 3, scout 1, cover 4 (none found: 9), formation 7;
 * refused 8) */
static void mate_order(units *us, radio *rd, int m, int slot, int item, int tgt, int nav, const mission *msn)
{
    unit *u = &us->u[m];
    int ok = 0, line = 8, has = tgt >= 0 && tgt < us->n && !us->u[tgt].cb.dead;
    int hostile = has && us->u[tgt].side != u->side;
    switch (item) {
    case 0:   /* attack (2): a target that is neither the mate nor friendly; none at all: nothing said */
        if (!has) return;
        ok = hostile; line = 2;
        break;
    case 1:   /* flank (0xd): a vehicle other than the player */
        ok = has && !us->u[tgt].player; line = 4;
        break;
    case 2:   /* defend (7): any target but the mate itself */
        ok = has && tgt != m; line = 3;
        break;
    case 3:   /* scout (0xe): the player's selected nav */
        ok = nav >= 0 && nav < msn->nnav; line = 1;
        if (ok) { u->order_x = msn->nav[nav].x; u->order_z = msn->nav[nav].z; }
        break;
    case 4: { /* get to cover (0xf): from the nearest hostile (the cover search, 0x100279a3, isn't traced: INFERRED
               * found whenever there is a hostile) */
        int any = 0;
        for (int j = 0; j < us->n; j++) if (!us->u[j].cb.dead && us->u[j].side != u->side && us->u[j].placed) any = 1;
        ok = any; line = any ? 4 : 9;
        break;
    }
    case 5:   /* return to formation (5) */
        ok = 1; line = 7;
        break;
    }
    if (ok) {
        static const int ord[6] = {UNIT_ORDER_ATTACK, UNIT_ORDER_FLANK, UNIT_ORDER_DEFEND, UNIT_ORDER_SCOUT,
                                   UNIT_ORDER_COVER, UNIT_ORDER_FORM};
        u->order = ord[item];
        u->order_target = tgt;
    } else if (line != 9) {
        line = 8;
    }
    mate_reply(rd, slot, u->voice, line);
}

static float bearing(float fx, float fz, float tx, float tz, float heading)
{
    float b = SDL_atan2f(tx - fx, tz - fz) * 180.0f / 3.14159265f - heading;
    while (b > 180) b -= 360;
    while (b < -180) b += 360;
    return b;
}

/* play a mission until Esc (failed) or F12 (accomplished); 0 / 1 to go back to the shell, -1 to quit.
 * The player's gear: keys 1-9 and 0 set the throttle (1 stop .. 0 full, as the original's), W / S and Up / Down
 * step it, Left / Right (or A / D) turn, a mouse drag looks up and down; Space fires the selected weapon, Enter picks
 * the next (KEYBOARD.MAP). F2 toggles a free camera (the viewer's). */
static int run_mission(SDL_Renderer *ren, SDL_Texture *tx, hg_archive *a, const char *game, const char *scene,
                       const squad *sq, const char *player_gdf)
{
    char err[256];
    world *w = world_load(a, scene, err, sizeof err);
    if (!w) { fprintf(stderr, "%s\n", err); return 0; }
    load_skygnd(w, game);
    /* the mission's CD track, looping (HGSIM 0x10015cdc / 0x10015e1a) */
    if (world_music(w) > 0 && audio_music_track(game, world_music(w)))
        fprintf(stderr, "music track %d not found (wavmap.ini, music/, or a CD image in the game folder)\n", world_music(w));
    static units us;
    units_spawn(&us, w, a);
    static combat_shots shots;
    memset(&shots, 0, sizeof shots);
    static structures sts;
    structures_init(&sts, w);
    /* the structures have their own test; the rest of the mission's objects stop shells by their polygons */
    for (int i = 0; i < sts.n; i++) { world_set_solid(w, sts.s[i].inst, 0); world_set_solid(w, sts.s[i].wreck, 0); }
    battle bt = {w, &us, &sts};
    fx_battle = &bt;
    {
        effects_ops fo = {w, fx_add, fx_set, fx_vehicle_pos, fx_ground, fx_radius, fx_turn, fx_splash};
        effects_init(&fx, &fo, (const char (*)[17])world_mission(w)->fx_model, world_mission(w)->fx_count);
    }
    combat_world cw = {&bt, battle_hit, battle_ground, battle_victim, battle_hit_other, battle_hit_vehicle, battle_target_pos};
    /* the objectives; the player's GPS record would be USERSTAR's, flags 0x4006 (the shell's, 0x100ff1e6) */
    static objectives obj;
    objectives_init(&obj, world_mission(w), -1, 0x4007);
    hud_msg[0] = 0;
    memset(ob_said, 0, sizeof ob_said);
    static voices vo;
    vo.a = a;
    sfx_init(a);
    shots.ev.ctx = &bt;
    shots.ev.fire = snd_fire;
    shots.ev.impact = snd_impact;
    shots.ev.killed = on_killed;
    memset(&stat, 0, sizeof stat);
    stat_mates_lost = 0;
    memset(stat_slot_lost, 0, sizeof stat_slot_lost);
    static radio rd;
    radio_ops rops = {voice_start, voice_busy, voice_exists, &vo};
    radio_init(&rd, &rops);
    ob_radio = &rd;
    static unsigned char was_dead[UNITS_MAX];
    memset(was_dead, 0, sizeof was_dead);
    obj_world ow = {&bt, ob_vehicle_dead, ob_structure_dead, ob_leader, ob_hit_by, ob_announce, ob_nav_reached};
    int has_mtbl = world_mission(w)->mtbl[0].present, outcome = GROUP_RUNNING;
    long sim_ticks = 0;
    camera cam;
    world_start_camera(w, &cam);
    /* the gear under the cockpit eye: the Hunter's 171.3 above its origin, 48.3 ahead, standing 245.3 (MEASURED,
     * RE_NOTES "The cockpit view"); another gear's from its skeleton: its EYE_ point (the Hunter's 155.6 up, 48.4
     * ahead) raised by the Hunter's difference, 15.7 (INFERRED), and its soles' depth */
    const char *pgdf = player_gdf && player_gdf[0] ? player_gdf : "gdhuntea";
    float eye_up = 171.3f, eye_fwd = 48.3f, stand = 245.3f;
    const float rad = 3.14159265f / 180;
    {
        gearmodel pm;
        if (strcmp(pgdf, "gdhuntea") && !gearmodel_load(a, pgdf, &pm)) {
            if (pm.found_eye) { eye_up = pm.eye[1] + 15.7f; eye_fwd = pm.eye[2]; }
            if (pm.stand > 0) stand = pm.stand;
        }
    }
    sim_gear pg;
    /* the start (world_start_camera puts the Hunter's eye there) */
    float gx = cam.x - 48.3f * SDL_sinf(cam.yaw * rad), gz = cam.z - 48.3f * SDL_cosf(cam.yaw * rad), gy = cam.y - 171.3f - 245.3f;
    cam.x = gx + eye_fwd * SDL_sinf(cam.yaw * rad); cam.z = gz + eye_fwd * SDL_cosf(cam.yaw * rad); cam.y = gy + stand + eye_up;
    sim_gear_init(&pg, gx, gy + stand, gz, cam.yaw, SIM_HUNTER_TOP_SPEED, stand);
    /* the player's gear (the game's, HGSHELL record +0x14 -> USERGEAR; the port's Hunter by default), on group 0's side */
    int pi = units_add_player(&us, w, a, pgdf, world_mission(w)->group_side[0], 6.0f, &pg);
    /* the wingmen (the launch's USERSTAR records, HGSHELL 0x100ff1e6): squad slots 1 and 2 */
    for (int i = 0; sq && i < 2; i++)
        if (sq->pilot[i] >= 0 && sq->gear[i][0] && units_add_mate(&us, w, a, sq->gear[i], i + 1, sq->voice[i]) < 0)
            fprintf(stderr, "wingman %d: gear %s not loaded\n", i + 1, sq->gear[i]);
    /* the design's top speed (0x1004e6a0: speed points x 47.1744; the Hunter's 7 give the MEASURED 330.22) */
    if (pi >= 0 && us.u[pi].cb.gr.speed_points > 0) pg.top_speed = us.u[pi].s.top_speed = us.u[pi].cb.gr.speed_points * 47.1744f;
    static combatant nobody;
    combatant *pcp = pi >= 0 ? &us.u[pi].cb : &nobody;
    mount_ctx mc = {pi >= 0 ? &us.u[pi].gm : NULL, &pg};
    hud *hd = hud_load(a, 0);
    gear g;
    if (hd && !gear_load(a, pgdf, &g)) hud_set_gear(hd, a, &g, 0);
    raster_target rt;
    raster_init(&rt, 640, 480);
    int quit = 0, sw = 0, show_hud = 1, result = 0, free_cam = 0, trig_was = 0, started = 0, menu = 0, show_obj = 0;
    snd_player = pi;
    combat_seeker seeker = {0, 0, 0, 0};
    int tone_lock = 0, tone_acq = 0;
    long player_start = 0x43e + rand() % 0x16a;   /* 0x100620b8 state 1: 0x43e ticks and a random 0..0x169 */
    player_active = 0;
    pcp->report = player_report;
    pcp->report_ctx = &rd;
    float throttle = 0, pitch = 0, tick_ms = 0;
    int tgt = -1, nav = -1, frame = 0;
    float aim_pt[3] = {0, 0, 0};
    Uint64 last = SDL_GetTicks(), start = last;
    long end_at = 0;
    for (int run = 1; run;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) { run = 0; quit = 1; }
            if (e.type == SDL_EVENT_KEY_DOWN) {
                SDL_Keycode kc = e.key.key;
                int shift = (e.key.mod & SDL_KMOD_SHIFT) != 0, ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
                /* the radio menus (GAMEKEY.MAP: F1 MISSION_CHANNEL_MENU, F2..F7 SQUADMATE1..6_CHANNEL_MENU): a key opens
                 * its menu (AFXHDMSB) or closes it; F2.. with no such squad mate beeps (AFXHDWPS). While a menu is open
                 * the digits pick its items and Esc closes it (0x10063231: keys '0'..'9', Esc 0x1b) */
                if (kc >= SDLK_F1 && kc <= SDLK_F7 && !ctrl) {
                    int want = 1 + (int)(kc - SDLK_F1);
                    if (menu == want) menu = 0;
                    else if (want == 1 || squad_mate(&us, want - 1) >= 0) { menu = want; sfx_flat("AFXHDMSB", 0, 0, 0x32); }
                    else sfx_flat("AFXHDWPS", 0, 0, 0x32);
                    continue;
                }
                if (menu && kc == SDLK_ESCAPE) { menu = 0; continue; }
                if (menu && kc >= SDLK_1 && kc <= SDLK_9) {
                    int item = (int)(kc - SDLK_1);
                    if (menu == 1 && item < 3) {
                        int k = objectives_radio_slot(&obj, 0, item);
                        if (k >= 0) objectives_call(&obj, 0, k);   /* 0x10009d0e: the objective becomes a 0x10000 */
                    } else if (menu > 1 && item < 6) {
                        int m = squad_mate(&us, menu - 1);
                        if (m >= 0) mate_order(&us, &rd, m, menu - 1, item, tgt, nav, world_mission(w));
                    }
                    sfx_flat("AFXHDWPS", 0, 0, 0x32);
                    menu = 0;
                    continue;
                }
                /* F12 DISPLAY_OBJECTIVES (0x1003b3ba command 0x59) toggles the objectives display (window 13) */
                if (kc == SDLK_F12 && !ctrl) { show_obj = !show_obj; continue; }
                if (kc == SDLK_ESCAPE) run = 0;
                if (kc == SDLK_F12 && ctrl) { run = 0; result = 1; }
                if (kc == SDLK_L) sw = world_set_software(w, !sw);
                if (kc == SDLK_H) show_hud = !show_hud;
                if (kc == SDLK_GRAVE) free_cam = !free_cam;
                if (kc == SDLK_T && pi >= 0) tgt = next_target(&us, pi, tgt, shift ? -1 : 1, 0);
                if (kc == SDLK_F && pi >= 0) tgt = next_target(&us, pi, tgt, 1, 1);
                if (kc == SDLK_Q && !free_cam && pi >= 0) {
                    float hr = pg.heading * rad, pr = pitch * rad, t1 = 1;
                    float eye[3] = {pg.x, pg.y + eye_up, pg.z};
                    float far_[3] = {eye[0] + SDL_sinf(hr) * SDL_cosf(pr) * 150000, eye[1] + SDL_sinf(pr) * 150000,
                                     eye[2] + SDL_cosf(hr) * SDL_cosf(pr) * 150000};
                    int z0, v = units_hit(&us, w, pi, eye, far_, &t1, &z0);
                    if (v >= 0) tgt = v;
                }
                if (kc == SDLK_N) nav = next_nav(world_mission(w), nav, shift ? -1 : 1);
                /* weapon_cycle: Enter (KEYBOARD.MAP) */
                if (kc == SDLK_RETURN && hd) {
                    combat_cycle(pcp);
                    sfx_flat("AFXHDWPS", 0, 0, 0x32);   /* 0x100620b8 */
                    for (int i = 0; i < g.nweapons; i++) g.weapon[i].ammo = pcp->ammo[i];
                    hud_set_gear(hd, a, &g, pcp->sel);
                }
                /* throttle keys: 1 = 0, 2..9 = 1/9 .. 8/9, 0 = full (MEASURED in the original: the control's
                 * throttle reads 4/9 x 1/64 after 5, 8/9 x 1/64 after 9, 1/64 after 0) */
                if (kc >= SDLK_1 && kc <= SDLK_9) throttle = (float)(kc - SDLK_1) / 9.0f;
                if (kc == SDLK_0) throttle = 1.0f;
                if (kc == SDLK_W || kc == SDLK_UP) throttle = SDL_min(1.0f, throttle + 1.0f / 9.0f);
                if (kc == SDLK_S || kc == SDLK_DOWN) throttle = SDL_max(0.0f, throttle - 1.0f / 9.0f);
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
                if (free_cam) cam.yaw += e.motion.xrel * 0.25f;
                pitch -= e.motion.yrel * 0.25f;
                if (pitch > 89) pitch = 89;
                if (pitch < -89) pitch = -89;
            }
        }
        Uint64 now = SDL_GetTicks();
        float dt = (float)(now - last) / 1000.0f;
        last = now;
        const bool *k = SDL_GetKeyboardState(NULL);
        if (free_cam) {
            float sp = 3000.0f * dt, fx = SDL_sinf(cam.yaw * rad), fz = SDL_cosf(cam.yaw * rad);
            if (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) { cam.x += fx * sp; cam.z += fz * sp; }
            if (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) { cam.x -= fx * sp; cam.z -= fz * sp; }
            if (k[SDL_SCANCODE_LEFT]) cam.yaw -= 90 * dt;
            if (k[SDL_SCANCODE_RIGHT]) cam.yaw += 90 * dt;
            if (k[SDL_SCANCODE_Q]) cam.y -= sp;
            if (k[SDL_SCANCODE_E]) cam.y += sp;
            cam.pitch = pitch;
        } else {
            float turn = (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D] ? 1.0f : 0.0f) -
                         (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A] ? 1.0f : 0.0f);
            /* the sim's 5 ms ticks */
            tick_ms += dt * 1000.0f;
            /* weapon_fire: Space (KEYBOARD.MAP). The aim: along the view to the first unit on it, else 150000 ahead
             * (0x100bcf44 makes a 150000 ray; the aim distance, the vehicle's +0xb0, its setter not traced) */
            int trig = k[SDL_SCANCODE_SPACE];
            /* the trigger on an empty weapon (0x100ba550): AFXWPMTY at -10 dB */
            if (trig && !trig_was && pcp->state[pcp->sel] < 0 && !pcp->dead) sfx_flat("AFXWPMTY", -1000, 0, 0x32);
            trig_was = trig;
            /* the effects' harm once a frame, for the frame's ticks (0x1001c40d: dt x power, truncated) */
            if (tick_ms >= SIM_TICK_MS) effects_splash(&fx, (int)(tick_ms / SIM_TICK_MS));
            for (; tick_ms >= SIM_TICK_MS; tick_ms -= SIM_TICK_MS) {
                /* the gear moves once its engine runs (0x10060491 zeroes the speeds below state 2) */
                sim_gear_tick(&pg, started == 2 ? throttle * pcp->speed_factor : 0, started == 2 ? turn : 0, ground_at, w);
                float hr = pg.heading * rad, pr = pitch * rad;
                float eye[3] = {pg.x + eye_fwd * SDL_sinf(hr), pg.y + eye_up, pg.z + eye_fwd * SDL_cosf(hr)};
                float dir[3] = {SDL_sinf(hr) * SDL_cosf(pr), SDL_sinf(pr), SDL_cosf(hr) * SDL_cosf(pr)};
                float far_[3] = {eye[0] + dir[0] * 150000, eye[1] + dir[1] * 150000, eye[2] + dir[2] * 150000};
                float at = 1, aim[3];
                int az;
                if (units_hit(&us, w, pi, eye, far_, &at, &az) < 0) at = 1;
                for (int q = 0; q < 3; q++) aim[q] = eye[q] + (far_[q] - eye[q]) * at;
                memcpy(aim_pt, aim, sizeof aim_pt);
                int before = pcp->ammo[pcp->sel];
                if (pi >= 0) us.u[pi].s = pg;
                /* the seeker (0x100bc861): guided shots home only when fired locked; the tones (0x10021a8d, cockpit
                 * view, the engine running): AFXHDMLD as it starts acquiring, AFXHDMLG once locked (the slot ready) */
                {
                    int sl = pcp->sel, ty = sl < pcp->gr.nweapons ? pcp->gr.weapon[sl].type : -1;
                    float d = 0, ye = 0, pe = 0;
                    int has = tgt >= 0 && tgt < us.n && !us.u[tgt].cb.dead;
                    if (has) {
                        const unit *t = &us.u[tgt];
                        float dx = t->s.x - pg.x, dy = t->s.y - pg.y, dz = t->s.z - pg.z;
                        d = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
                        ye = bearing(pg.x, pg.z, t->s.x, t->s.z, pg.heading);
                        pe = SDL_atan2f(dy - eye_up, SDL_sqrtf(dx * dx + dz * dz)) * 180.0f / 3.14159265f - pitch;
                    }
                    combat_seeker_tick(&seeker, ty, has, d, ye, pe, 0, 1);
                    pcp->lock = seeker.locked ? tgt : -1;
                    if (!seeker.locked) {
                        tone_lock = 0;
                        if (!seeker.acquiring) tone_acq = 0;
                        else if (!tone_acq && started == 2 && snd_cockpit) { tone_acq = 1; sfx_flat("AFXHDMLD", 0, -5000, 0x32); }
                    } else if (!tone_lock && sl < pcp->gr.nweapons && pcp->state[sl] == 1 && started == 2 && snd_cockpit) {
                        tone_lock = 1;
                        sfx_flat("AFXHDMLG", 0, 5000, 0x32);
                    }
                }
                combat_weapons_tick(pcp, pi, trig, eye, aim, mc.gm ? gear_mount : NULL, &mc, &shots);
                if (pcp->ammo[pcp->sel] != before && hd) {
                    for (int i = 0; i < g.nweapons; i++) g.weapon[i].ammo = pcp->ammo[i];
                    hud_set_gear(hd, a, &g, pcp->sel);
                }
                /* each group's goal: the first nav of its current objective when that is a go-to (0x100) */
                units_goal goals[16];
                memset(goals, 0, sizeof goals);
                for (int g = 0; has_mtbl && g < 16; g++) {
                    int k = obj.current[g];
                    if (k < 0 || world_mission(w)->mtbl[g].obj[k].type != 0x100) continue;
                    for (int q = 0; q < obj.st[g][k].ntgt; q++) {
                        if (obj.st[g][k].tgt[q].type != OBJT_NAV) continue;
                        const mission_nav *v = &world_mission(w)->nav[obj.st[g][k].tgt[q].idx];
                        goals[g].go = 1; goals[g].x = v->x; goals[g].y = v->y; goals[g].z = v->z;
                        break;
                    }
                }
                units_ai_tick(&us, w, goals, &shots);
                combat_shots_tick(&shots, &cw);
                effects_tick(&fx, 1);
                sim_ticks++;
                if (has_mtbl && outcome == GROUP_RUNNING) {
                    outcome = objectives_tick(&obj, (int)sim_ticks, &ow, 0);
                    if (outcome != GROUP_RUNNING) {
                        fprintf(stderr, "mission %s\n", outcome == GROUP_WON ? "accomplished" : "failed");
                        /* the group's result voice (0x100680d0): won the MTBL's +0xa, lost its +0x15, out of
                         * time "BET68" (not in the archive: silent) */
                        const mission_mtbl *mt = &world_mission(w)->mtbl[0];
                        char vn[16];
                        snprintf(vn, sizeof vn, "%s", outcome == GROUP_WON ? mt->ok_voice :
                                 outcome == GROUP_LOST ? mt->fail_voice : "BET68");
                        voice_side(vn, player_faction);
                        radio_say(&rd, vn, NULL);
                        if (!end_at) end_at = sim_ticks + 0x16a;
                        result = outcome == GROUP_WON;
                    }
                }
                /* the player's gear destroyed (0x100620b8 sets 0x10148e1c): the same end (its outside view isn't done) */
                if (pcp->dead && !end_at) end_at = sim_ticks + 0x16a;
                /* an enemy vehicle destroyed: the computer names its kind (0x10059e9d: classes 1 gear, 3, 4 (2
                 * reads as 4) vehicle, 5 / 8 aircraft, 10; infantry say nothing) */
                for (int i = 0; i < us.n && i < UNITS_MAX && pi >= 0; i++) {
                    unit *u = &us.u[i];
                    if (!u->cb.dead || was_dead[i]) continue;
                    was_dead[i] = 1;
                    /* its death (state 4): a gear's (0x10060491 -> 0x100533d2) the gear death effect, then 0x712
                     * ticks of burning, debris among it; other vehicles' (0x1002b87a) the burning alone */
                    if (u->gm.vclass == 1) fx_play(0x24, u->s.x, u->s.y, u->s.z, -1, 0);
                    effects_wreck(&fx, u->s.x, u->s.y, u->s.z);
                    if (u->side == us.u[pi].side || u->side >= 2) continue;
                    int c = u->gm.vclass == 2 ? 4 : u->gm.vclass;
                    int subj = c == 1 ? 0x22 : c == 3 ? 0x23 : c == 4 ? 0x24 : c == 5 || c == 8 ? 0x25 : c == 10 ? 0x26 : -1;
                    if (subj >= 0) computer_say(&rd, player_faction, subj, -1);
                }
            }
            /* the mission's end (0x1003ad72): once the player's group is decided (0x10131968) or its gear is gone,
             * "Exiting Mission Area..." shows and the sim ends 0x16a ticks later (INFERRED: the port lets the last
             * voice finish) */
            radio_tick(&rd);
            if (end_at) {
                snprintf(hud_msg, sizeof hud_msg, "Exiting Mission Area...");
                hud_msg_at = SDL_GetTicks();
                if (sim_ticks >= end_at && !radio_active(&rd)) run = 0;
            }
            cam.yaw = pg.heading;
            cam.pitch = pitch;
            cam.x = pg.x + eye_fwd * SDL_sinf(pg.heading * rad);
            cam.z = pg.z + eye_fwd * SDL_cosf(pg.heading * rad);
            cam.y = pg.y + eye_up;
            /* the player's gear starts up (0x100620b8 state 0: AFXHGENG at -20 dB, then after 0x43e ticks and a random
             * part, state 2 and AFXHBEEP; till then it neither moves nor turns) */
            sfx_listener(cam.x, cam.y, cam.z, cam.yaw);
            snd_cockpit = !free_cam;
            if (!started) { started = 1; sfx_flat("AFXHGENG", -2000, 0, 0x32); }
            if (started == 1 && sim_ticks > player_start) { started = 2; player_active = 1; obj.player_running = 1; sfx_flat("AFXHBEEP", 0, 0, 0x32); }
        }
        units_update(&us, w);
        world_set_time(w, (float)(now - start) / 1000.0f);
        world_draw(w, &rt, &cam);
        {
            static float st[COMBAT_MAX_SHOTS][6];
            int ns = 0;
            for (int i = 0; i < COMBAT_MAX_SHOTS; i++) {
                const combat_shot *p = &shots.shot[i];
                if (!p->active) continue;
                float tail = 4.0f;   /* ticks of travel the streak spans: INFERRED */
                st[ns][0] = p->x; st[ns][1] = p->y; st[ns][2] = p->z;
                st[ns][3] = p->x - p->vx * tail; st[ns][4] = p->y - p->vy * tail; st[ns][5] = p->z - p->vz * tail;
                ns++;
            }
            world_draw_streaks(w, &rt, &cam, (const float (*)[6])st, ns, 0xffe080);
        }
        if (show_hud && hd) {
            static hud_state hs;
            const mission *msn = world_mission(w);
            hud_state_simple(&hs, free_cam ? cam.yaw : pg.heading, 0);
            /* the speed readout: |velocity| (units per tick) x 6 x 1.38 (0x100b336b) */
            hs.kph = SDL_sqrtf(pg.vx * pg.vx + pg.vz * pg.vz) * 6.0f * 1.38f;
            hs.throttle = throttle;
            hs.frame = frame++;
            hs.armor = pcp->armor; hs.internal = pcp->internal; hs.ztotal = pcp->ztotal;
            hs.selected = pcp->sel;
            for (int i = 0; i < GEAR_MAX_WEAPONS; i++) {
                hs.ammo[i] = pcp->ammo[i] > 0 ? pcp->ammo[i] : 0;
                hs.gone[i] = i < pcp->gr.nweapons && pcp->zone_gone[pcp->gr.weapon[i].kind > 0 &&
                             pcp->gr.weapon[i].kind <= GEAR_ZONES ? pcp->gr.weapon[i].kind - 1 : 0] &&
                             pcp->gr.weapon[i].kind >= 9;
            }
            /* the message box: the player's group's first shown, open, mandatory objective (0x100b1c8f); its label by
             * the objective's order byte: 1 "Primary: ", 8 "Return: ", else "Secondary: " */
            if (has_mtbl)
                for (int k = 0; k < msn->mtbl[0].nobj && k < 64; k++) {
                    const mission_objective *ob = &msn->mtbl[0].obj[k];
                    int stt = obj.st[0][k].state;
                    if (!obj.st[0][k].visible || stt == OBJ_DONE || stt == OBJ_FAILED || !ob->must) continue;
                    hs.obj_label = ob->order == 1 ? "Primary: " : ob->order == 8 ? "Return: " : "Secondary: ";
                    hs.obj_text = ob->text;
                    break;
                }
            /* the opened message box: a radio menu or the objectives (INFERRED layout: the original's menus are its own
             * windows, not traced) */
            static char boxbuf[HUD_BOX_LINES][80];
            if (menu == 1) {
                hs.box_open = 1;
                hs.box[0].text = "MISSION CHANNEL"; hs.box[0].colour = 0x0f; hs.box[0].label = NULL;
                for (int sl = 0; sl < 3; sl++) {
                    int k = has_mtbl ? objectives_radio_slot(&obj, 0, sl) : -1;
                    snprintf(boxbuf[sl], sizeof boxbuf[sl], "%d. %s", sl + 1, k >= 0 ? msn->mtbl[0].obj[k].text : "NO MESSAGE");
                    hs.box[1 + sl].label = NULL; hs.box[1 + sl].text = boxbuf[sl]; hs.box[1 + sl].colour = 0x0c;
                }
                hs.nbox = 4;
            } else if (menu > 1) {
                /* the squad mate channels' menus (0x101063b4 ..: six items and "(Esc to exit)") */
                static const char *const orders[6] = {"Attack My Target", "Flank my target", "Defend my target",
                    "Scout my target", "Get to cover", "Return to Formation"};
                hs.box_open = 1;
                snprintf(boxbuf[0], sizeof boxbuf[0], "SQUAD MATE %d CHANNEL", menu - 1);
                hs.box[0].label = NULL; hs.box[0].text = boxbuf[0]; hs.box[0].colour = 0x0f;
                for (int i = 0; i < 6; i++) {
                    snprintf(boxbuf[1 + i], sizeof boxbuf[1 + i], "%d. %s", i + 1, orders[i]);
                    hs.box[1 + i].label = NULL; hs.box[1 + i].text = boxbuf[1 + i]; hs.box[1 + i].colour = 0x0c;
                }
                hs.nbox = 7;
            }
            if (show_obj && has_mtbl && !menu) {
                hs.objlist = 1;
                hs.nobjl = 0;
                for (int k = 0; k < msn->mtbl[0].nobj && k < 64; k++) {
                    const mission_objective *ob = &msn->mtbl[0].obj[k];
                    unsigned ty = objectives_type(&obj, 0, k);
                    if (!obj.st[0][k].visible || (ty >= 0x8000 && ty <= 0x8002)) continue;
                    hs.objl[hs.nobjl].order = ob->order;
                    hs.objl[hs.nobjl].text = ob->text;
                    hs.objl[hs.nobjl].state = obj.st[0][k].state;
                    hs.nobjl++;
                }
                /* the time line: running, the time elapsed (no limit) or remaining as hh:mm:ss.ss (0x100b13bb); decided,
                 * "Completed at" / "Incompleted at" / "Out of time at" hh:mm:ss (0x100b143b) */
                static char tl[64];
                int gs = obj.gstate[0];
                if (gs == GROUP_RUNNING) {
                    long t = msn->mtbl[0].limit < 1 ? sim_ticks - (long)obj.gstart[0] * 0xb5
                                                    : ((long)msn->mtbl[0].limit + obj.gstart[0]) * 0xb5 - sim_ticks;
                    if (t < 0) t = 0;
                    long hh = t / 0x9f150, mm = (t % 0x9f150) / 0x2a6c;
                    snprintf(tl, sizeof tl, "%s%02ld:%02ld:%05.2f", msn->mtbl[0].limit < 1 ? "Elapsed Time: " : "Time remaining: ",
                             hh, mm, (double)(t % 0x9f150 - mm * 0x2a6c) / 181.0);
                } else {
                    int e = obj.gend[0];
                    snprintf(tl, sizeof tl, "%s%02d:%02d:%02d", gs == GROUP_WON ? "Completed at " : gs == GROUP_LOST ? "Incompleted at " :
                             "Out of time at ", e / 3600, e % 3600 / 60, e % 60);
                }
                hs.time_line = tl;
            }
            /* the reticle at the aim point (0x10055de4), its shape by the selected weapon (0x1005413c) */
            float sx, sy;
            hs.ret_on = !free_cam && world_project(&cam, 640, 480, aim_pt[0], aim_pt[1], aim_pt[2], &sx, &sy);
            hs.ret_x = sx; hs.ret_y = sy;
            if (tgt >= 0 && (tgt >= us.n || us.u[tgt].cb.dead)) tgt = -1;
            {
                int sl = pcp->sel, ty = sl < pcp->gr.nweapons ? pcp->gr.weapon[sl].type : -1;
                if (gear_weapon_kind(ty) < 0) hs.ret_shape = HUD_RET_PROJ_BUSY;
                else {
                    int kind = gear_weapon_kind(ty), ready = pcp->state[sl] == 1;
                    int missile = kind == 0x3e || kind == 0x3f, laser = kind == 0 || kind == 1 || kind == 2;
                    if (!ready) hs.ret_shape = missile || laser ? HUD_RET_PROJ : HUD_RET_PROJ_BUSY;
                    else if (gear_weapon_guided(ty)) hs.ret_shape = seeker.locked ? HUD_RET_MISSILE_LOCK : HUD_RET_MISSILE;
                    else {
                        int locked = 0;
                        if (tgt >= 0) {
                            const unit *t = &us.u[tgt];
                            float dx = t->s.x - pg.x, dy = t->s.y - (pg.y + eye_up), dz = t->s.z - pg.z;
                            float d = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
                            float yaw = bearing(pg.x, pg.z, t->s.x, t->s.z, pg.heading);
                            float pit = SDL_atan2f(dy, SDL_sqrtf(dx * dx + dz * dz)) * 180.0f / 3.14159265f - pitch;
                            locked = d > gear_weapon_min_range(ty) && d < gear_weapon_max_range(ty) &&
                                     SDL_fabsf(yaw) < 3.0f && SDL_fabsf(pit) < 3.0f;
                        }
                        hs.ret_shape = missile ? (locked ? HUD_RET_MISSILE : HUD_RET_MISSILE_LOCK)
                                     : laser ? (locked ? HUD_RET_LASER_ON : HUD_RET_LASER) : HUD_RET_PROJ;
                    }
                }
            }
            /* the target */
            if (tgt >= 0) {
                const unit *t = &us.u[tgt];
                hs.tgt = 1;
                hs.tgt_side = t->side == us.u[pi].side ? 0 : t->side >= 2 ? 2 : 1;
                hs.tgt_bearing = bearing(pg.x, pg.z, t->s.x, t->s.z, pg.heading);
                float cx, cy;
                hs.tgt_in_view = world_project(&cam, 640, 480, t->s.x, t->s.y, t->s.z, &cx, &cy);
                hs.tgt_x = cx; hs.tgt_y = cy;
                /* the box's half size: the view's focal length x the target's standing height / its depth */
                float dx = t->s.x - cam.x, dz = t->s.z - cam.z, depth = SDL_sqrtf(dx * dx + dz * dz);
                hs.tgt_size = depth > 1 ? 320.0f * t->s.stand / depth : 12.0f;
                if (hs.tgt_size < 12.0f) hs.tgt_size = 12.0f;
            }
            /* the nav */
            if (nav >= 0 && nav < msn->nnav) {
                const mission_nav *v = &msn->nav[nav];
                int f;
                float gy = v->y != 0 ? v->y : world_ground_height(w, v->x, v->z, &f);
                hs.nav = 1;
                hs.nav_name = v->name;
                hs.nav_seen = nav < 0x80 && (obj.nav_seen[nav] & 1);
                hs.nav_bearing = bearing(pg.x, pg.z, v->x, v->z, pg.heading);
                float dx = v->x - pg.x, dz = v->z - pg.z;
                hs.nav_range = SDL_sqrtf(dx * dx + dz * dz) / 100.0f;   /* 100 units a metre (0x10101204) */
                float nx, ny;
                hs.nav_in_view = world_project(&cam, 640, 480, v->x, gy, v->z, &nx, &ny);
                hs.nav_x = nx; hs.nav_y = ny;
            }
            /* the radar's contacts, turned into the gear's frame */
            {
                float hr = pg.heading * rad, c = SDL_cosf(hr), sn = SDL_sinf(hr);
                for (int i = 0; i < us.n && hs.ncontacts < HUD_MAX_CONTACTS; i++) {
                    const unit *u = &us.u[i];
                    if (i == pi || u->cb.dead || !u->placed) continue;
                    float dx = u->s.x - pg.x, dz = u->s.z - pg.z;
                    /* an enemy whose stealth beats the player's sensors by k shows only within {750, 500, 300, 150}
                     * [k - 1] x 100 (0x100882b3 / 0x10088182) */
                    if (u->side != us.u[pi].side && u->side < 2) {
                        static const int seen[5] = {0, 750, 500, 300, 150};
                        int k = u->cb.gr.stealth - pcp->gr.sensors;
                        if (k > 4) k = 4;
                        float dy = u->s.y - pg.y, d = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
                        if (k >= 1 && d / 100.0f > (float)seen[k]) continue;
                    }
                    if (i == tgt) hs.tgt_contact = hs.ncontacts;
                    hs.contact[hs.ncontacts].x = dx * c - dz * sn;
                    hs.contact[hs.ncontacts].z = dx * sn + dz * c;
                    hs.contact[hs.ncontacts].kind = u->side == us.u[pi].side ? HUD_RADAR_FRIEND : u->side >= 2 ? HUD_RADAR_NEUTRAL : HUD_RADAR_ENEMY;
                    hs.ncontacts++;
                }
                for (int i = 0; i < msn->nnav && hs.ncontacts < HUD_MAX_CONTACTS; i++) {
                    const mission_nav *v = &msn->nav[i];
                    if (v->group != 0 || !v->visit) continue;
                    float dx = v->x - pg.x, dz = v->z - pg.z;
                    if (i == nav) hs.nav_contact = hs.ncontacts;
                    hs.contact[hs.ncontacts].x = dx * c - dz * sn;
                    hs.contact[hs.ncontacts].z = dx * sn + dz * c;
                    hs.contact[hs.ncontacts].kind = i < 0x80 && (obj.nav_seen[i] & 1) ? HUD_RADAR_NAV_SEEN : HUD_RADAR_NAV;
                    hs.ncontacts++;
                }
            }
            if (hud_msg[0] && now - hud_msg_at < 5000) hs.msg = hud_msg;
            hud_draw(hd, rt.color, rt.w, rt.h, world_palette(w), &hs);
        }
        SDL_UpdateTexture(tx, NULL, rt.color, rt.w * 4);
        SDL_RenderTexture(ren, tx, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    /* the objectives for the debriefing: the player's group's shown ones (as the F12 display picks them) */
    stat_objectives[0] = 0;
    if (has_mtbl)
        for (int k = 0; k < world_mission(w)->mtbl[0].nobj && k < 64; k++) {
            const mission_objective *ob = &world_mission(w)->mtbl[0].obj[k];
            unsigned ty = objectives_type(&obj, 0, k);
            if (!obj.st[0][k].visible || !ob->order || !ob->text[0] || (ty >= 0x8000 && ty <= 0x8002)) continue;
            size_t L = strlen(stat_objectives);
            snprintf(stat_objectives + L, sizeof stat_objectives - L, "%s%s%s\n",
                     ob->order == 1 ? "Primary: " : ob->order == 8 ? "Return: " : "Secondary: ", ob->text,
                     obj.st[0][k].state == OBJ_DONE ? " - Complete" : obj.st[0][k].state == OBJ_FAILED ? " - InComplete" : "");
        }
    audio_music_stop();
    audio_stop(vo.voice);
    vo.voice = 0;
    wave_free(&vo.wave);
    sfx_free();
    ob_radio = NULL;
    raster_free(&rt);
    hud_free(hd);
    world_free(w);
    return quit ? -1 : result;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: heavygear <game folder>\n"); return 2; }
    char err[256];
    hg_archive *a = hga_open(argv[1], err, sizeof err);
    if (!a) { fprintf(stderr, "%s\n", err); return 1; }
    if (!hga_has_patch(a)) {
        const char *base = SDL_GetBasePath();
        char p[1200];
        snprintf(p, sizeof p, "%sgamedata/patch12/Patch", base ? base : "");
        if (hga_set_patch_fallback(a, p)) hga_set_patch_fallback(a, "gamedata/patch12/Patch");
    }
    const char *script = getenv("HG_SCRIPT");
    if (!script) {
        if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
        if (audio_open()) fprintf(stderr, "no sound: %s\n", SDL_GetError());
    }
    front *fe = front_open(a, argv[1]);
    static uint32_t frame[640 * 480];
    if (script) {
        char buf[4096];
        snprintf(buf, sizeof buf, "%s", script);
        for (char *step = strtok(buf, ";"); step; step = strtok(NULL, ";")) {
            while (*step == ' ') step++;
            int x, y;
            if (step[0] == 'c' && sscanf(step + 1, "%d %d", &x, &y) == 2) {
                front_mouse(fe, x, y, 0); front_mouse(fe, x, y, 1); front_mouse(fe, x, y, 0);
            } else if (step[0] == 'm' && sscanf(step + 1, "%d %d", &x, &y) == 2) front_mouse(fe, x, y, 0);
            else if (step[0] == 'd' && sscanf(step + 1, "%d %d", &x, &y) == 2) front_mouse(fe, x, y, 1);
            else if (step[0] == 't') for (const char *c = step + 2; *c; c++) front_char(fe, (unsigned char)*c);
            else if (step[0] == 'k') front_key(fe, (int)strtol(step + 1, NULL, 0));
            else if (step[0] == 'K' && sscanf(step + 1, "%d %d", &x, &y) == 2) front_keys(fe, x, y);
            else if (step[0] == 'S') front_state(fe, (int)strtol(step + 1, NULL, 0));
            else if (step[0] == 'W') front_mission_over(fe, atoi(step + 1));
            else if (step[0] == 'X') { front_mission_stats(fe, "Vehicle Kills: 3\nHit Rate: 42%\nHits Taken: 17\nSquad-mate Kills: 1\nSquad-mates Killed: 0"); front_mission_objectives(fe, "Primary: Destroy all Enemies - Complete\nPrimary: Destroy Base at Way ECHO - Complete\nPrimary: Defend Your Unit\n"); }
            else if (step[0] == 's') { front_draw(fe, frame); write_ppm(step + 2, frame, 640, 480); }
            else if (step[0] == 'w') {
                /* let n ms of the front end's time pass (animations, narration) */
                static uint64_t clk = 1000;
                for (long n = strtol(step + 1, NULL, 0); n > 0; n -= 10) front_tick(fe, clk += 10);
            }
            int r = front_poll(fe);
            if (r == FRONT_MOVIE) printf("movie %s\n", front_movie(fe));
            if (r == FRONT_LAUNCH) printf("launch %s\n", front_scenario(fe));
            if (r == FRONT_QUIT) printf("quit\n");
            printf("%s: %s\n", step, front_screen(fe));
        }
        front_close(fe);
        return 0;
    }
    SDL_Window *win;
    SDL_Renderer *ren;
    if (!SDL_CreateWindowAndRenderer("Heavy Gear", 1280, 960, SDL_WINDOW_RESIZABLE, &win, &ren)) return 1;
    SDL_SetRenderLogicalPresentation(ren, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_Texture *tx = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480);
    SDL_SetTextureScaleMode(tx, SDL_SCALEMODE_NEAREST);
    SDL_StartTextInput(win);
    /* HG_MOVIE=<name> (e.g. ZL_CUT01) plays that cutscene first, for testing */
    if (getenv("HG_MOVIE")) printf("movie %s: %d\n", getenv("HG_MOVIE"), movie_play(ren, argv[1], getenv("HG_MOVIE")));
    /* HG_MISSION=<scene record> (e.g. m01_scn1) goes straight into that mission, for testing */
    if (getenv("HG_MISSION")) {
        /* the wingmen the mission's BRF (mXX_brf1) offers, every pilot alive */
        squad sq;
        int alive[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, mask = 0;
        {
            char bn[16];
            const char *sc = getenv("HG_MISSION");
            snprintf(bn, sizeof bn, "%.3s_brf1", sc);
            size_t bl;
            uint8_t *bd = hga_load(a, bn, "BRF", &bl, NULL);
            static hg_brf hb;
            if (bd && !brf_parse(bd, bl, &hb)) mask = hb.squad;
            free(bd);
        }
        squad_default(&sq, mask, alive, 1);
        for (int i = 0; i < 2; i++) snprintf(sq.gear[i], sizeof sq.gear[i], "%s", sq.pilot[i] >= 0 ? "gacheeta" : "");   /* the gear list's first at tech 0 */
        int res = run_mission(ren, tx, a, argv[1], getenv("HG_MISSION"), &sq, getenv("HG_GEAR"));
        front_close(fe);
        audio_close();
        SDL_Quit();
        (void)res;
        return 0;
    }
    int down = 0;
    for (int run = 1; run;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) run = 0;
            if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                SDL_ConvertEventToRenderCoordinates(ren, &e);
                float fx = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
                float fy = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) down = 1;
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) down = 0;
                front_mouse(fe, (int)fx, (int)fy, down);
            }
            if (e.type == SDL_EVENT_TEXT_INPUT)
                for (const unsigned char *c = (const unsigned char *)e.text.text; *c; c++) if (*c < 0x80) front_char(fe, *c);
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_BACKSPACE) front_key(fe, 8);
                if (e.key.key == SDLK_LEFT) front_key(fe, 0x25);
                if (e.key.key == SDLK_RIGHT) front_key(fe, 0x27);
                if (e.key.key == SDLK_DELETE) front_key(fe, 0x2e);
            }
        }
        {
            const bool *ks = SDL_GetKeyboardState(NULL);
            front_keys(fe, ks[SDL_SCANCODE_LEFT], ks[SDL_SCANCODE_RIGHT]);
        }
        front_tick(fe, SDL_GetTicks());
        int r = front_poll(fe);
        if (r == FRONT_QUIT) run = 0;
        if (r == FRONT_MOVIE) {
            front_sound_pause(fe);
            if (movie_play(ren, argv[1], front_movie(fe)) == 1) run = 0;
        }
        if (r == FRONT_LAUNCH) {
            char scn[16];
            snprintf(scn, sizeof scn, "%s", front_scenario(fe));
            for (char *c = scn; *c; c++) *c = (char)tolower((unsigned char)*c);
            SDL_StopTextInput(win);
            front_sound_pause(fe);
            int res = run_mission(ren, tx, a, argv[1], scn, front_squad(fe), front_player_gear(fe));
            SDL_StartTextInput(win);
            if (res < 0) run = 0;
            else {
                front_squad_lost(fe, stat_slot_lost[1], stat_slot_lost[2]);
                front_mission_over(fe, res);
                char st[256];
                snprintf(st, sizeof st, "Vehicle Kills: %d\nHit Rate: %d%%\nHits Taken: %d\nSquad-mate Kills: %d\nSquad-mates Killed: %d",
                         stat.kills, stat.hits * 100 / (stat.shots > 0 ? stat.shots : 1) > 100 ? 100 : stat.hits * 100 / (stat.shots > 0 ? stat.shots : 1), stat.taken, stat.mate_kills, stat_mates_lost);
                front_mission_stats(fe, st);
                front_mission_objectives(fe, stat_objectives);
            }
        }
        front_draw(fe, frame);
        SDL_UpdateTexture(tx, NULL, frame, 640 * 4);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tx, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(10);
    }
    front_close(fe);
    audio_close();
    SDL_Quit();
    return 0;
}
