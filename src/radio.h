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
 * of names matches one already waiting is dropped when the sound's limit (+2) is -1, else once that many match.
 * The table (0x107f9780, 8 bytes by sound id: priority, limit; 0x10016080 fills 0x2c entries from 0x10108d08, the
 * rest -1) is keyed by the name registry's ids (0x10007abc), which are handed out in the order names are first used
 * and never freed (reset at the sound's start, 0x10015950): every sound played, the effects' and the gear's as well
 * as the voices, takes the next id the first time it is used, so which names the 0x2c entries reach follows the
 * order sounds are first used in the run (in the running original's mission 1: AFXHGWKL 1, AFXHGENG 2, AvoM011S 3,
 * ...). The port keeps the same registry (sound_reg_id; every sound started must pass through it) and table.
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
    /* optional: does the name have a spoken-line file, <name>.sfl in a KEATING folder (0x1006a7a6 loads it whole as
     * the message is queued, the record's sfl handle; the queue only holds it: the start keeps a message whose names
     * are empty but has one, the flush 0x100171f5 keeps such messages, and the removal frees it, 0x100172d8) */
    int  (*sfl)(void *ctx, const char *name);
    /* optional: the voices off (0x1012d488 == 0 or hgsnd's flag 2 clear: the voices' level at 0); a message then shows
     * its text (+0x34) in the message bar for 0x712 ticks instead (0x10016f28 -> 0x1006a0ec(text, 0, 0x712, 0x32)) and
     * lasts that long (+0x2c = the clock + 0x712); show draws it, clock is the sim's tick count (0x10130768) */
    int  (*voices_off)(void *ctx);
    void (*show)(void *ctx, const char *text, int ticks);
    int  (*clock)(void *ctx);
} radio_ops;

typedef struct {
    int  used;
    char name[2][16];
    int  prio;            /* +0x30: the sound table's priority of name[0] */
    int  sfl[2];          /* +0x24 / +0x28: the names' .sfl files were found (radio_ops.sfl) */
    int  next;            /* the queue's next slot, -1 at the end */
    char text[0x34];      /* +0x34: the names' texts joined by " " with a "." after (0x10016aa0; src/radio_text.h) */
    int  end;             /* +0x2c: the clock it ends at while its text shows (voices off), 0 none */
} radio_msg;

typedef struct {
    radio_ops ops;
    radio_msg msg[RADIO_SLOTS];
    int       head;       /* the message speaking, -1 none */
    int       part;       /* which of its names is playing */
    int       handle;
} radio;

/* the sound name registry (0x10007abc): a name's id (1..), given the first time it is used; reset at the sim's start.
 * Every sound the sim starts must register (the port's sfx calls it as the original's play wrappers do). */
void sound_reg_reset(void);
int  sound_reg_id(const char *name);
/* the sound table's priority and limit for an id (0x107f9780 / 0x10016080), -1 / -1 for most */
void sound_reg_entry(int id, int *prio, int *limit);

void radio_init(radio *r, const radio_ops *ops);
/* queue a message of one name (b NULL or "") or two; returns 1 if queued (or started) */
int  radio_say(radio *r, const char *a, const char *b);
/* advance: call every frame */
void radio_tick(radio *r);
/* is anything speaking or waiting? */
int  radio_active(const radio *r);
/* a voice record's text (the first record of that name; "" if none) */
const char *radio_text_of(const char *name);

#endif
