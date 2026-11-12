#include "radio.h"

#include <string.h>
#include <strings.h>

#include "radio_text.h"

/* The sound name registry (0x10007abc): ids from 1, each name's the first free entry when first used, never freed
 * (0x10835200 counts uses, nothing counts down); reset at the sound's start (0x10015950 -> 0x10007a70). */
#define SOUND_REG_MAX 0x4b0
static char reg_name[SOUND_REG_MAX][16];
static int  reg_n;

void sound_reg_reset(void) { reg_n = 0; }

int sound_reg_id(const char *name)
{
    if (!name) name = "";
    for (int i = 1; i <= reg_n; i++) if (!strcmp(reg_name[i], name)) return i;
    if (reg_n + 1 >= SOUND_REG_MAX) return 0;
    reg_n++;
    strncpy(reg_name[reg_n], name, sizeof reg_name[reg_n] - 1);
    reg_name[reg_n][sizeof reg_name[reg_n] - 1] = 0;
    return reg_n;
}

/* 0x10016080: the sound table (0x107f9780, by id: priority, limit, +4) all -1 but the 0x2c entries of 0x10108d08 */
static const short sound_tab[0x2c][4] = {
    {11, 80, 1, -1}, {10, 80, 1, -1}, {60, 80, 1, -1}, {201, 10, 1, -1}, {109, 80, -1, -1}, {108, 80, -1, -1},
    {100, 80, -1, -1}, {101, 80, -1, -1}, {107, 80, -1, -1}, {99, 80, -1, -1}, {60, 80, -1, -1}, {71, 80, -1, -1},
    {1, 80, -1, -1}, {58, 80, -1, -1}, {64, 80, -1, -1}, {8, 80, -1, -1}, {183, 80, -1, -1}, {127, 80, -1, -1},
    {154, -1, -1, -1}, {135, 100, 1, -1}, {121, -1, 1, -1}, {98, 100, -1, -1}, {96, 80, -1, -1}, {158, 80, -1, -1},
    {162, 80, -1, -1}, {144, 100, -1, -1}, {177, 80, -1, -1}, {160, 80, -1, -1}, {164, 80, -1, -1},
    {147, 80, -1, -1}, {172, 80, -1, -1}, {175, 80, -1, -1}, {179, 80, -1, -1}, {152, 0, -1, -1}, {200, 0, 1, -1},
    {206, 80, 2, -1}, {145, 80, 2, -1}, {146, 80, 2, -1}, {169, 80, 2, -1}, {161, 80, 2, -1}, {126, 80, 2, -1},
    {195, 80, 2, -1}, {196, 80, 2, -1}, {148, 100, 4, -1},
};

/* the entry for an id (a later entry for the same id overwrites, as the fill's order does) */
void sound_reg_entry(int id, int *prio, int *limit)
{
    *prio = -1; *limit = -1;
    for (int k = 0; k < 0x2c; k++)
        if (sound_tab[k][0] == id && id > 0 && id < SOUND_REG_MAX) { *prio = sound_tab[k][1]; *limit = sound_tab[k][2]; }
}

void radio_init(radio *r, const radio_ops *ops)
{
    memset(r, 0, sizeof *r);
    r->ops = *ops;
    r->head = -1;
}

/* 0x10016f28: 1 if the message started */
const char *radio_text_of(const char *name)
{
    if (!name || !name[0]) return "";
    for (size_t k = 0; k < sizeof radio_text / sizeof radio_text[0]; k++)
        if (!strcasecmp(radio_text[k].name, name)) return radio_text[k].text;
    return "";
}

