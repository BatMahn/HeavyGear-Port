#include "radio.h"

#include <string.h>

void radio_init(radio *r, const radio_ops *ops)
{
    memset(r, 0, sizeof *r);
    r->ops = *ops;
    r->head = -1;
}

/* 0x10016f28: 1 if the message started */
static int start(radio *r, int i)
{
    radio_msg *m = &r->msg[i];
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
    if (r->head < 0) {
        m->used = 1;
        if (!start(r, s)) { m->used = 0; return 0; }
        r->head = s;
        return 1;
    }
    /* the waiting messages (the head's followers): a duplicate pair is dropped; equal priorities go to the end */
    int last = r->head;
    for (int i = r->msg[r->head].next; i >= 0; i = r->msg[i].next) {
        if (!strcmp(r->msg[i].name[0], m->name[0]) && !strcmp(r->msg[i].name[1], m->name[1])) return 0;
        last = i;
    }
    m->used = 1;
    r->msg[last].next = s;
    return 1;
}

/* 0x10016e7d with the sound callback's chain */
void radio_tick(radio *r)
{
    while (r->head >= 0) {
        if (r->ops.busy(r->ops.ctx, r->handle)) return;
        radio_msg *m = &r->msg[r->head];
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
