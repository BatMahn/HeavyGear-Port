#include "sfx.h"

#include "audio.h"

#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CACHE 96
#define SFX_CHANNELS 16   /* the sound block's +4 (channel 0 is the radio's) */

static struct {
    hg_archive *a;
    struct { char name[16]; hg_wave w; int ok; } c[CACHE];
    int   n;
    float lx, ly, lz, heading;
    struct { int voice, snd; unsigned flags; } ch[SFX_CHANNELS];
} S;

void sfx_init(hg_archive *a)
{
    sfx_free();
    S.a = a;
}

void sfx_free(void)
{
    for (int i = 0; i < S.n; i++) if (S.c[i].ok) { audio_stop_wave(&S.c[i].w); wave_free(&S.c[i].w); }
    memset(&S, 0, sizeof S);
}

static int get_index(const char *name)
{
    if (!S.a || !name || !name[0]) return -1;
    char key[16];
    int k = 0;
    for (; k < 15 && name[k]; k++) key[k] = (char)tolower((unsigned char)name[k]);
    key[k] = 0;
    for (int i = 0; i < S.n; i++) if (!strcmp(S.c[i].name, key)) return i;
    if (S.n >= CACHE) return -1;
    int i = S.n++;
    strcpy(S.c[i].name, key);
    size_t len;
    uint8_t *d = hga_load(S.a, key, "WAV", &len, NULL);
    S.c[i].ok = d && !wave_parse(d, len, &S.c[i].w);
    free(d);
    return i;
}

/* 0x10007bf9: a free channel, else one of lower priority (its flags' low byte below the new flags) is taken; a
 * channel playing the same sound refuses it when both have 0x100 (or both 0x400; the loops' 0x2000 channels only
 * by 0x100), and with both 0x200 the old one stops. -1: no channel. */
static int channel(int snd, unsigned flags)
{
    int got = -1, lower = -1;
    for (int k = 1; k < SFX_CHANNELS && got < 0; k++) {
        int busy = S.ch[k].voice && audio_playing(S.ch[k].voice);
        if (!busy) { got = k; break; }
        unsigned f = S.ch[k].flags;
        if (f & 0xff00) {
            if (!(f & 0x100) && !(f & 0x2000)) {
                if ((f & 0x400) && (flags & 0x400) && S.ch[k].snd == snd) return -1;
                if ((f & 0x200) && (flags & 0x200) && S.ch[k].snd == snd) { audio_stop(S.ch[k].voice); got = k; break; }
            } else {
                if ((flags & 0x100) && S.ch[k].snd == snd) return -1;
                continue;
            }
        }
        if ((int)(signed char)(f & 0xff) < (int)flags) lower = k;
    }
    if (got < 0) {
        if (lower < 0) return -1;
        got = lower;
        audio_stop(S.ch[got].voice);
    }
    return got;
}

static int start(int snd, float gain, int pan, int delay_ms, unsigned flags)
{
    int k = channel(snd, flags);
    if (k < 0) return 0;
    S.ch[k].snd = snd;
    S.ch[k].flags = flags;
    S.ch[k].voice = audio_play_ex(&S.c[snd].w, gain, pan, delay_ms, (flags & 0x2000) != 0);
    return S.ch[k].voice;
}

void sfx_listener(float x, float y, float z, float heading)
{
    S.lx = x; S.ly = y; S.lz = z; S.heading = heading;
}

int sfx_flat(const char *name, int vol, int pan, unsigned flags)
{
    int i = get_index(name);
    if (i < 0 || !S.c[i].ok || vol < -6000) return 0;
    if (vol > 0) vol = 0;
    if (getenv("HG_SNDLOG")) fprintf(stderr, "sfx %s vol %d pan %d\n", name, vol, pan);
    return start(i, audio_db(vol), pan, 0, flags);
}

/* 0x1000863a's volume, delay and pan for the offset camera - source */
static int place(float dx, float dy, float dz, int quiet, int *vol, int *pan, int *delay_ms)
{
    float d = sqrtf(dx * dx + dy * dy + dz * dz), m = d / 100.0f;
    int v = m > 50000.0f ? -10000 : (int)(-2500.0f * m * m / 250000.0f);
    if (v > 0) v = 0;
    if (quiet) v -= 1000;
    if (v < -10000) v = -10000;
    if (v < -6000) return 0;
    float bearing = atan2f(dx, dz) * 180.0f / 3.14159265f;
    int p = (int)(sinf((S.heading - bearing) * 3.14159265f / 180.0f) * 10000.0f);
    if (p < -7500) p = -7500;
    if (p > 7500) p = 7500;
    *vol = v; *pan = p;
    *delay_ms = (int)(d * 0.00549f * 5.0f);
    return 1;
}

int sfx_at(const char *name, float x, float y, float z, int quiet, unsigned flags)
{
    int i = get_index(name);
    int vol, pan, delay;
    if (i < 0 || !S.c[i].ok || !place(S.lx - x, S.ly - y, S.lz - z, quiet, &vol, &pan, &delay)) return 0;
    if (getenv("HG_SNDLOG")) fprintf(stderr, "sfx %s at vol %d pan %d delay %d ms\n", name, vol, pan, delay);
    return start(i, audio_db(vol), pan, flags & 0x2000 ? 0 : delay, flags);
}

void sfx_move(int voice, float x, float y, float z, int quiet)
{
    int vol, pan, delay;
    if (!place(S.lx - x, S.ly - y, S.lz - z, quiet, &vol, &pan, &delay)) { vol = -10000; pan = 0; }
    audio_adjust(voice, audio_db(vol), pan);
}

void sfx_stop(int voice) { audio_stop(voice); }
int  sfx_playing(int voice) { return audio_playing(voice); }

void sfx_listener_pos(float *x, float *y, float *z) { *x = S.lx; *y = S.ly; *z = S.lz; }