static int start(radio *r, int i)
{
    radio_msg *m = &r->msg[i];
    m->end = 0;
    if (r->ops.voices_off && r->ops.voices_off(r->ops.ctx)) {
        /* 0x10016f28, the voices off: the text in the message bar for 0x712 ticks (none: nothing, the message ends) */
        if (!m->text[0]) return 0;
        if (r->ops.show) r->ops.show(r->ops.ctx, m->text, 0x712);
        m->end = (r->ops.clock ? r->ops.clock(r->ops.ctx) : 0) + 0x712;
        r->handle = 0;
        r->part = 1;
        return 1;
    }
    const char *a = m->name[0], *b = m->name[1];
    if (!a[0]) { a = b; b = ""; }
    if (!a[0]) return 0;
    if (b[0] && (!r->ops.exists(r->ops.ctx, b) || !r->ops.exists(r->ops.ctx, a))) return 0;
    r->handle = r->ops.start(r->ops.ctx, a);
    if (r->handle <= 0) return 0;
    /* the message's names shuffled so that part 0 is the one playing */
    if (a != m->name[0]) { memmove(m->name[0], m->name[1], sizeof m->name[0]); m->name[1][0] = 0; }
    r->part = 0;
    return 1;
}

int radio_say(radio *r, const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    if (!a[0] && !b[0]) return 0;
    int s = -1;
    for (int i = 0; i < RADIO_SLOTS && s < 0; i++) if (!r->msg[i].used) s = i;
    if (s < 0) return 0;
    radio_msg *m = &r->msg[s];
    memset(m, 0, sizeof *m);
    strncpy(m->name[0], a, sizeof m->name[0] - 1);
    strncpy(m->name[1], b, sizeof m->name[1] - 1);
    m->next = -1;
    /* the text (0x10016aa0): the first name's, then " " and the second's, then "." - at most 0x33 characters */
    {
        const char *ta = radio_text_of(a), *tb = b[0] ? radio_text_of(b) : "";
        size_t cap = sizeof m->text - 1;
        strncat(m->text, ta, cap);
        if (b[0] && tb[0]) { strncat(m->text, " ", cap - strlen(m->text)); strncat(m->text, tb, cap - strlen(m->text)); }
        if (m->text[0]) strncat(m->text, ".", cap - strlen(m->text));
    }
    for (int k = 0; k < 2; k++) m->sfl[k] = r->ops.sfl && m->name[k][0] ? r->ops.sfl(r->ops.ctx, m->name[k]) : 0;
    /* the priority of the first name's id (every caller passes -1: from the table) */
    int limit;
    if (m->name[0][0]) sound_reg_entry(sound_reg_id(m->name[0]), &m->prio, &limit);
    else m->prio = -1;
    if (r->head < 0) {
        m->used = 1;
        if (!start(r, s)) { m->used = 0; return 0; }
        r->head = s;
        return 1;
    }
    /* the waiting messages (the head's followers): a pair matching one waiting is dropped when the first name's limit
     * is -1, else once that many match; the message goes before the first waiting one of lower priority, else at
     * the end (0x10016aa0) */
    int prev = r->head, count = 0;
    for (int i = r->msg[r->head].next; i >= 0; prev = i, i = r->msg[i].next) {
        if (!strcmp(r->msg[i].name[0], m->name[0]) && !strcmp(r->msg[i].name[1], m->name[1])) {
            int pr, lim;
            sound_reg_entry(sound_reg_id(m->name[0]), &pr, &lim);
            if (lim == -1 || lim <= ++count) return 0;
        }
        if (r->msg[i].prio < m->prio) {
            m->used = 1;
            r->msg[prev].next = s;
            m->next = i;
            return 1;
        }
    }
    m->used = 1;
    r->msg[prev].next = s;
    return 1;
}

/* 0x10016e7d with the sound callback's chain */
void radio_tick(radio *r)
{
    while (r->head >= 0) {
        radio_msg *m = &r->msg[r->head];
        if (m->end) {
            if (r->ops.clock && r->ops.clock(r->ops.ctx) < m->end) return;
        } else if (r->ops.busy(r->ops.ctx, r->handle)) return;
        if (r->part == 0 && m->name[1][0]) {
            r->part = 1;
            r->handle = r->ops.start(r->ops.ctx, m->name[1]);
            if (r->handle > 0) return;
        }
        /* the message is over: the next one that starts */
        int n = m->next;
        m->used = 0;
        r->head = -1;
        while (n >= 0) {
            int after = r->msg[n].next;
            if (start(r, n)) { r->head = n; break; }
            r->msg[n].used = 0;
            n = after;
        }
        if (r->head < 0) return;
    }
}

int radio_active(const radio *r) { return r->head >= 0; }
