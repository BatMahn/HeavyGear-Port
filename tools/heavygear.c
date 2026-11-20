/*
 * heavygear - the port's game: the front end (shell) and, from the briefing's launch button, the mission.
 *   heavygear <game folder>
 *   HG_SCRIPT="..." heavygear <game folder>   no window: runs a script of ';'-separated steps:
 *       c x y (click), d x y (press), m x y (move / release), t text (type), k code (key, Windows code), K l r (the
 *       arrow keys held: Left, Right 0 / 1), S n (go to state n), s file.ppm (screenshot),
 *       w ms (let time pass)
 *   HG_MISSION=m01_scn1 / HG_MOVIE=ZL_CUT01   straight into a mission / play a cutscene first
 * In a mission: the player's gear (1-9 / 0 throttle, W / S step it, Left / Right turn, mouse drag looks; ` a free
 * camera, L look, H HUD; GAMEKEY.MAP's F1 mission channel, F2-F7 squad mates, F12 objectives); Esc the sim's main menu
 * (src/escmenu.h: abort, flee), Ctrl+F12 a success (the mission ends by itself when its objectives decide it).
 */
#include "../src/escmenu.h"
#include "../src/fsutil.h"
#include "../src/hgprj.h"
#include "../src/hud.h"
#include "../src/sim.h"
#include "../src/player.h"
#include "../src/audio.h"
#include "../src/front.h"
#include "../src/movie.h"
#include "../src/world.h"
#include "../src/units.h"
#include "../src/views.h"
#include "../src/input.h"
#include "../src/radar.h"
#include "../src/structures.h"
#include "../src/objectives.h"
#include "../src/radio.h"
#include "../src/sfx.h"
#include "../src/effects.h"
#include "../src/particles.h"
#include "../src/squad.h"
#include "../src/brf.h"
#include "../src/gear_tables.h"
#include "../src/hgrand.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <ctype.h>
#include "../src/mp.h"
#include "../src/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* multiplayer (src/mp.h): the session, set up before the mission (HG_NET_HOST / HG_NET_JOIN), and its sim side */
static net *netg;
static mp *mpg;
static char mp_results[1024];   /* the scores, for B39 */

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
static int struct_boom = 0x32; /* its explosion effect */
/* the visible effects (src/effects.h): world objects for the pool */
static effects fx;
static battle *fx_battle;
static int fx_add(void *ctx, const char *model) { return world_add_effect((world *)ctx, model); }
static void fx_set(void *ctx, int h, int on, float x, float y, float z, float age, int seq) { world_set_effect((world *)ctx, h, on, x, y, z, age, seq); }
/* start an effect and, if it started, its sound there (0x1001b3a1: +8 > 0, the chance +0xc) - quiet in the cockpit
 * view unless `loud` */
static int fx_play(int e, float x, float y, float z, int follow, int loud)
{
    if (!effects_start(&fx, e, x, y, z, follow)) return 0;
    if (gear_effect[e].gate <= 0 || !gear_effect[e].sound[0]) return 1;
    if (gear_effect[e].chance != -1 && hg_rand(100) >= gear_effect[e].chance) return 1;
    sfx_at(gear_effect[e].sound, x, y, z, snd_cockpit && !loud, 0x32);
    return 1;
}

static float fx_ground(void *ctx, float x, float z) { int found; return world_ground_height((world *)ctx, x, z, &found); }
static float fx_radius(void *ctx, int h) { return world_object_radius((world *)ctx, h, NULL); }
static void fx_turn(void *ctx, int h, const float m[9]) { world_set_object_matrix((world *)ctx, h, m); }

static void on_killed(void *ctx, int victim, int owner);
static void mate_reply(radio *rd, int slot, int voice, int line);

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
        float l[3], ox, oy, oz, rs, rc;
        int in = b->st->s[i].inst;
        if (world_object_pose(b->w, in, &ox, &oy, &oz, &rs, &rc)) continue;
        /* its bounding sphere (0x10003dce: the model's +0x44 centre, +0x50 radius, 0x10003326) */
        float r = world_object_radius(b->w, in, l);
        float c[3] = {ox + l[0] * rc + l[2] * rs, oy + l[1], oz - l[0] * rs + l[2] * rc};
        float dx = c[0] - x, dy = c[1] - y, dz = c[2] - z;
        if (SDL_sqrtf(dx * dx + dy * dy + dz * dz) >= r + radius) continue;
        if (structures_damage(b->st, b->w, i, (int)power)) {
            int was = fx.owner;
            fx.owner = owner;
            if (fx_play(b->st->s[i].boom, c[0], c[1], c[2], -1, 0) && b->st->s[i].boom == 0x24)
                world_add_light(b->w, c[0], c[1], c[2], gear_effect[0x24].gate);
            fx.owner = was;
        }
    }
}

static int fx_vehicle_yaw(void *ctx, int u, float *deg)
{
    (void)ctx;
    if (!fx_battle || u < 0 || u >= fx_battle->us->n || fx_battle->us->u[u].cb.dead) return 0;
    *deg = fx_battle->us->u[u].s.heading;
    return 1;
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
static debrief_in stat_result;   /* the debriefing's score input (hgmsn.cfg's objectives, HGCAR +0x48.. / +0x1e) */

static int battle_hit(void *ctx, int owner, const float p0[3], const float p1[3], float *t, int *zone)
{
    battle *b = ctx;
    float ts = 2;
    int v = units_hit(b->us, b->w, owner, p0, p1, t, zone);
    int s = structures_hit(b->st, b->w, p0, p1, &ts);
    if (s >= 0 && (v < 0 || ts < *t)) { *t = ts; *zone = 0; v = STRUCT_ID + s; }
    /* every other object stops shots by its polygons: the scene test (0x100229d9, unless the shot has 0x8000) walks
     * every scene object and its children, skipping only the firer's own vehicle (flag 0x100 with +0x24 = the
     * firer) and hidden ones (+0 & 0x800; 0x10022beb), polygon-tested (0x10022beb / 0x10023199); a step shorter
     * than 0x100eb4f8 tests the ground alone (0x10022890). The structures are tested by structures_hit, the ground
     * by battle_ground */
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
    if (mpg && mp_remote(mpg, id)) return NULL;   /* its damage is its own machine's (0x1005ab68), told by DM */
    return id >= 0 && id < b->us->n ? &b->us->u[id].cb : NULL;
}

static void battle_hit_other(void *ctx, int id, int owner, int power)
{
    battle *b = ctx;
    (void)owner;
    if (id >= STRUCT_ID && id < STATIC_ID && structures_damage(b->st, b->w, id - STRUCT_ID, power)) {
        struct_down = id;
        struct_boom = b->st->s[id - STRUCT_ID].boom;
    }
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

static int ob_vehicle_at(void *ctx, int gps, float *x, float *y, float *z, int *flat, int *dead)
{
    battle *b = ctx;
    int i = units_by_gps(b->us, gps);
    if (i < 0) return 0;
    const unit *u = &b->us->u[i];
    *x = u->s.x; *y = u->s.y; *z = u->s.z;
    *flat = u->gm.vclass == 5 || u->gm.vclass == 8;   /* 0x10066eeb */
    *dead = u->cb.dead;
    return 1;
}

static int ob_structure_at(void *ctx, int tgt, float *x, float *y, float *z)
{
    battle *b = ctx;
    float sn, cs;
    if (tgt < 0 || tgt >= b->st->n) return 0;
    return world_object_pose(b->w, b->st->s[tgt].inst, x, y, z, &sn, &cs) >= 0;
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
    sound_reg_id(name);
    voices *v = ctx;
    audio_stop(v->voice);
    v->voice = 0;
    wave_free(&v->wave);
    if (voice_load(v->a, name, &v->wave)) {
        fprintf(stderr, "voice %s: not found\n", name);
        return 0;
    }
    /* 0x10016f28: at the voices' level (0x10108c28, hgsnd.cfg); a level of 0 turns the voices off (flag 2) */
    int lv = audio_level(SNDCFG_VOICE), db = audio_level_db(0, lv);
    if (lv <= 0 || db < -6000) return 0;
    v->voice = audio_play(&v->wave, audio_db(db > 0 ? 0 : db), 0);
    if (v->voice) fprintf(stderr, "voice %s\n", name);
    return v->voice;
}

/* 0x1006a7a6: the line's .sfl read whole as it is queued (its handle; the queue only holds it) */
static int voice_sfl(void *ctx, const char *name)
{
    (void)ctx;
    size_t len = 0;
    uint8_t *d = audio_sfl_load(name, &len);
    free(d);
    return d != NULL;
}

static void laser_sound(void *ctx, int on) { (void)ctx; sfx_laser_loop(on); }

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

static int player_faction;   /* the player gear's +0x613 (its GDF block's +0x2c): 0 North, 1 South */

/* the player's gear's computer (only once it is running, state 2: 0x1005b5c6 checks +0x4f0) */
static int player_active;

static void player_report(void *ctx, int subject, int verdict)
{
    if (player_active && subject >= 0 && subject < 44) computer_say(ctx, player_faction, subject, verdict);
}

/* the original speaks the objective's voice for the player's group's objectives, once each (0x1028fc60);
 * 0x10067cfc builds "<text> completed / failed" for visible ones but never shows it (logged here only) */
static char hud_msg[96];
static Uint64 hud_msg_at;
static long *g_sim_ticks;     /* the sim's clock for the radio's texts */
static long hud_msg_until;    /* a radio text shows until this tick (0x10016f28: 0x712 ticks) */

/* the voices off (0x10016f28: the voices' level 0, hgsnd flag 2): the radio shows its texts in the message bar */
static int radio_voices_off(void *ctx) { (void)ctx; return audio_level(SNDCFG_VOICE) <= 0; }
static int radio_clock(void *ctx) { (void)ctx; return g_sim_ticks ? (int)*g_sim_ticks : 0; }
static void radio_show(void *ctx, const char *text, int ticks)
{
    (void)ctx;
    snprintf(hud_msg, sizeof hud_msg, "%s", text);
    hud_msg_at = 0;
    hud_msg_until = (g_sim_ticks ? *g_sim_ticks : 0) + ticks;
}
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
        fprintf(stderr, "%s %s\n", o->text, state == OBJ_DONE ? "completed" : "failed");
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
    /* the muzzle flash (0x100ba550: the weapon's +0x18), hung on the firer's weapon (0x100bd13f) */
    effects_start_on(&fx, st->flash, at[0], at[1], at[2], owner);
    if (!(st->flags & 3) || !st->sound[0]) return;
    if (owner == snd_player && snd_cockpit) sfx_flat(st->sound, 0, 0, st->flags & 2 ? 0x432 : 0x32);
    else sfx_at(st->sound, at[0], at[1], at[2], snd_cockpit, st->flags & 2 ? 0x432 : 0x32);
}

/* the particle engine (src/particles.c) and its clock (the sim's ticks) */
static particles *parts;
static long part_clock;

/* the shells' pools (0x100bb425 / 0x100bd1e7) and what a bouncing shell struck (its object's +2: a structure is a GT
 * object, 0x200; another object its OBJ's +0x34) */
static int pool_cap(void *ctx, int kind) { return world_shot_capacity((world *)ctx, kind); }
static float pool_radius(void *ctx, int kind) { return world_shot_radius((world *)ctx, kind); }
static unsigned pool_obj_flags(void *ctx, int id)
{
    if (id >= STATIC_ID) return world_object_flags((world *)ctx, id - STATIC_ID);
    return id >= STRUCT_ID ? 0x200 : 0;
}

/* a bouncing shell's spray off the ground (0x1008514d, by its weapon type) */
static void snd_spray(void *ctx, int owner, int type, const float at[3], const float v[3])
{
    (void)ctx; (void)owner;
    if (parts) particles_spray(parts, (int)part_clock, at, v, type);
}

/* a rail gun's point ending (0x10079f7d: effect 0x49 there) */
static void part_effect(void *ctx, int type, const float at[3])
{
    (void)ctx;
    fx_play(type, at[0], at[1], at[2], -1, 0);
}

/* the particles' sight test (0x100229d9 from the point to the eye): the mission's objects and the vehicles */
static int part_blocked(void *ctx, const float a[3], const float b[3])
{
    battle *bt = ctx;
    float t = 2;
    int zone;
    if (world_ray(bt->w, a, b, &t) >= 0) return 1;
    t = 2;
    return units_hit(bt->us, bt->w, -1, a, b, &t, &zone) >= 0;
}

static const raster_texture *part_texture(void *ctx, const char *name) { return world_texture((world *)ctx, name); }

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
            else sfx_flat(hg_rand(2) ? "AFXWISTB" : "AFXWISHR", -2000, 0, 0x50);
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
    /* 0x10019f1d: a shell striking an object that is neither a vehicle (0x100) nor a structure (0x200) nor flagged
     * 0x400 is flagged 0x400 (the ground's): every other object and the ground (-1); a bouncing shell's end strikes
     * by its own flags (0x100191d7: -1 when it met the ground, -3 when not) */
    if (victim == -1 || victim >= STATIC_ID) fl |= COMBAT_HIT_GROUND;
    int kind = gear_weapon_stat[type].kind;
    int e = combat_effect(kind, fl, snd_cockpit);
    /* a structure brought down (0x1001cc6c): its explosion - 0x32, or 0x24 / 0x16 by the GT's flags (0x100c1152) */
    fx.owner = owner;
    if (victim >= STRUCT_ID && victim == struct_down) {
        struct_down = -1;
        if (fx_play(struct_boom, at[0], at[1], at[2], -1, 0) && struct_boom == 0x24)
            world_add_light(b->w, at[0], at[1], at[2], gear_effect[0x24].gate);
    }
    /* the effect, and only if it started its sound; quiet unless the player was struck (0x101316ec is the view, set by
     * 0x1003719d: command 0x14 toggles 0 the cockpit / 1 outside, 3 the camera of 0x1001ca8a) */
    if (e >= 0 && fx_play(e, at[0], at[1] - combat_effect_drop(kind, fl, snd_cockpit), at[2],
                          (fl & COMBAT_HIT_VEHICLE) ? victim : -1, (fl & COMBAT_HIT_PLAYER) != 0) &&
        combat_effect_light(kind, fl, snd_cockpit))   /* its point light: the effect table's +0 its life (0x1001b3a1) */
        world_add_light(b->w, at[0], at[1] - combat_effect_drop(kind, fl, snd_cockpit), at[2], gear_effect[e].gate);
    fx.owner = -2;
}

