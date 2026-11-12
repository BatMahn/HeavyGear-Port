/*
 * test_objectives - the objective rules (HGSIM 0x100684db / 0x1006932f) on a made-up table shaped like mission 1's:
 * a start objective, "destroy" and "defend" mandatory ones, a "complete objective 2" action gated on the destroy,
 * and a "lose" action gated on the defence failing.
 */
#include "../src/objectives.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int dead[4];
static int player_dead;
static int vdead_p(void *c, int g) { (void)c; return g < 0 ? player_dead : g < 4 ? dead[g] : 0; }
static int sdead(void *c, int t) { (void)c; (void)t; return 0; }
static int lead(void *c, int g, float *x, float *y, float *z, int *f) { (void)c; (void)g; *x = *y = *z = 0; *f = 0; return 1; }

static mission_objective ob[5];
static mission m;

static void setup(void)
{
    memset(&m, 0, sizeof m);
    memset(ob, 0, sizeof ob);
    m.mtbl[0].present = 1;
    m.mtbl[0].limit = -1;
    m.mtbl[0].nobj = 5;
    m.mtbl[0].obj = ob;
    for (int k = 0; k < 5; k++) { ob[k].limit = -1; ob[k].arg_group = ob[k].arg_obj = -1; }
    ob[0].type = 0x10; ob[0].must = 1;
    ob[1].type = 2; ob[1].must = 1; ob[1].ncond = 1; ob[1].cond[0].kind = 1; strcpy(ob[1].tag, "ENEMY");
    ob[2].type = 4; ob[2].must = 1; ob[2].ncond = 1; ob[2].cond[0].kind = 1; strcpy(ob[2].tag, "USERSTAR");
    ob[3].type = 0x100000; ob[3].ncond = 1; ob[3].cond[0].kind = 2; ob[3].cond[0].obj = 1; ob[3].arg_group = 0; ob[3].arg_obj = 2;
    ob[4].type = 0x20000; ob[4].ncond = 1; ob[4].cond[0].kind = 3; ob[4].cond[0].obj = 2; ob[4].arg_group = 0;
    static mission_gps gps[2];
    memset(gps, 0, sizeof gps);
    strcpy(gps[0].tag, "ENEMY"); gps[0].mask = 0x737; gps[0].group = 1;
    strcpy(gps[1].tag, "ENEMY"); gps[1].mask = 0x7; gps[1].group = 1;
    /* the order: the MTBL (0), the BTAG ENEMY's scope (1), the two GPS (2, 3); USERSTAR at the end */
    m.mtbl[0].seq = 0;
    gps[0].scope = gps[1].scope = 1; gps[0].seq = 2; gps[1].seq = 3;
    m.reg_seq = 4; m.user_seq = -1;
    m.gps = gps; m.ngps = 2;
}

int main(void)
{
    static objectives o;
    setup();
    obj_world w = {NULL, vdead_p, sdead, lead, NULL, NULL, NULL};
    objectives_init(&o, &m, -1, 0x4007, 0);
    CHECK(o.st[0][1].ntgt == 2 && o.st[0][2].ntgt == 1, "targets %d %d", o.st[0][1].ntgt, o.st[0][2].ntgt);
    {   /* an MTBL loaded after the scope opened takes none of its targets (0x1006663a found it unloaded) */
        static objectives o2;
        m.mtbl[0].seq = 1;
        objectives_init(&o2, &m, -1, 0x4007, 0);
        CHECK(o2.st[0][1].ntgt == 0 && o2.st[0][2].ntgt == 1, "late MTBL %d %d", o2.st[0][1].ntgt, o2.st[0][2].ntgt);
        m.mtbl[0].seq = 0;
    }
    int r = objectives_tick(&o, 181, &w, 0);
    CHECK(r == GROUP_RUNNING && o.st[0][0].state == OBJ_DONE && o.st[0][1].state == OBJ_PENDING, "start");
    dead[0] = 1;
    r = objectives_tick(&o, 362, &w, 0);
    CHECK(r == GROUP_RUNNING && o.st[0][1].state == OBJ_PENDING, "one of two destroyed");
    dead[1] = 1;
    r = objectives_tick(&o, 543, &w, 0);
    /* destroy done -> the action completes the defence -> every mandatory objective done -> won */
    CHECK(o.st[0][1].state == OBJ_DONE && o.st[0][2].state == OBJ_DONE && r == GROUP_WON, "won: %d %d %d",
          o.st[0][1].state, o.st[0][2].state, r);

    setup();
    memset(dead, 0, sizeof dead);
    objectives_init(&o, &m, -1, 0x4007, 0);
    objectives_tick(&o, 181, &w, 0);
    player_dead = 1;
    r = objectives_tick(&o, 362, &w, 0);
    CHECK(o.st[0][2].state == OBJ_FAILED && r == GROUP_LOST, "lost: %d %d", o.st[0][2].state, r);

    /* the mission channel (0x100096c3 / 0x10009d0e): a shown 0x8000 objective is its first message; sending it makes
     * it a 0x10000, which completes at its next step */
    setup();
    player_dead = 0;
    memset(dead, 0, sizeof dead);
    ob[3].type = 0x8000; ob[3].visible = 1; ob[3].ncond = 1; ob[3].cond[0].kind = 2; ob[3].cond[0].obj = 0;
    ob[3].arg_group = -1;
    objectives_init(&o, &m, -1, 0x4007, 0);
    objectives_tick(&o, 181, &w, 0);
    CHECK(objectives_radio_slot(&o, 0, 0) == 3 && objectives_radio_slot(&o, 0, 1) == -1, "radio slot");
    objectives_tick(&o, 362, &w, 0);
    CHECK(o.st[0][3].state == OBJ_PENDING, "radio objective waits");
    objectives_call(&o, 0, 3);
    objectives_tick(&o, 543, &w, 0);
    CHECK(o.st[0][3].state == OBJ_DONE && objectives_radio_slot(&o, 0, 0) == -1, "sent: %d", o.st[0][3].state);

    if (fails) { printf("test_objectives: %d failures\n", fails); return 1; }
    printf("test_objectives: ok\n");
    return 0;
}
