/*
 * radio.h - the sim's spoken messages (HGSIM's radio queue, 0x10016aa0 / 0x10016e7d / 0x10016f28).
 *
 * Every voice the sim speaks (objective and mission results, the gear computer's reports, the radio commands) goes
 * through one queue of 8 messages (0x1012fe88, 0x6b bytes each). A message is one or two sound names (+4, +0x14;
 * the computer's reports pair a subject, "avonhead", with a verdict, "avondamg") and a text (+0x34, shown instead
 * when the sound is off). One message speaks at a time (0x101301e0 the head, 0x101301e4 set while it plays); when it
 * ends (the sound step, 0x1002dc30: the channel's chain at +0x334 played next, else 0x101301e4 cleared) the next
 * one starts (0x10016e7d), skipping any that can't start.
 *
 * Starting (0x10016f28): one name plays that sound; two names need both sounds to exist (0x100a2bc9, "WAV" with
 * the language variant first) and play the first with the second chained after it (flags 0x5622 vs 0x2b11).
 *
 * Queueing (0x10016aa0): a free slot (none: dropped); priority from the sound table (0x107f9780 by the name's sound
 * id, 0x10007abc), placed before the first waiting message of lower priority, else at the end. A message whose pair
 * of names matches one already waiting is dropped (the table's +2 limit is -1 for the voices: INFERRED, the table
 * 0x10108d08 only fills ids of the effects). The voices all get priority -1 here, so the queue is first come, first
 * served.
 */
#ifndef HG_RADIO_H
#define HG_RADIO_H

#define RADIO_SLOTS 8

typedef struct {
    /* start a sound by name: a handle > 0, or 0 if it doesn't exist / can't play */
    int  (*start)(void *ctx, const char *name);
    /* does the sound still play? */
    int  (*busy)(void *ctx, int handle);
    /* is there such a sound? (both names are checked before a pair starts) */
    int  (*exists)(void *ctx, const char *name);
    void *ctx;
} radio_ops;

typedef struct {
    int  used;
    char name[2][16];
    int  next;            /* the queue's next slot, -1 at the end */
} radio_msg;

typedef struct {
    radio_ops ops;
    radio_msg msg[RADIO_SLOTS];
    int       head;       /* the message speaking, -1 none */
    int       part;       /* which of its names is playing */
    int       handle;
} radio;

void radio_init(radio *r, const radio_ops *ops);
/* queue a message of one name (b NULL or "") or two; returns 1 if queued (or started) */
int  radio_say(radio *r, const char *a, const char *b);
/* advance: call every frame */
void radio_tick(radio *r);
/* is anything speaking or waiting? */
int  radio_active(const radio *r);

#endif