static void ob_nav_reached(void *ctx, int nav)
{
    (void)ctx; (void)nav;
    sfx_flat("AFXHDSAT", -2500, 0, 0x50);
}

/* the squad's slot n (0x1000edfa: the vehicle of the player's group with +0x10 == n, its slot as units_spawn gives it) */
static int squad_slot(const units *us, int i)
{
    if (snd_player < 0 || i < 0 || i >= us->n || us->u[i].player || us->u[i].group != us->u[snd_player].group) return -1;
    return us->u[i].slot;
}

/* 0x10017566's codes as squad replies (0x100173e1 lines): 1 -> 2, 2 -> 7, 3 -> 3, 4 -> 5, 5 -> 6, 8 / 9 -> 4, 10 -> 1,
 * 0xb -> 9, 0xc -> 8 (7 none) */
static void squad_code(const units *us, int i, int code)
{
    static const int line[13] = {-1, 2, 7, 3, 5, 6, -1, -1, 4, 4, 1, 9, 8};
    if (!ob_radio || i < 0 || i >= us->n || code < 0 || code > 12 || line[code] < 0) return;
    mate_reply(ob_radio, us->u[i].slot, us->u[i].voice, line[code]);
}

static void on_killed(void *ctx, int victim, int owner)
{
    if (victim >= 0 && victim == snd_player) stat_result.deaths++;   /* HGCAR +0x1e (0x10059e9d) */
    if (mpg) mp_killed(mpg, victim, owner);
    battle *b = ctx;
    if (victim < 0 || victim >= b->us->n) return;
    /* a vehicle of the group the player leads destroyed (0x10059e9d): its slot says code 5 */
    {
        int l = units_leader(b->us, b->us->u[victim].group);
        if (!b->us->u[victim].player && l >= 0 && b->us->u[l].player) squad_code(b->us, victim, 5);
    }
    int vs = squad_slot(b->us, victim), k = b->us->u[victim].gm.vclass;
    if (vs == 1 || vs == 2) { stat_mates_lost++; stat_slot_lost[vs] = 1; }
    if (owner < 0 || owner >= b->us->n || b->us->u[victim].side != 1) return;
    int os = squad_slot(b->us, owner);
    if (owner == snd_player && (k == 1 || k == 2 || k == 3 || k == 4 || k == 5 || k == 8 || k == 10)) stat.kills++;
    /* the slots' kills count victims whose record's +0x18 has 0x1400 (0x10059e9d; the GPS's +0x1a) */
    else if ((os == 1 || os == 2) && (b->us->u[victim].vflags & 0x1400) &&
             (k == 1 || k == 2 || k == 3 || k == 8 || k == 10)) stat.mate_kills++;
}

typedef struct { const gearmodel *gm; const sim_gear *g; } mount_ctx;

static void gear_mount(void *ctx, int kind, float from[3])
{
    const mount_ctx *m = ctx;
    gearmodel_mount(m->gm, m->g->x, m->g->y, m->g->z, m->g->heading, kind, from);
}

/* a beam's ray each frame (0x10018a9c): from the owner's mount */
static int battle_beam_ray(void *ctx, int owner, int type, float from[3], float dir[3])
{
    battle *b = ctx;
    if (owner < 0 || owner >= b->us->n || b->us->u[owner].cb.dead) return 0;
    const unit *u = &b->us->u[owner];
    int kind = 0;
    for (int k = 0; k < u->cb.gr.nweapons; k++) if (u->cb.gr.weapon[k].type == type) { kind = u->cb.gr.weapon[k].kind; break; }
    from[0] = u->s.x; from[1] = u->s.y; from[2] = u->s.z;
    gearmodel_mount(&u->gm, u->s.x, u->s.y, u->s.z, u->s.heading, kind, from);
    dir[0] = dir[1] = dir[2] = 0;   /* the aim as fired (the aim isn't kept per vehicle) */
    return 1;
}

/* the ground's height for the track view (0x1003714c's 0x1002275d) */
static float battle_ground_h(void *ctx, float x, float z)
{
    int found;
    return world_ground_height((const world *)ctx, x, z, &found);
}

/* the ground under the player's gear with its polygon's normal (0x10022591) */
static float ground_n_at(void *ctx, float x, float z, float n[3])
{
    int found;
    return world_ground_height_n((const world *)ctx, x, z, &found, n);
}

/* the HUD's targeting (0x100af750 for the player, 0x100afe82 its test): the sim's INPUTW.MAP: T / Shift+T advance /
 * previous target (any side, filter 8), Y / Shift+Y the next / previous enemy (0x40008: side 1), F / Shift+F friendly
 * (0x20008: side 0), E the nearest enemy (0x100b0f74), Q the one under the reticle. The target is the vehicle's
 * +0xec (0x1000: none). From none the search starts at vehicle 0 going forward, else one past the current one, in
 * table order, wrapping (the original then runs on through the structures' table: structures as targets aren't
 * kept by the port). A vehicle is taken when it isn't the player, powered down (0x10), destroyed (6) or 0x800, has
 * 0x1400 (its GPS +0x1a), its side passes the filter, it is within 175000 (0x10103758) unless 0x1000, and a
 * stealth k over the player's sensors (0..4) keeps it within {750, 500, 300, 150}[k - 1] x 100. None found: the
 * target stays. */
static const mission *tgt_msn;   /* the mission's GPS records (the vehicles' +0x18 flags) */

static int target_ok(const units *us, int pi, int i, int side)
{
    const unit *u = &us->u[i], *p = &us->u[pi];
    if (i == pi || u->cb.dead || !u->placed || u->mode == 10 || u->mode == 11) return 0;
    unsigned vf = tgt_msn && u->gps >= 0 && u->gps < tgt_msn->ngps ? tgt_msn->gps[u->gps].vflags : 0x400;
    if (side >= 0 && u->side != side) return 0;
    if (!(vf & 0x1400) || (vf & 0x800)) return 0;
    float dx = u->s.x - p->s.x, dy = u->s.y - p->s.y, dz = u->s.z - p->s.z, d = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
    if (!(vf & 0x1000) && d > 175000.0f) return 0;
    int k = u->cb.gr.stealth - p->cb.gr.sensors;
    if (k > 4) k = 4;
    static const int seen[5] = {0, 750, 500, 300, 150};
    if (k >= 1 && d / 100.0f > (float)seen[k]) return 0;
    return 1;
}

/* the structures the cycling runs on into (0x100b0379): not destroyed (+0 & 4), with 0x1400 and not 0x800 (the GT's
 * +0x14), their object shown, the side filter by their team's side (0x10042b22), within 175000 unless 0x1000 */
static const structures *tgt_sts;
static const world *tgt_w;

static int target_ok_s(const units *us, int pi, int k, int side)
{
    if (!tgt_sts || !tgt_msn || k < 0 || k >= tgt_sts->n || k >= tgt_msn->ntgt) return 0;
    const mission_target *g = &tgt_msn->tgt[k];
    if (tgt_sts->s[k].dead || (g->gflags & 4) || !(g->gflags & 0x1400) || (g->gflags & 0x800)) return 0;
    float x, y, z, sn, cs;
    if (world_object_pose(tgt_w, tgt_sts->s[k].inst, &x, &y, &z, &sn, &cs) != 0) return 0;
    int sd = g->team < 0 || g->team >= 64 ? 2 : tgt_msn->team_side[g->team];
    if (side >= 0 && sd != side) return 0;
    const unit *p = &us->u[pi];
    float dx = x - p->s.x, dy = y - p->s.y, dz = z - p->s.z;
    return (g->gflags & 0x1000) || SDL_sqrtf(dx * dx + dy * dy + dz * dz) <= 175000.0f;
}

/* the cycling (0x100af750): the vehicles' table, then the structures' (codes 1000 + k), wrapping; cur the code */
static int next_target(const units *us, int pi, int cur, int dir, int side)
{
    int nv = us->n, ns = tgt_sts ? tgt_sts->n : 0, n = nv + ns, i;
    int ci = cur >= 1000 ? nv + cur - 1000 : cur;
    if (ci < 0 || dir == 0) { i = 0; dir = 1; }
    else i = ci + dir;
    for (int k = 0; k < n; k++, i += dir) {
        i = (i % n + n) % n;
        if (i < nv ? target_ok(us, pi, i, side) : target_ok_s(us, pi, i - nv, side)) return i < nv ? i : 1000 + i - nv;
    }
    return cur;
}

/* E: the nearest enemy (0x100b0f74): the enemy filter's run from the start, the least 3D distance (+0xd4) within
 * 175000; none: the target stays */
static int nearest_enemy(const units *us, int pi, int cur)
{
    int best = -1;
    float bd = 3.4e38f;
    for (int i = 0; i < us->n; i++) {
        if (!target_ok(us, pi, i, 1)) continue;
        const unit *u = &us->u[i], *p = &us->u[pi];
        float dx = u->s.x - p->s.x, dy = u->s.y - p->s.y, dz = u->s.z - p->s.z, d = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < bd) { bd = d; best = i; }
    }
    return best < 0 || bd > 175000.0f ? cur : best;
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
static world *cover_world;   /* the mission's world, for the cover search */
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
    case 4: { /* get to cover (0xf, 0x1000f0f4): taken when the cover search (0x100279a3) finds a free cover point
               * (world_cover_points) within 30000 of the mate; from the nearest hostile */
        static float cp[200][3];
        int ncp = world_cover_points(cover_world, cp, NULL, 200), any = 0;
        for (int k = 0; k < ncp && !any; k++) {
            float dx = cp[k][0] - u->s.x, dy = cp[k][1] - u->s.y, dz = cp[k][2] - u->s.z;
            if (dx * dx + dy * dy + dz * dz < 30000.0f * 30000.0f) any = 1;
        }
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
        u->order_n++;   /* applied once (units.c ai_order) */
        u->order_pt = 0;
        if (item == 3) u->order_r = msn->nav[nav].radius;
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
/* HG_NET_AUTO (a test aid for two headless sessions): the player's gear turns toward the nearest other player's,
 * walks while it is far and fires its selected weapon when it is ahead and in reach */
static void net_auto(units *us, int pi, sim_gear *pg, float *throttle, float *turn, int *trig, combatant *pc,
                     int *tgt)
{
    if (!getenv("HG_NET_AUTO") || pi < 0 || pc->dead) return;
    int best = -1;
    float bd = 1e30f;
    for (int i = 0; i < us->n; i++) {
        const unit *u = &us->u[i];
        if (i == pi || !u->remote || u->gps >= 0 || u->cb.dead) continue;
        float dx = u->s.x - pg->x, dz = u->s.z - pg->z, d = SDL_sqrtf(dx * dx + dz * dz);
        if (d < bd) { bd = d; best = i; }
    }
    *trig = 0;
    *throttle = 0;
    *turn = 0;
    if (best < 0) return;
    *tgt = best;
    float want = SDL_atan2f(us->u[best].s.x - pg->x, us->u[best].s.z - pg->z) * 180.0f / 3.14159265f;
    float e = want - sim_gear_view_yaw(pg);
    while (e > 180) e -= 360;
    while (e < -180) e += 360;
    pg->heading += e;   /* faced at once (a test aid, not the sim's turn) */
    e = 0;
    *throttle = 0;
    if (pc->sel < 0 || pc->sel >= pc->gr.nweapons || pc->state[pc->sel] < 0) combat_cycle(pc);
    *trig = SDL_fabsf(e) < 10 && bd < 9000 ? (SDL_GetTicks() / 300) & 1 : 0;
}

static int run_mission(SDL_Renderer *ren, SDL_Texture *tx, hg_archive *a, const char *game, const char *scene,
                       const squad *sq, const char *player_gdf)
{
    char err[256];
    world *w = world_load(a, scene, err, sizeof err);
    if (!w) { fprintf(stderr, "%s\n", err); return 0; }
    cover_world = w;
    load_skygnd(w, game);
    /* the mission's CD track, looping (HGSIM 0x10015cdc / 0x10015e1a) */
    if (world_music(w) > 0 && audio_music_track(game, world_music(w)))
        fprintf(stderr, "music track %d not found (wavmap.ini, music/, or a CD image in the game folder)\n", world_music(w));
    static units us;
    { options o0; options_load(&o0, game); units_set_internal_scale(options_internal_scale(&o0)); }
    units_spawn(&us, w, a);
    if (netg) mpg = mp_open(netg, &us, w, a, us.n);
    static combat_shots shots;
    memset(&shots, 0, sizeof shots);
    static structures sts;
    tgt_sts = &sts; tgt_w = w;
    structures_init(&sts, w);
    /* the structures have their own test; the rest of the mission's objects stop shells by their polygons */
    for (int i = 0; i < sts.n; i++) { world_set_solid(w, sts.s[i].inst, 0); world_set_solid(w, sts.s[i].wreck, 0); }
    battle bt = {w, &us, &sts};
    units_aimd aimd;
    memset(&aimd, 0, sizeof aimd);
    fx_battle = &bt;
    {
        effects_ops fo = {w, fx_add, fx_set, fx_vehicle_pos, fx_ground, fx_radius, fx_turn, fx_splash, fx_vehicle_yaw};
        effects_init(&fx, &fo, (const char (*)[17])world_mission(w)->fx_model, world_mission(w)->fx_count);
    }
    combat_world cw = {&bt, battle_hit, battle_ground, battle_victim, battle_hit_other, battle_hit_vehicle, battle_target_pos, battle_beam_ray};
    /* the objectives; the player's GPS record would be USERSTAR's, flags 0x4006 (the shell's, 0x100ff1e6) */
    static objectives obj;
    int mates = 0;   /* the wingmen the launch writes into USERSTAR (registered there as targets, 0x100ff1e6) */
    for (int i = 0; sq && i < 2; i++) if (sq->pilot[i] >= 0 && sq->gear[i][0]) mates |= 1 << i;
    objectives_init(&obj, world_mission(w), -1, 0x4007, mates);
    hud_msg[0] = 0;
    memset(ob_said, 0, sizeof ob_said);
    static voices vo;
    vo.a = a;
    sfx_init(a);
    /* hgsnd.cfg's levels (HGSIM 0x100396ca at the sim's start; 0x100397ec writes it back at the end) */
    static hg_sndcfg sc;
    audio_cfg_load(game, &sc);
    audio_cfg_apply(&sc);
    int sc_dirty = 0;
    /* the escape screen's values (src/escmenu.h): as committed, and as being edited */
    escmenu_vals evc = {escmenu_value(sc.v[0x44 / 4]), sc.v[SNDCFG_MUSIC], sc.v[SNDCFG_FX], sc.v[SNDCFG_VOICE], 0,
                        sc.v[0x28 / 4] != 0, sc.v[0x30 / 4] != 0, sc.v[0x2c / 4] != 0, sc.v[0x64 / 4] != 0, 0};
    escmenu_vals ev = evc;
    unsigned char gamma6[64];   /* the palette's gamma row (0x1003cd04) for the brightness level in force */
    escmenu_gamma(sc.v[0x44 / 4] >= 0 && sc.v[0x44 / 4] < 16 ? sc.v[0x44 / 4] : 8, gamma6);
    shots.ev.ctx = &bt;
    shots.beams_by_frame = 1;
    shots.ev.fire = snd_fire;
    shots.ev.impact = snd_impact;
    shots.ev.killed = on_killed;
    shots.ev.spray = snd_spray;
    shots.pool_ctx = w;
    shots.pool_cap = pool_cap;
    shots.pool_radius = pool_radius;
    shots.obj_flags = pool_obj_flags;
    {
        /* PartSpec.ini: the patched game folder's own (as SKYGND.PAR, load_skygnd), else the port's patch 1.2 copy,
         * else the game folder's */
        char pp[1200];
        const char *base = SDL_GetBasePath();
        int own = !fs_find_ci(game, "Patch", pp, sizeof pp);
        if (own && !fs_find_ci(game, "partspec.ini", pp, sizeof pp)) parts = particles_new(a, pp);
        else {
            snprintf(pp, sizeof pp, "%sgamedata/patch12/partspec.ini", base ? base : "");
            FILE *f = fopen(pp, "rb");
            if (!f && (f = fopen("gamedata/patch12/partspec.ini", "rb"))) snprintf(pp, sizeof pp, "gamedata/patch12/partspec.ini");
            if (f) { fclose(f); parts = particles_new(a, pp); }
            else parts = particles_new(a, !fs_find_ci(game, "partspec.ini", pp, sizeof pp) ? pp : NULL);
        }
    }
    if (parts) particles_set_sound(parts, laser_sound, NULL);
    part_clock = 0;
    memset(&stat, 0, sizeof stat);
    memset(&stat_result, 0, sizeof stat_result);
    stat_mates_lost = 0;
    memset(stat_slot_lost, 0, sizeof stat_slot_lost);
    static radio rd;
    audio_sfl_scan(game);   /* 0x1006a65c */
    radio_ops rops = {voice_start, voice_busy, voice_exists, &vo, voice_sfl, radio_voices_off, radio_show, radio_clock};
    sound_reg_reset();
    radio_init(&rd, &rops);
    ob_radio = &rd;
    static unsigned char was_dead[UNITS_MAX];
    memset(was_dead, 0, sizeof was_dead);
    obj_world ow = {&bt, ob_vehicle_dead, ob_structure_dead, ob_leader, ob_hit_by, ob_announce, ob_nav_reached,
                    ob_vehicle_at, ob_structure_at};
    int has_mtbl = world_mission(w)->mtbl[0].present, outcome = GROUP_RUNNING;
    long sim_ticks = 0, lost_at = 0;   /* lost_at: 0x10148e1c's tick once the gear is lost */
    g_sim_ticks = &sim_ticks;
    hud_msg_until = 0;
    camera cam;
    world_start_camera(w, &cam);
    /* the gear under the cockpit eye (0x100373fc): the eye object (+0x48, 0x100c4b81) on the standing skeleton, lifted
     * by its own offset's y (+0x524, 0x100c4c26): the Hunter's 155.6 + 15.73 = 171.3 above its root, 48.3 ahead, as
     * measured; the standing height is the soles' depth on that pose (+0x53c) */
    /* no gear from the shell (training, whose scenarios have no USERSTAR): the mission's own player record's */
    const char *pgdf = player_gdf && player_gdf[0] ? player_gdf : NULL;
    for (int k = 0; !pgdf && k < world_mission(w)->ngps; k++)
        if (world_mission(w)->gps[k].role == 0) pgdf = world_mission(w)->gps[k].gear;
    if (!pgdf) pgdf = "gdhuntea";
    float eye_up = 171.3f, eye_fwd = 48.3f, stand = 245.3f;
    const float rad = 3.14159265f / 180;
    {
        gearmodel pm;
        gear pgr;
        if (!gearmodel_load(a, pgdf, &pm) && !gear_load(a, pgdf, &pgr)) {
            int ty[GEAR_MAX_WEAPONS], ki[GEAR_MAX_WEAPONS];
            for (int q = 0; q < pgr.nweapons; q++) { ty[q] = pgr.weapon[q].type; ki[q] = pgr.weapon[q].kind; }
            gearmodel_stand(&pm, a, ty, ki, pgr.nweapons, pgr.version);
            if (pm.found_eye) { eye_up = pm.eye[1] + pm.eye_local[1]; eye_fwd = pm.eye[2]; }
            if (pm.stand > 0) stand = pm.stand;
        }
    }
    sim_gear pg;
    /* the start (world_start_camera puts the Hunter's eye there) */
    float gx = cam.x - 48.3f * SDL_sinf(cam.yaw * rad), gz = cam.z - 48.3f * SDL_cosf(cam.yaw * rad), gy = cam.y - 171.3f - 245.3f;
    if (mpg) {   /* the template's start nav, or the players side by side (the port's), on the ground there */
        mp_start_pos(mpg, mp_slot(mpg), &gx, &gz, cam.yaw);
        int gf;
        float gh = world_ground_height(w, gx, gz, &gf);
        if (gf && mp_has_starts(mpg)) gy = gh;
    }
    cam.x = gx + eye_fwd * SDL_sinf(cam.yaw * rad); cam.z = gz + eye_fwd * SDL_cosf(cam.yaw * rad); cam.y = gy + stand + eye_up;
    sim_gear_init(&pg, gx, gy + stand, gz, cam.yaw, SIM_HUNTER_TOP_SPEED, stand);
    pg.player = 1;
    sim_control pctl;   /* the player's control block (0x101334d8), filled from INPUTW.MAP's keys */
    memset(&pctl, 0, sizeof pctl);
    player_sound psnd;
    memset(&psnd, 0, sizeof psnd);
    float look = 0;     /* the view's tilt, up positive: control +0 = -2 x look (the eye pitches by half the tilt) */
    /* the player's gear (the game's, HGSHELL record +0x14 -> USERGEAR; the port's Hunter by default), on group 0's side */
    /* the internal points' multiplier: hgdif.cfg's difficulty (+5: x 12 / 6 / 1, 0x1004e4cc; set on B64) */
    options opts;
    options_load(&opts, game);
    pg.fall_damage = opts.dif[3] != 0;   /* the landing-damage option (hgdif +3, 0x1014a564 + 3) */
    /* hgsnd.cfg's Particles (+0x28, 0x10086a56), Sky (+0x38) and Texture Filtering (+0x3c), read at the sim's start
     * (0x1006fae1); without the file the port keeps them on */
    if (opts.snd_found) {
        particles_set_enabled(parts, options_snd_i32(&opts, 0x28) != 0);
        world_set_options(w, options_snd_i32(&opts, 0x38) != 0, options_snd_i32(&opts, 0x3c) != 0);
        world_set_dynamic_lights(w, options_snd_i32(&opts, 0x2c) != 0);   /* 0x10086a8e -> 0x1004b2c4 */
    }
    int pi = units_add_player(&us, w, a, pgdf, world_mission(w)->group_side[0], options_internal_scale(&opts), &pg);
    if (mpg && pi >= 0) {
        us.u[pi].group = 15 - mp_slot(mpg);   /* each player its own team (the port's) */
        mp_set_local(mpg, pi);
        mp_add_remotes(mpg, pgdf, &pg);
        if (getenv("HG_NET_FRAGILE"))   /* a test aid: the local gear at a tenth of its points */
            for (int z = 0; z < GEAR_ZONES; z++) { us.u[pi].cb.armor[z] *= 0.1f; us.u[pi].cb.internal[z] *= 0.1f; }
    }
    /* the wingmen (the launch's USERSTAR records, HGSHELL 0x100ff1e6): squad slots 1 and 2 */
    for (int i = 0; sq && i < 2; i++)
        if (sq->pilot[i] >= 0 && sq->gear[i][0] && units_add_mate(&us, w, a, sq->gear[i], i + 1, sq->voice[i]) < 0)
            fprintf(stderr, "wingman %d: gear %s not loaded\n", i + 1, sq->gear[i]);
    /* the design's top speed (0x1004e6a0: speed points x 47.1744; the Hunter's 7 give the MEASURED 330.22) */
    if (pi >= 0 && us.u[pi].cb.gr.speed_points > 0) pg.top_speed = us.u[pi].s.top_speed = us.u[pi].cb.gr.speed_points * 47.1744f;
    if (pi >= 0) sim_gear_jets(&pg, us.u[pi].cb.gr.jump_jets, us.u[pi].cb.gr.torso_id);
    static combatant nobody;
    combatant *pcp = pi >= 0 ? &us.u[pi].cb : &nobody;
    /* B64's Invulnerability / Unlimited Ammo (hgdif +1 / +0): the player's only (0x1005c1f8, 0x10059e9d, 0x100bb425) */
    pcp->invulnerable = options_invulnerable(&opts);
    pcp->unlimited_ammo = options_unlimited_ammo(&opts);
    mount_ctx mc = {pi >= 0 ? &us.u[pi].gm : NULL, &pg};
    /* the HUD's set by the player gear's faction (0x10020d48(+0x613), 0x10053435): North H N.., South H S.. */
    player_faction = pi >= 0 && us.u[pi].gm.faction == 1 ? 1 : 0;
    hud *hd = hud_load(a, player_faction);
    gear g;
    if (hd && !gear_load(a, pgdf, &g)) hud_set_gear(hd, a, &g, 0);
    raster_target rt;
    raster_init(&rt, 640, 480);
    int fled = 0;
    int quit = 0, sw = 0, show_hud = 1, result = 0, free_cam = 0, trig_was = 0, started = 0, menu = 0, show_obj = 0;
    /* B64's Graphics Type (hgsnd +0x58): a software item starts the SOFTWARE look, a Direct3D device the 3D look;
     * without hgsnd.cfg the 3D look (the port's default). L still switches */
    if (options_graphics_3d(&opts) == 0) sw = world_set_software(w, 1);
    snd_player = pi;
    combat_seeker seeker = {0, 0, 0, 0};
    int tone_lock = 0, tone_acq = 0;
    long player_start = 0x43e + rand() % 0x16a;   /* 0x100620b8 state 1: 0x43e ticks and a random 0..0x169 */
    player_active = 0;
    pcp->report = player_report;
    pcp->report_ctx = &rd;
    float throttle = 0, pitch = 0, tick_ms = 0;
    int tgt = -1, nav = -1, frame = 0, stgt = -1;
    int td_on = 0;   /* the target display (0x10123d60, command 0x12: F9) */
    if (getenv("HG_TD")) { td_on = 1; tgt = atoi(getenv("HG_TD")); }   /* a test aid: the display on, that vehicle targeted */   /* stgt: a structure targeted (the cycling's 0x400 | k) */
    static views vw;
    views_init(&vw, 0);
    if (getenv("HG_VIEW")) vw.view = atoi(getenv("HG_VIEW")) == 1 ? VIEW_TRACK : VIEW_COCKPIT;   /* a test aid */
    int glance = 0;
    /* the analog controls (src/input.h: zoom_factor, pilot_tilt, the track view's) and the radar / HUD state
     * (src/radar.h); a reset asked by COCKPIT_VIEW (0x1003b3ba case 0x14: 0x1013355a / 0x1013355d / 0x10133570) */
    static hg_axis ax[AX_COUNT];
    hg_axes_init(ax);
    static radar rdr;
    radar_init(&rdr);
    if (getenv("HG_MAP")) { rdr.req = 1; rdr.mode = 1; radar_map_key(&rdr, 1, 1, 0); }   /* a test aid: the map open */
    int ax_reset = 0;
    camera map_cam = {0, 0, 0, 0, 0, 90, 0};
    tgt_msn = world_mission(w);
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
                if (menu && kc == SDLK_ESCAPE) {
                    if (menu <= ESCMENU_BRIGHT) {   /* a submenu left (list state 5: the cancels): back as committed */
                        ev = evc;
                        audio_set_levels(evc.fx, evc.voice, evc.music);
                        escmenu_gamma(escmenu_level(evc.bright), gamma6);
                    }
                    menu = 0;
                    continue;
                }
                /* MAIN_MENU Esc (command 0x4b -> 0x10062aba(5)): the sim's main menu (src/escmenu.h) */
                if (!menu && kc == SDLK_ESCAPE) { menu = ESCMENU_MAIN; sfx_flat("AFXHDMSB", 0, 0, 0x32); continue; }
                if (menu < 0 && kc >= SDLK_0 && kc <= SDLK_9) {
                    int act = escmenu_pick(menu, (int)(kc - SDLK_0), &ev);
                    if (act == ESCMENU_NONE || act == ESCMENU_UNSUPPORTED) continue;
                    sfx_flat("AFXHDWPS", 0, 0, 0x32);
                    if (act == ESCMENU_STEP) {
                        /* the sliders' previews (0x1001550d: the level in force; 0x10018608: the gamma row) */
                        if (menu == ESCMENU_AUDIO) audio_set_levels(ev.fx, ev.voice, ev.music);
                        if (menu == ESCMENU_BRIGHT) escmenu_gamma(escmenu_level(ev.bright), gamma6);
                        continue;
                    }
                    if (act == ESCMENU_COMMIT) {
                        /* Accept (list state 4): the setters - hgsnd v[1..3] (0x1001569c), +0x44 (0x10018650), +0x28
                         * (0x10086a56: the particles), +0x30, +0x2c, +0x64 - and back to the main list */
                        if (menu == ESCMENU_BRIGHT) sc.v[0x44 / 4] = escmenu_level(ev.bright);
                        if (menu == ESCMENU_AUDIO) {
                            sc.v[SNDCFG_MUSIC] = ev.music; sc.v[SNDCFG_FX] = ev.fx; sc.v[SNDCFG_VOICE] = ev.voice;
                            audio_set_levels(ev.fx, ev.voice, ev.music);
                        }
                        if (menu == ESCMENU_COMBAT) {
                            sc.v[0x28 / 4] = ev.particles; sc.v[0x30 / 4] = ev.shadows;
                            sc.v[0x2c / 4] = ev.lights; sc.v[0x64 / 4] = ev.joycenter;
                            if (parts) particles_set_enabled(parts, ev.particles);
                        }
                        ev.sel = 0;
                        evc = ev;
                        sc_dirty = 1;
                        menu = ESCMENU_MAIN;
                        continue;
                    }
                    ev.sel = 0;
                    if (act == ESCMENU_OPEN_ABORT) menu = ESCMENU_ABORT;
                    else if (act == ESCMENU_OPEN_FLEE) menu = ESCMENU_FLEE;
                    else if (act == ESCMENU_OPEN_BRIGHT) menu = ESCMENU_BRIGHT;
                    else if (act == ESCMENU_OPEN_AUDIO) menu = ESCMENU_AUDIO;
                    else if (act == ESCMENU_OPEN_COMBAT) menu = ESCMENU_COMBAT;
                    else menu = 0;
                    if (act == ESCMENU_DO_ABORT) { pcp->invulnerable = 0; run = 0; result = 0; }
                    if (act == ESCMENU_DO_FLEE && pi >= 0 && !pcp->dead) {
                        /* 0x100308a8: 0x10131458 = 1 - the end at once (0x1003ad72's other path, 0x16a ticks), and
                         * SimMain returns 0xff, on which HG.EXE leaves (0x402710: (char)result == -1) */
                        fled = 1;
                        if (!end_at) end_at = sim_ticks + 0x16a;
                        pcp->invulnerable = 0;
                        pcp->dead = 1;
                        on_killed(&bt, pi, pi);
                    }
                    continue;
                }
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
                if (kc == SDLK_F12 && ctrl) { run = 0; result = 1; }
                if (kc == SDLK_L) sw = world_set_software(w, !sw);
                if (kc == SDLK_H) show_hud = !show_hud;
                if (kc == SDLK_GRAVE && ctrl) free_cam = !free_cam;   /* the port's free camera */
                /* INPUTW.MAP: skate *W, standup *S, crouch Tab, jumpjet_enabled J, reverse_direction *Backspace /
                 * *BackQuote, feet_to_torso *M, torso_pan_reset / torso_tilt_reset Keypad5 */
                if (kc == SDLK_W && !ctrl) pctl.skate = 1;
                if (kc == SDLK_J) pctl.jump = 1;
                if ((kc == SDLK_BACKSPACE || (kc == SDLK_GRAVE && !ctrl))) pctl.reverse = 1;
                if (kc == SDLK_M) pctl.feet_to_torso = 1;
                if (kc == SDLK_KP_5) { pctl.torso_pan = 0; look = 0; }
                if (kc == SDLK_T && pi >= 0 && !free_cam) { int v = next_target(&us, pi, stgt >= 0 ? 1000 + stgt : tgt, shift ? -1 : 1, -1); if (v >= 1000) { stgt = v - 1000; tgt = -1; } else if (v >= 0) { tgt = v; stgt = -1; } }
                if (kc == SDLK_Y && pi >= 0 && !free_cam) { int v = next_target(&us, pi, stgt >= 0 ? 1000 + stgt : tgt, shift ? -1 : 1, 1); if (v >= 1000) { stgt = v - 1000; tgt = -1; } else if (v >= 0) { tgt = v; stgt = -1; } }
                if (kc == SDLK_F && pi >= 0 && !free_cam) { int v = next_target(&us, pi, stgt >= 0 ? 1000 + stgt : tgt, shift ? -1 : 1, 0); if (v >= 1000) { stgt = v - 1000; tgt = -1; } else if (v >= 0) { tgt = v; stgt = -1; } }
                if (kc == SDLK_E && pi >= 0 && !free_cam) { int v = nearest_enemy(&us, pi, tgt); if (v != tgt) { tgt = v; stgt = -1; } }
                /* COCKPIT_VIEW 'c' (command 0x14): the cockpit to the track view, anything else to the cockpit;
                 * ORDINANCE_VIEW F10 (0x25 -> 0x1001ca8a): the view on the player's last missile while it flies */
                if (kc == SDLK_C && !ctrl && pi >= 0 && !us.u[pi].cb.dead) {
                    /* 0x1003b3ba case 0x14: the cockpit (the map closed) to the track view; else the map closes
                     * (0x100891fc), the cockpit, the pilot's tilt / pan and the zoom reset */
                    if (vw.view == VIEW_COCKPIT && rdr.mode != 4) vw.view = VIEW_TRACK;
                    else {
                        if (rdr.mode == 4) radar_map_key(&rdr, 1, 0, (int)sim_ticks);
                        vw.view = VIEW_COCKPIT;
                        ax_reset = 1;
                    }
                }
                /* TOGGLE_HUD F11 (case 0x2b; AFXHDWPS while the gear runs), RADAR_MAP_TOGGLE F8 (case 0x4a: the
                 * satellite uplink, the head's option bit 7, +0xbb3), RADAR_ZOOM_IN / OUT x / Shift+x (cases 0x47 /
                 * 0x48: hgdif.cfg +9 must be set) */
                if (kc == SDLK_F11 && !ctrl && radar_toggle_hud(&rdr) && started == 2) sfx_flat("AFXHDWPS", 0, 0, 0x32);
                /* TARGET_DISPLAY F9 (case 0x12): 0x10123d60 ^= 1, AFXHDWPS when the vehicle record's +0x18 has 0x2000 */
                if (kc == SDLK_F9 && !ctrl) {
                    td_on ^= 1;
                    if (pi >= 0 && (us.u[pi].vflags & 0x2000)) sfx_flat("AFXHDWPS", 0, 0, 0x32);
                }
                if (kc == SDLK_F8 && !ctrl) radar_map_key(&rdr, (pcp->gr.head_flags & 0x80) != 0, started == 2, (int)sim_ticks);
                if (kc == SDLK_X && !ctrl && !(e.key.mod & SDL_KMOD_ALT)) radar_zoom(&rdr, shift ? 2 : 1, opts.dif[9]);
                if (kc == SDLK_F10 && vw.missile >= 0 && shots.shot[vw.missile].active && vw.view != VIEW_ORDINANCE) {
                    vw.before = vw.view;
                    vw.view = VIEW_ORDINANCE;
                }
                if (kc == SDLK_Q && !free_cam && pi >= 0) {
                    float hr = sim_gear_view_yaw(&pg) * rad, pr = pitch * rad, t1 = 1;
                    float eye[3] = {pg.x, pg.y + eye_up, pg.z};
                    float far_[3] = {eye[0] + SDL_sinf(hr) * SDL_cosf(pr) * 150000, eye[1] + SDL_sinf(pr) * 150000,
                                     eye[2] + SDL_cosf(hr) * SDL_cosf(pr) * 150000};
                    int z0, v = units_hit(&us, w, pi, eye, far_, &t1, &z0);
                    if (v >= 0) tgt = v;
                }
                if (kc == SDLK_N) nav = next_nav(world_mission(w), nav, shift ? -1 : 1);
                /* weapon_cycle: Enter (KEYBOARD.MAP) */
                if (kc == SDLK_RETURN && hd && !(e.key.mod & SDL_KMOD_ALT)) {
                    combat_cycle(pcp);
                    sfx_flat("AFXHDWPS", 0, 0, 0x32);   /* 0x100620b8 */
                    for (int i = 0; i < g.nweapons; i++) g.weapon[i].ammo = pcp->ammo[i];
                    hud_set_gear(hd, a, &g, pcp->sel);
                }
                /* throttle keys: 1 = 0, 2..9 = 1/9 .. 8/9, 0 = full (MEASURED in the original: the control's
                 * throttle reads 4/9 x 1/64 after 5, 8/9 x 1/64 after 9, 1/64 after 0) */
                if (kc >= SDLK_1 && kc <= SDLK_9) throttle = (float)(kc - SDLK_1) / 9.0f;
                if (kc == SDLK_0) throttle = 1.0f;
                /* throttle_plus / throttle_minus: Equal / Minus (a ninth a press: the port's step) */
                if (kc == SDLK_EQUALS || kc == SDLK_KP_PLUS) throttle = SDL_min(1.0f, throttle + 1.0f / 9.0f);
                if (kc == SDLK_MINUS || kc == SDLK_KP_MINUS) throttle = SDL_max(0.0f, throttle - 1.0f / 9.0f);
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
                if (free_cam) {
                    cam.yaw += e.motion.xrel * 0.25f;
                    pitch -= e.motion.yrel * 0.25f;
                    if (pitch > 89) pitch = 89;
                    if (pitch < -89) pitch = -89;
                } else {
                    /* torso_pan (+4, -90..90) and torso_tilt (+0, -60..60) by the mouse (0.25 degree a pixel: the
                     * port's scale) */
                    pctl.torso_pan = SDL_clamp(pctl.torso_pan + e.motion.xrel * 0.25f, -90.0f, 90.0f);
                    look = SDL_clamp(look - e.motion.yrel * 0.25f, -30.0f, 30.0f);
                }
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
            /* legs_pan: the arrows (INPUTW.MAP GreyLeft / GreyRight; with Ctrl held they glance, glance_left /
             * glance_right); torso_pan: Comma / Period; torso_tilt: Up / Down (held: 90 / 60 degrees a second, the port's
             * rates); crouch Tab, standup S held */
            int ctl_held = (SDL_GetModState() & SDL_KMOD_CTRL) != 0, sh_held = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            float turn = (k[SDL_SCANCODE_RIGHT] && !ctl_held && !sh_held ? 1.0f : 0.0f) -
                         (k[SDL_SCANCODE_LEFT] && !ctl_held && !sh_held ? 1.0f : 0.0f);
            /* straffe_left / straffe_right: Shift + the arrows, not Ctrl (INPUTW.MAP; the animations' +0x57 / +0x58) */
            pctl.straffe_l = sh_held && !ctl_held && k[SDL_SCANCODE_LEFT];
            pctl.straffe_r = sh_held && !ctl_held && k[SDL_SCANCODE_RIGHT];
            glance = ctl_held && !sh_held ? (k[SDL_SCANCODE_LEFT] ? VIEW_GLANCE_LEFT : 0) | (k[SDL_SCANCODE_RIGHT] ? VIEW_GLANCE_RIGHT : 0) : 0;
            if (k[SDL_SCANCODE_PERIOD]) pctl.torso_pan = SDL_min(90.0f, pctl.torso_pan + 90.0f * dt);
            if (k[SDL_SCANCODE_COMMA]) pctl.torso_pan = SDL_max(-90.0f, pctl.torso_pan - 90.0f * dt);
            if (k[SDL_SCANCODE_UP] && !ctl_held) look = SDL_min(30.0f, look + 30.0f * dt);
            if (k[SDL_SCANCODE_DOWN] && !ctl_held) look = SDL_max(-30.0f, look - 30.0f * dt);
            pctl.crouch = k[SDL_SCANCODE_TAB];
            pctl.standup = k[SDL_SCANCODE_S];
            /* the sim's 5 ms ticks */
            tick_ms += dt * 1000.0f;
            /* weapon_fire: Space (KEYBOARD.MAP). The aim: along the view at the aim distance (0x100bcd98's lag, set by
             * the 150000 ray of 0x100bcf44 striking a vehicle or a structure; src/units.h units_aim_dist) */
            int trig = k[SDL_SCANCODE_SPACE];
            if (mpg) net_auto(&us, pi, &pg, &throttle, &turn, &trig, pcp, &tgt);
            /* the trigger on an empty weapon (0x100ba550): AFXWPMTY at -10 dB */
            if (trig && !trig_was && pcp->state[pcp->sel] < 0 && !pcp->dead) sfx_flat("AFXWPMTY", -1000, 0, 0x32);
            trig_was = trig;
            /* the effects' harm once a frame, for the frame's ticks (0x1001c40d: dt x power, truncated) */
            int frame_ticks = (int)(tick_ms / SIM_TICK_MS);
            /* the analog controls once a frame with its ticks (0x10048c58; INPUTW.MAP: zoom_factor Z / Shift+Z /
             * Ctrl+Z, track_distance Z / Shift+Z, pilot_tilt and track_height Ctrl+Up / Down, eyepoint_pan
             * Ctrl+Left / Right) */
            {
                int z = k[SDL_SCANCODE_Z], up = k[SDL_SCANCODE_UP] && ctl_held, dn = k[SDL_SCANCODE_DOWN] && ctl_held;
                int lf = k[SDL_SCANCODE_LEFT] && ctl_held && !sh_held, rt_ = k[SDL_SCANCODE_RIGHT] && ctl_held && !sh_held;
                hg_axis_step(&ax[AX_ZOOM], frame_ticks, z && !ctl_held && !sh_held, z && sh_held && !ctl_held,
                             (z && ctl_held && !sh_held) || ax_reset);
                hg_axis_step(&ax[AX_PILOT_TILT], frame_ticks, up, dn, ax_reset);
                hg_axis_step(&ax[AX_PILOT_PAN], frame_ticks, 0, 0, ax_reset);
                hg_axis_step(&ax[AX_EYE_TILT], frame_ticks, 0, 0, 0);
                hg_axis_step(&ax[AX_EYE_PAN], frame_ticks, rt_, lf, 0);
                hg_axis_step(&ax[AX_TRACK_DIST], frame_ticks, z && sh_held && !ctl_held, z && !ctl_held && !sh_held, 0);
                hg_axis_step(&ax[AX_TRACK_HEIGHT], frame_ticks, up, dn, 0);
                ax_reset = 0;
            }
            /* the player's gear: the move then the control, once a frame with the frame's ticks (0x10060296 ->
             * 0x1001f70a / 0x1001f769), as the original (src/sim.h); the power out (+0xc3d) holds it */
            if (frame_ticks) {
                pctl.throttle = throttle;
                pctl.turn = turn;
                pctl.torso_tilt = -2.0f * look;
                pg.speed_factor = pcp->speed_factor;
                int stunned = pcp->stun > 0;
                if (stunned) { pctl.throttle = 0; pctl.turn = 0; }
                int sev = sim_gear_step(&pg, &pctl, frame_ticks, started == 2 && !pcp->dead, ground_n_at, w);
                psnd.cockpit = snd_cockpit;
                player_sounds(&psnd, sev, &pg, pcp, frame_ticks);
                if (sev & SIM_EV_SKATE_ENGAGE) throttle = 0;      /* control +8 = 0 as the wheels come down */
                if (sev & SIM_EV_SKATE_ON) throttle = 0.5f;       /* then 1/128 (0x3c000000) */
                pctl.skate = pctl.jump = pctl.reverse = pctl.feet_to_torso = 0;
                pitch = -(pg.pitch.cur + pg.tilt.cur / 2);       /* the eye: the gear's pitch + the tilt / 2 (0x100373fc) */
            }
            if (mpg) {   /* the session (0x10031a86 each frame): the others' states in, ours out; the sim waits for GO */
                int clk = -1;
                mp_frame(mpg, (int)sim_ticks, &clk);
                if (clk >= 0 && !net_is_host(netg)) sim_ticks = clk;   /* a joiner runs on the master's clock */
                const char *mm = mp_message(mpg);
                if (mm) { snprintf(hud_msg, sizeof hud_msg, "%s", mm); hud_msg_at = SDL_GetTicks(); }
                if (!mp_started(mpg)) { tick_ms = 0; frame_ticks = 0; }
                if (mp_ended(mpg) && !end_at) end_at = sim_ticks + 0x16a;
            }
            units_frame(&us);
            world_lights_tick(w, frame_ticks);   /* 0x1004b193: the point lights age by the frame's ticks */
            /* the animations once a frame (0x10013d53, src/units.h units_animate): the player's pose gives the eye
             * (0x100373fc: the eye object's place plus its own offset's y), its foot-downs AFXHGWKL, its falls AFXHGFLS */
            if (frame_ticks && pi >= 0) {
                us.u[pi].s.body = pg.body;
                int aev = units_animate(&us, a, frame_ticks, &pctl);
                pg.body = us.u[pi].s.body;
                pg.legs_gone = us.u[pi].s.legs_gone;
                if (us.u[pi].gm.found_eye) { eye_up = us.u[pi].gm.eye[1] + us.u[pi].gm.eye_local[1]; eye_fwd = us.u[pi].gm.eye[2]; }
                if (aev & 1) sfx_footstep(pg.x, pg.y, pg.z, snd_cockpit);
                if (aev & 2) sfx_fall();
            }
            if (frame_ticks) effects_splash(&fx, frame_ticks);
            for (; tick_ms >= SIM_TICK_MS; tick_ms -= SIM_TICK_MS) {
                /* the gear moves once its engine runs (0x10060491 zeroes the speeds below state 2) */
                float hr = sim_gear_view_yaw(&pg) * rad, pr = pitch * rad;
                float eye[3] = {pg.x + eye_fwd * SDL_sinf(hr), pg.y + eye_up, pg.z + eye_fwd * SDL_cosf(hr)};
                float dir[3] = {SDL_sinf(hr) * SDL_cosf(pr), SDL_sinf(pr), SDL_cosf(hr) * SDL_cosf(pr)};
                float far_[3] = {eye[0] + dir[0] * 150000, eye[1] + dir[1] * 150000, eye[2] + dir[2] * 150000};
                float at = 2, aim[3];
                int az;
                /* the aim distance (0x100bcd98, units_aim_dist): a vehicle or structure first on the ray */
                int hv = battle_hit(&bt, pi, eye, far_, &at, &az);
                float ad = units_aim_dist(&aimd, (int)sim_ticks, hv >= 0 && hv < STATIC_ID && at <= 1 ? at * 150000.0f : -1.0f);
                for (int q = 0; q < 3; q++) aim[q] = eye[q] + dir[q] * ad;
                memcpy(aim_pt, aim, sizeof aim_pt);
                int before = pcp->ammo[pcp->sel];
                if (pi >= 0) { us.u[pi].s = pg; us.u[pi].target = tgt; us.u[pi].aim_pitch = pitch; }
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
                if (mpg) mp_tick(mpg, &shots);
                if (pcp->ammo[pcp->sel] != before && hd) {
                    for (int i = 0; i < g.nweapons; i++) g.weapon[i].ammo = pcp->ammo[i];
                    hud_set_gear(hd, a, &g, pcp->sel);
                }
                /* each group's current objective, as the AI's modes need it (src/units.h units_goals) */
                units_goal goals[16];
                units_goals(goals, has_mtbl ? &obj : NULL, &us, w);
                units_ai_tick(&us, w, goals, &shots);
                for (int ru, rc; units_take_reply(&us, &ru, &rc);) squad_code(&us, ru, rc);
                combat_shots_tick(&shots, &cw);
                /* the ordinance view's missile (0x100bb425: the player's last shot of kind 0x3e, 0x41 or 0x3f) */
                {
                    static unsigned seen = 0;
                    for (int q = 0; q < COMBAT_MAX_SHOTS; q++) {
                        const combat_shot *ms = &shots.shot[q];
                        int kd = ms->active ? gear_weapon_stat[ms->type].kind : 0;
                        if (ms->active && ms->owner == pi && (kd == 0x3e || kd == 0x41 || kd == 0x3f) && ms->serial > seen) {
                            seen = ms->serial;
                            if (vw.view != VIEW_ORDINANCE) vw.missile = q;
                        }
                    }
                }
                effects_tick(&fx, 1);
                sim_ticks++;
                /* a multiplayer template (start navs, no player record): its objectives don't end the session (the
                 * port's: the original's player stars aren't written, so the player's group would read as lost) */
                if (has_mtbl && outcome == GROUP_RUNNING && !(mpg && mp_has_starts(mpg))) {
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
                /* the player's gear destroyed (0x1005ca49): alone (single player) it is a wreck at once (state 5) and
                 * 0x100620b8 holds view 4 (0x10037c73, the camera rising over it) for 0x389 ticks, then sets 0x10148e1c,
                 * with which 0x1003ad72 starts the end ("Exiting Mission Area...", 0x16a ticks); the view stays. In a
                 * network game it goes to state 4: 0x10148e1c at once and the track view (0x1003719d(1)), whose
                 * eyepoint then turns by itself (0x10036ea6 case 1: the frame's ticks x 0x100eb72c) */
                if (pcp->dead && !lost_at) {
                    lost_at = mpg ? sim_ticks : sim_ticks + 0x389;
                    vw.view = mpg ? VIEW_TRACK : VIEW_RISE;
                }
                if (pcp->dead && !end_at && sim_ticks >= lost_at) end_at = sim_ticks + 0x16a;
                if (mpg && end_at) mp_end(mpg);
                /* an enemy vehicle destroyed: the computer names its kind (0x10059e9d: classes 1 gear, 3, 4 (2
                 * reads as 4) vehicle, 5 / 8 aircraft, 10; infantry say nothing) */
                for (int i = 0; i < us.n && i < UNITS_MAX && pi >= 0; i++) {
                    unit *u = &us.u[i];
                    if (!u->cb.dead || was_dead[i]) continue;
                    was_dead[i] = 1;
                    /* its death (state 4): a gear's (0x10060491 -> 0x100533d2) the gear death effect, then 0x712
                     * ticks of burning, debris among it; other vehicles' (0x1002b87a) the burning alone */
                    if (u->gm.vclass == 1 && fx_play(0x24, u->s.x, u->s.y, u->s.z, -1, 0))
                        world_add_light(w, u->s.x, u->s.y, u->s.z, gear_effect[0x24].gate);
                    effects_wreck(&fx, u->s.x, u->s.y, u->s.z);
                    if (u->side == us.u[pi].side || u->side >= 2) continue;
                    int c = u->gm.vclass == 2 ? 4 : u->gm.vclass;
                    int subj = c == 1 ? 0x22 : c == 3 ? 0x23 : c == 4 ? 0x24 : c == 5 || c == 8 ? 0x25 : c == 10 ? 0x26 : -1;
                    if (subj >= 0) computer_say(&rd, player_faction, subj, -1);
                }
            }
            if (frame_ticks) combat_beams_frame(&shots, &cw, frame_ticks);
            if (frame_ticks && parts) {
                /* the particle engine, once a frame: the shells' trails over the frame's motion (0x10019f1d /
                 * 0x100191d7 after moving: kinds 0x38 / 0x39 the tracer glow; 0x3d, 0x3e, 0x42..0x44 (and 0x45 for
                 * bouncing shells) the smoke trail when the weapon's +0x86 has 0x40, else the missile's; 0x3f both;
                 * 0x40, 0x41 (and 0x46..0x48 for the others) the smoke; 0x45 the missile's), the beams' points
                 * (0x100baf31 / 0x10018a9c), then the update (0x10079dfa) */
                static float trail_at[COMBAT_MAX_SHOTS][3];
                static unsigned trail_serial[COMBAT_MAX_SHOTS], beam_serial[COMBAT_MAX_SHOTS];
                part_clock = sim_ticks;
                part_view pv;
                memset(&pv, 0, sizeof pv);
                pv.cam = cam;
                pv.w = rt.w; pv.h = rt.h;
                pv.far = world_far(w);
                pv.camdist = stand * 5.0f * 4.0f;   /* 0x101316dc = the gear's +0x53c x 5 (0x100eb74c) x 4 */
                pv.outside = free_cam;
                pv.zoom = 1.0f;
                pv.software = sw;
                pv.ctx = &bt;
                pv.blocked = part_blocked;
                particles_set_view(parts, &pv);
                for (int i = 0; i < COMBAT_MAX_SHOTS; i++) {
                    const combat_shot *p = &shots.shot[i];
                    if (p->beam) {
                        if (!p->serial) continue;
                        float pts[16][3];
                        for (int k = 0; k < 16; k++)
                            for (int c = 0; c < 3; c++) pts[k][c] = (&p->x)[c] + (p->end[c] - (&p->x)[c]) * (float)k / 15.0f;
                        if (beam_serial[i] != p->serial) {
                            beam_serial[i] = p->serial;
                            particles_beam(parts, (int)sim_ticks, i, gear_weapon_stat[p->type].kind,
                                           gear_weapon_stat[p->type].life, (const float (*)[3])pts);
                        } else if (p->active) particles_beam_points(parts, i, (const float (*)[3])pts);
                        continue;
                    }
                    if (!p->active) { trail_serial[i] = 0; continue; }
                    float at[3] = {p->x, p->y, p->z};
                    if (trail_serial[i] != p->serial) {
                        trail_serial[i] = p->serial;
                        memcpy(trail_at[i], at, sizeof at);
                        continue;
                    }
                    float d[3] = {at[0] - trail_at[i][0], at[1] - trail_at[i][1], at[2] - trail_at[i][2]};
                    const float *o = trail_at[i];
                    int kind = gear_weapon_stat[p->type].kind, smoke40 = (gear_weapon_stat[p->type].sflags & 0x40) != 0;
                    int bouncing = p->motion == 3;
                    switch (kind) {
                    case 0x38: case 0x39: particles_tracer(parts, (int)sim_ticks, o, d); break;
                    case 0x3d: case 0x3e: case 0x42: case 0x43: case 0x44:
                        if (smoke40) particles_smoke(parts, (int)sim_ticks, o, d);
                        else particles_missile(parts, (int)sim_ticks, o, d);
                        break;
                    case 0x45:
                        if (bouncing && smoke40) particles_smoke(parts, (int)sim_ticks, o, d);
                        else particles_missile(parts, (int)sim_ticks, o, d);
                        break;
                    case 0x3f:
                        particles_missile(parts, (int)sim_ticks, o, d);
                        particles_smoke(parts, (int)sim_ticks, o, d);
                        break;
                    case 0x40: case 0x41: particles_smoke(parts, (int)sim_ticks, o, d); break;
                    case 0x46: case 0x47: case 0x48: if (!bouncing) particles_smoke(parts, (int)sim_ticks, o, d); break;
                    }
                    memcpy(trail_at[i], at, sizeof at);
                }
                particles_frame(parts, (int)sim_ticks, part_effect, NULL);
            }
            /* the mission's end (0x1003ad72): once the player's group is decided (0x10131968) or its gear is gone,
             * "Exiting Mission Area..." shows and the sim ends 0x16a ticks later (a port choice beyond that: the
             * last voice is let finish) */
            radio_tick(&rd);
            if (end_at) {
                snprintf(hud_msg, sizeof hud_msg, "Exiting Mission Area...");
                hud_msg_at = SDL_GetTicks();
                if (sim_ticks >= end_at && !radio_active(&rd)) run = 0;
            }
            /* the view (0x10036ea6) */
            {
                /* the eye (0x100373fc): the heading plus the twist, the gear's pitch plus half the tilt */
                float vyaw = sim_gear_view_yaw(&pg);
                float eye[3] = {pg.x + eye_fwd * SDL_sinf(vyaw * rad), pg.y + eye_up, pg.z + eye_fwd * SDL_cosf(vyaw * rad)};
                if (vw.view == VIEW_ORDINANCE && (vw.missile < 0 || !shots.shot[vw.missile].active)) {
                    /* 0x1003754a: the missile gone (0x1001ca55), back to the view before */
                    vw.view = vw.before;
                    vw.missile = -1;
                }
                if (vw.view == VIEW_RISE) {
                    views_rise(&vw, (int)sim_ticks, frame_ticks, eye, pg.heading, &cam);
                } else if (vw.view == VIEW_ORDINANCE) {
                    const combat_shot *ms = &shots.shot[vw.missile];
                    float mp[3] = {ms->x, ms->y, ms->z}, mv[3] = {ms->vx, ms->vy, ms->vz};
                    views_ordinance(mp, mv, &cam);
                    if (vw.was != VIEW_ORDINANCE) vw.zoom2 = 2.0f;   /* 0x1003754a: 0x10110408 = 2 */
                    vw.was = VIEW_ORDINANCE;
                } else if (vw.view == VIEW_TRACK && pi >= 0) {
                    /* the track view round the eye point (0x10036ea6 case 1): the frame's ticks x track_distance /
                     * track_height / eyepoint_pan / 181, the eyepoint's tilt as it stands */
                    const unit *pu = &us.u[pi];
                    float t181 = (float)frame_ticks / 181.0f;
                    views_track(&vw, (int)sim_ticks, eye, pg.heading, pu->gm.stand, eye_up, 0, battle_ground_h, w,
                                t181 * hg_axis_value(&ax[AX_TRACK_DIST]), t181 * hg_axis_value(&ax[AX_TRACK_HEIGHT]),
                                hg_axis_value(&ax[AX_EYE_TILT]),
                                lost_at && sim_ticks >= lost_at ? (float)frame_ticks * 0.25f : t181 * hg_axis_value(&ax[AX_EYE_PAN]), &cam);
                } else {
                    vw.pilot_tilt = hg_axis_value(&ax[AX_PILOT_TILT]);
                    vw.pilot_pan = hg_axis_value(&ax[AX_PILOT_PAN]);
                    views_cockpit(&vw, (int)sim_ticks, eye, vyaw, pitch, glance, &cam);
                    cam.roll = pg.roll.cur / 10.0f;   /* 0x100373fc: the eye object's roll / 10 (0x100eb744) */
                }
                /* 0x100371d9: the zoom (AFXHDZMI as it changes) */
                if (views_zoom(&vw, hg_axis_value(&ax[AX_ZOOM]))) sfx_flat("AFXHDZMI", -2500, 0, 0x50);
                cam.fov = views_fov(90.0f, vw.zoom);
                /* the radar's step (0x100878fc): into the map the HUD goes off, AFXHDSAT and report 0x27; out of it
                 * the HUD back and AFXHDSAT */
                int rev = radar_step(&rdr, pcp->dead);
                if (rev == 3) { sfx_flat("AFXHDSAT", 0, 0, 0x50); computer_say(&rd, player_faction, 0x27, -1); }
                if (rev == 5) sfx_flat("AFXHDSAT", 0, 0, 0x50);
            }
            /* the player's gear starts up (0x100620b8 state 0: AFXHGENG at -20 dB, then after 0x43e ticks and a random
             * part, state 2 and AFXHBEEP; till then it neither moves nor turns) */
            sfx_listener(cam.x, cam.y, cam.z, cam.yaw);
            snd_cockpit = !free_cam && vw.view == VIEW_COCKPIT;   /* 0x101316ec: 0 the cockpit */
            if (!started) { started = 1; sfx_flat("AFXHGENG", -2000, 0, 0x32); }
            if (started == 1 && sim_ticks > player_start) { started = 2; player_active = 1; obj.player_running = 1; sfx_flat("AFXHBEEP", 0, 0, 0x32); }
        }
        units_update(&us, w);
        {
            /* the near plane (camera +0x3c): the 640-wide view's 161, 8 in the track view (view mode 1) */
            int cockpit = !free_cam && vw.view == VIEW_COCKPIT;
            world_set_near(w, !free_cam && vw.view == VIEW_TRACK ? 8.0f : 0.0f);
            if (pi >= 0) views_player_parts(w, &us.u[pi], 1, cockpit);
        }
        world_set_time(w, (float)(now - start) / 1000.0f);
        {
            /* shots in flight: projectiles as their kind's model (world_set_shots: turned as fired, a guided one
             * along its motion each tick); the beams and the trails are the particle engine's (particles_draw) */
            static world_shot ws[COMBAT_MAX_SHOTS];
            static float shot_m[COMBAT_MAX_SHOTS][9];
            static int shot_age[COMBAT_MAX_SHOTS], shot_was[COMBAT_MAX_SHOTS];
            int nw = 0;
            for (int i = 0; i < COMBAT_MAX_SHOTS; i++) {
                const combat_shot *p = &shots.shot[i];
                if (!p->active) { shot_was[i] = 0; continue; }
                int kind = gear_weapon_stat[p->type].kind;
                if (world_has_shot_model(w, kind)) {
                    int guided = p->target >= 0;
                    if (guided) {
                        float d[3] = {p->vx, p->vy, p->vz};
                        world_shot_matrix(d, 1, shot_m[i]);
                    } else if (!shot_was[i] || p->age < shot_age[i]) {
                        /* the direction it was fired in: its velocity less the gravity of the ticks flown */
                        float d[3] = {p->vx - p->ax * (float)p->age, p->vy - p->ay * (float)p->age,
                                      p->vz - p->az * (float)p->age};
                        world_shot_matrix(d, 0, shot_m[i]);
                    }
                    shot_was[i] = 1;
                    shot_age[i] = p->age;
                    ws[nw].kind = kind;
                    ws[nw].x = p->x; ws[nw].y = p->y; ws[nw].z = p->z;
                    memcpy(ws[nw].m, shot_m[i], sizeof ws[nw].m);
                    nw++;
                    continue;
                }
            }
            world_set_shots(w, ws, nw);
            if (rdr.mode == 4) {
                /* the map (0x10087b59 mode 4 -> 0x100784e0): the scene from the gear's place at a height of the
                 * range, looking straight down, north up, its far distance the range (+0x40) */
                camera mc = cam;
                mc.roll = 0;
                mc.x = pg.x; mc.z = pg.z; mc.y = (float)radar_range(&rdr);
                mc.yaw = 0; mc.pitch = -90.0f;
                mission *mm = (mission *)world_mission(w);
                int vd = mm->view_distance;
                mm->view_distance = (int)((float)radar_range(&rdr) / 2.5f);
                world_set_nofog(1);
                world_draw(w, &rt, &mc);
                world_set_nofog(0);
                mm->view_distance = vd;
                map_cam = mc;
            } else {
                world_draw(w, &rt, &cam);
                units_set_view(&us, &cam, rt.w, rt.h, world_far(w));
                if (parts) particles_draw(parts, &rt, &cam, world_palette(w), sw ? NULL : part_texture, w);
                /* the target display (window 11's draw 0x100ad8cb, with the HUD's windows: 0x10118e90 on, the gear
                 * running, 0x107f2398 == 2): a vehicle or a standing structure targeted is drawn again alone, flat
                 * (0x10026610 mode 4: the target vehicle's parts, flag 0x100 with a 0 nibble, colour 0x10; a
                 * structure's object, 0x200, 0x17) */
                if (td_on && show_hud && rdr.hud && started == 2 && !free_cam) {
                    int ids[GEARMODEL_MAX + 1], n = 0;
                    unsigned char col[GEARMODEL_MAX + 1];
                    if (tgt >= 0 && tgt < us.n) {
                        const unit *tu = &us.u[tgt];
                        for (int k = 0; k < tu->gm.nparts && n < GEARMODEL_MAX; k++)
                            if (tu->obj[k] >= 0) { ids[n] = tu->obj[k]; col[n++] = 0x10; }
                    } else if (stgt >= 0 && stgt < sts.n && !sts.s[stgt].dead && sts.s[stgt].inst >= 0) {
                        ids[n] = sts.s[stgt].inst; col[n++] = 0x17;
                    }
                    if (n) world_draw_flat(w, &rt, &cam, ids, col, n);
                }
            }
        }
        if (show_hud && hd) {
            static hud_state hs;
            const mission *msn = world_mission(w);
            hud_state_simple(&hs, free_cam ? cam.yaw : pg.heading, 0);
            /* the speed readout: |velocity| (units per tick) x 6 x 1.38, truncated, negative backward (0x100b336b) */
            hs.kph = (float)abs(sim_gear_kph(&pg));
            hs.backward = pg.speed < 0;
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
            /* the opened message box: a radio menu (0x100636da in window 15, src/hud.c radio_menu): the mission
             * channel's three entries each with a spacer row; a squad channel's six orders and the exit item */
            hs.menu_style = 0;
            if (menu < 0) escmenu_fill(&hs, menu, &ev);
            else if (menu == 1) {
                hs.box_open = 1;
                hs.menu_title = "MISSION CHANNEL";
                hs.nmenu = 6;
                for (int sl = 0; sl < 3; sl++) {
                    int k = has_mtbl ? objectives_radio_slot(&obj, 0, sl) : -1;
                    hs.menu[2 * sl].type = HUD_MENU_ITEM;
                    hs.menu[2 * sl].text = k >= 0 ? msn->mtbl[0].obj[k].text : "NO MESSAGE";
                    hs.menu[2 * sl + 1].type = HUD_MENU_SPACER;
                    hs.menu[2 * sl + 1].text = NULL;
                }
            } else if (menu > 1) {
                /* the squad mate channels' menus (0x101063b4 ..: six items and "(Esc to exit)") */
                static const char *const orders[6] = {"Attack My Target", "Flank my target", "Defend my target",
                    "Scout my target", "Get to cover", "Return to Formation"};
                static char title[32];
                snprintf(title, sizeof title, "SQUAD MATE %d CHANNEL", menu - 1);
                hs.box_open = 1;
                hs.menu_title = title;
                hs.nmenu = 7;
                for (int i = 0; i < 6; i++) { hs.menu[i].type = HUD_MENU_ITEM; hs.menu[i].text = orders[i]; }
                hs.menu[6].type = HUD_MENU_EXIT;
                hs.menu[6].text = "(Esc to exit)";
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
            float sx = 0, sy = 0;
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
            /* the target: a structure (0x400 | k) at its object */
            if (stgt >= 0 && !target_ok_s(&us, pi, stgt, -1)) stgt = -1;
            if (stgt >= 0) {
                float x, y, z, sn2, cs2;
                world_object_pose(w, sts.s[stgt].inst, &x, &y, &z, &sn2, &cs2);
                const mission_target *g = &msn->tgt[stgt];
                int sd = g->team < 0 || g->team >= 64 ? 2 : msn->team_side[g->team];
                hs.tgt = 1;
                hs.tgt_side = sd == us.u[pi].side ? 0 : sd >= 2 ? 2 : 1;
                hs.tgt_bearing = bearing(pg.x, pg.z, x, z, pg.heading);
                float cx, cy;
                hs.tgt_in_view = world_project(&cam, 640, 480, x, y, z, &cx, &cy);
                hs.tgt_x = cx; hs.tgt_y = cy;
                float r = world_object_radius(w, sts.s[stgt].inst, NULL);
                float dx = x - cam.x, dz = z - cam.z, depth = SDL_sqrtf(dx * dx + dz * dz);
                hs.tgt_size = depth > 1 ? 320.0f * r / depth : 12.0f;
                if (hs.tgt_size < 12.0f) hs.tgt_size = 12.0f;
            }
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
            /* the radar's contacts, turned into the gear's frame (0x10088037): the player's group's shown navs
             * (0x100889c9), then the vehicles and structures (0x100882b3), the target / nav on top (0x100885f3) */
            {
                float hr = pg.heading * rad, c = SDL_cosf(hr), sn = SDL_sinf(hr);
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
                /* a vehicle shows with +0x18 & 0x1400 (its GPS +0x1a) and none of 0x800 / 0x4000 / 0x16: 0x10 is
                 * set while it is powered down (modes 10 / 11, 0x1000d9de; state 3, 0x10060491), 2 / 4 once
                 * destroyed; its shape by its side (0 HRHFRGPN, 1 HRHENGPN, 2 HRHNEGPN) */
                for (int i = 0; i < us.n && hs.ncontacts < HUD_MAX_CONTACTS; i++) {
                    const unit *u = &us.u[i];
                    if (i == pi || u->cb.dead || !u->placed) continue;
                    unsigned vf = u->gps >= 0 && u->gps < msn->ngps ? msn->gps[u->gps].vflags : 0x400;
                    if (!(vf & 0x1400) || (vf & 0x4816) || u->mode == 10 || u->mode == 11) continue;
                    float dx = u->s.x - pg.x, dz = u->s.z - pg.z;
                    /* side 1 (0x10042b07): its stealth over the player's sensors by k shows it only within
                     * {750, 500, 300, 150}[k - 1] x 100 of 3D distance (0x100b0d4b, 0x10088182) */
                    if (u->side == 1) {
                        static const int seen[5] = {0, 750, 500, 300, 150};
                        int k = u->cb.gr.stealth - pcp->gr.sensors;
                        if (k > 4) k = 4;
                        float dy = u->s.y - pg.y, d = SDL_sqrtf(dx * dx + dy * dy + dz * dz);
                        if (k >= 1 && d / 100.0f > (float)seen[k]) continue;
                    }
                    if (i == tgt) hs.tgt_contact = hs.ncontacts;
                    hs.contact[hs.ncontacts].x = dx * c - dz * sn;
                    hs.contact[hs.ncontacts].z = dx * sn + dz * c;
                    hs.contact[hs.ncontacts].kind = u->side >= 0 && u->side <= 2 ? u->side : HUD_RADAR_NEUTRAL;
                    hs.ncontacts++;
                }
                /* structures: the object table's +0 & 0x1400 (GT +0x14) and none of 0x1e (destroyed), at their
                 * object's place, by their AFFL team's side (none: 2; 0x10042b22) */
                for (int i = 0; i < msn->ntgt && i < sts.n && hs.ncontacts < HUD_MAX_CONTACTS; i++) {
                    const mission_target *g = &msn->tgt[i];
                    if (!(g->gflags & 0x1400) || (g->gflags & 0x1e) || sts.s[i].dead || g->obj < 0) continue;
                    float dx = msn->obj[g->obj].x - pg.x, dz = msn->obj[g->obj].z - pg.z;
                    int sd = g->team < 0 || g->team >= 64 ? 2 : msn->team_side[g->team];
                    hs.contact[hs.ncontacts].x = dx * c - dz * sn;
                    hs.contact[hs.ncontacts].z = dx * sn + dz * c;
                    hs.contact[hs.ncontacts].kind = sd >= 0 && sd <= 2 ? sd : HUD_RADAR_NEUTRAL;
                    hs.ncontacts++;
                }
            }
            if (hud_msg[0] && ((hud_msg_at && now - hud_msg_at < 5000) || sim_ticks < hud_msg_until)) hs.msg = hud_msg;
            /* the radar's mode and range, the HUD's state (src/radar.h); on the map the contacts at their projections */
            hs.radar_mode = rdr.mode;
            hs.radar_range = (float)radar_range(&rdr);
            hs.hud_off = rdr.hud == 0;
            hs.warn = rdr.msg_until > (int)sim_ticks ? "SATELLITE UPLINK NOT FUNCTIONING" : NULL;
            if (rdr.mode == 4) {
                float hr = pg.heading * rad, c = SDL_cosf(hr), sn = SDL_sinf(hr);
                for (int i = 0; i < hs.ncontacts; i++) {
                    float x = hs.contact[i].x, z = hs.contact[i].z, dx = x * c + z * sn, dz = -x * sn + z * c, px, py;
                    int found;
                    float gy = world_ground_height(w, pg.x + dx, pg.z + dz, &found);
                    if (!world_project(&map_cam, 640, 480, pg.x + dx, gy, pg.z + dz, &px, &py)) px = py = -1000;
                    hs.contact[i].x = px; hs.contact[i].z = py;
                }
            }
            hud_draw(hd, rt.color, rt.w, rt.h, world_palette(w), &hs);
        }
        /* HG_SHOT=<file.ppm>:<frame> writes that frame (a test aid) */
        if (getenv("HG_SHOT")) {
            char sp[512]; int at = 0;
            const char *c = strrchr(getenv("HG_SHOT"), ':');
            if (c) { snprintf(sp, sizeof sp, "%.*s", (int)(c - getenv("HG_SHOT")), getenv("HG_SHOT")); at = atoi(c + 1); }
            if (c && frame == at + 1) {
                FILE *o = fopen(sp, "wb");
                if (o) {
                    fprintf(o, "P6 %d %d 255\n", rt.w, rt.h);
                    for (int i = 0; i < rt.w * rt.h; i++) { uint32_t q = rt.color[i]; fputc(q >> 16 & 255, o); fputc(q >> 8 & 255, o); fputc(q & 255, o); }
                    fclose(o);
                }
            }
        }
        if (sw) {
            /* the palette through the brightness's gamma row (0x1003cd04, the 8-bit renderer's palette set) */
            for (int i = 0; i < rt.w * rt.h; i++) {
                uint32_t c = rt.color[i], o = c & 0xff000000u;
                for (int sh = 0; sh < 24; sh += 8) {
                    unsigned g = gamma6[((c >> sh) & 255) >> 2];
                    o |= (uint32_t)((g << 2) | (g >> 4)) << sh;
                }
                rt.color[i] = o;
            }
        }
        SDL_UpdateTexture(tx, NULL, rt.color, rt.w * 4);
        SDL_RenderTexture(ren, tx, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    /* the objectives for the debriefing (0x1006977c: the player's group's shown ones, done and order byte) */
    stat_result.n = 0;
    stat_result.kills = stat.kills;
    stat_result.crit_zones = pcp->crit_zones;
    stat_result.valid = 1;
    for (int k = 0; has_mtbl && k < world_mission(w)->mtbl[0].nobj && k < 64 && stat_result.n < DEBRIEF_MAX; k++) {
        const mission_objective *ob = &world_mission(w)->mtbl[0].obj[k];
        if (!obj.st[0][k].visible) continue;
        debrief_obj *d = &stat_result.o[stat_result.n++];
        snprintf(d->text, sizeof d->text, "%.63s", ob->text);
        d->done = obj.st[0][k].state == OBJ_DONE;
        d->order = ob->order;
    }
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
    if (mpg) {   /* the scores (0x10035105) */
        mp_end(mpg);
        mp_results[0] = 0;
        for (int k = 0; k < mp_count(mpg); k++) {
            printf("mp score %d %s %d\n", k, mp_name(mpg, k), mp_score(mpg, k));
            size_t l = strlen(mp_results);
            snprintf(mp_results + l, sizeof mp_results - l, "%s\t%d\n", mp_name(mpg, k), mp_score(mpg, k));
        }
        fflush(stdout);
        mp_close(mpg);
        mpg = NULL;
    }
    audio_music_stop();
    audio_stop(vo.voice);
    vo.voice = 0;
    wave_free(&vo.wave);
    sfx_free();
    ob_radio = NULL;
    raster_free(&rt);
    particles_free(parts);
    parts = NULL;
    hud_free(hd);
    world_free(w);
    cover_world = NULL;
    if (sc_dirty) audio_cfg_save(game, &sc);   /* 0x100397ec at the sim's end */
    return quit || fled ? -1 : result;
}

/* Alt+Enter: borderless full screen (SDL3's desktop-mode fullscreen, no border or title bar) <-> a window; the cursor
 * stays the system's (the shell's screens use it) */
/* in full screen the pointer is kept on the 4:3 picture, not in the side bars (the shell's screens read it there) */
static void confine_pointer(SDL_Window *win)
{
    SDL_Renderer *ren = SDL_GetRenderer(win);
    int ww, wh, pw, ph;
    SDL_FRect r;
    if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) || !ren || !SDL_GetRenderLogicalPresentationRect(ren, &r) ||
        !SDL_GetWindowSize(win, &ww, &wh) || !SDL_GetCurrentRenderOutputSize(ren, &pw, &ph) || pw <= 0 || ph <= 0) {
        SDL_SetWindowMouseRect(win, NULL);
        return;
    }
    float sx = (float)ww / (float)pw, sy = (float)wh / (float)ph;   /* output pixels -> window points (Retina) */
    SDL_Rect m = {(int)(r.x * sx + 0.5f), (int)(r.y * sy + 0.5f), (int)(r.w * sx), (int)(r.h * sy)};
    SDL_SetWindowMouseRect(win, &m);
}

static bool SDLCALL fullscreen_toggle(void *ud, SDL_Event *e)
{
    SDL_Window *win = ud;
    if (e->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || e->type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN ||
        e->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        confine_pointer(win);
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && (e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER) &&
        (e->key.mod & SDL_KMOD_ALT)) {
        int fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
        SDL_SetWindowFullscreenMode(win, NULL);
        SDL_SetWindowFullscreen(win, !fs);
    }
    return true;
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
            else if (step[0] == 'Y') {   /* testing: a sample result for the debriefing's score (before W) */
                static debrief_in t = {3, {{"Destroy all Enemies", 1, 1}, {"Destroy Base at Way ECHO", 1, 1},
                                          {"Defend Your Unit", 0, 2}}, 3, 0, 1, 0};
                front_mission_result(fe, &t);
            }
            else if (step[0] == 'X') { front_mission_stats(fe, "Vehicle Kills: 3\nHit Rate: 42%\nHits Taken: 17\nSquad-mate Kills: 1\nSquad-mates Killed: 0"); front_mission_objectives(fe, "Primary: Destroy all Enemies - Complete\nPrimary: Destroy Base at Way ECHO - Complete\nPrimary: Defend Your Unit\n"); }
            else if (step[0] == 's') { front_draw(fe, frame); write_ppm(step + 2, frame, 640, 480); }
            else if (step[0] == 'z') {   /* n ms of real time, the front end ticking (the multiplayer screens' network) */
                for (long n = strtol(step + 1, NULL, 0); n > 0; n -= 10) { SDL_Delay(10); front_tick(fe, SDL_GetTicks()); }
            }
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
    /* borderless full screen on the desktop's mode by default (the picture letterboxed to 4:3); `-w` / --windowed or
     * HG_WINDOWED=1 starts in a window; Alt+Enter switches at any time */
    int windowed = getenv("HG_WINDOWED") && strcmp(getenv("HG_WINDOWED"), "0");
    for (int i = 2; i < argc; i++) if (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--windowed")) windowed = 1;
    SDL_Window *win;
    SDL_Renderer *ren;
    if (!SDL_CreateWindowAndRenderer("Heavy Gear", 1280, 960, SDL_WINDOW_RESIZABLE | (windowed ? 0 : SDL_WINDOW_FULLSCREEN),
                                     &win, &ren)) return 1;
    SDL_AddEventWatch(fullscreen_toggle, win);
    SDL_SetRenderLogicalPresentation(ren, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    confine_pointer(win);
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
        if (getenv("HG_NET_HOST") || getenv("HG_NET_JOIN")) {
            /* the session (the port's lobby: no screens): the host waits for HG_NET_PLAYERS (2) then starts */
            char nerr[128];
            const char *nm = getenv("HG_NET_NAME") ? getenv("HG_NET_NAME") : "Player";
            if (getenv("HG_NET_HOST")) netg = net_host(atoi(getenv("HG_NET_HOST")), nm, nerr, sizeof nerr);
            else {
                char h[128];
                snprintf(h, sizeof h, "%s", getenv("HG_NET_JOIN"));
                char *c = strrchr(h, ':');
                int port = 0;
                if (c) { *c = 0; port = atoi(c + 1); }
                netg = net_join(h, port, nm, nerr, sizeof nerr);
            }
            if (!netg) { fprintf(stderr, "net: %s\n", nerr); return 1; }
            int want = getenv("HG_NET_PLAYERS") ? atoi(getenv("HG_NET_PLAYERS")) : 2;
            Uint64 t0 = SDL_GetTicks();
            while (!net_started(netg) && SDL_GetTicks() - t0 < 60000) {
                net_poll(netg);
                if (net_is_host(netg) && net_players(netg, NULL) >= want) net_start(netg);
                SDL_Delay(10);
            }
            if (net_started(netg) <= 0) { fprintf(stderr, "net: no session\n"); net_close(netg); return 1; }
            fprintf(stderr, "net: player %d of %d\n", net_me(netg), net_players(netg, NULL));
        }
        int res = run_mission(ren, tx, a, argv[1], getenv("HG_MISSION"), netg ? NULL : &sq, getenv("HG_GEAR"));
        if (netg) net_close(netg);
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
                if (e.key.key == SDLK_ESCAPE) front_key(fe, 0x1b);
                if (e.key.key == SDLK_LEFT) front_key(fe, 0x25);
                if (e.key.key == SDLK_RIGHT) front_key(fe, 0x27);
                if (e.key.key == SDLK_DELETE) front_key(fe, 0x2e);
                if (e.key.key == SDLK_RETURN && !(e.key.mod & SDL_KMOD_ALT)) front_key(fe, 0x0d);
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
            net *fn = front_net(fe);   /* a multiplayer game (src/mpscreens.h): the sim takes the session over */
            if (fn) netg = fn;
            int res = run_mission(ren, tx, a, argv[1], scn, fn ? NULL : front_squad(fe), front_player_gear(fe));
            SDL_StartTextInput(win);
            if (fn) { netg = NULL; front_mp_over(fe, mp_results); }
            else if (res < 0) run = 0;
            else {
                front_squad_lost(fe, stat_slot_lost[1], stat_slot_lost[2]);
                front_mission_result(fe, &stat_result);
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
