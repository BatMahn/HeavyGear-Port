#include "audio.h"

#include "fsutil.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RATE 44100
#define VOICES 24

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int wave_parse(const uint8_t *d, size_t len, hg_wave *w)
{
    memset(w, 0, sizeof *w);
    if (len < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return -1;
    int fmt = 0, ch = 0, rate = 0, bits = 0;
    size_t o = 12;
    while (o + 8 <= len) {
        uint32_t n = rd32(d + o + 4);
        const uint8_t *c = d + o + 8;
        size_t avail = len - o - 8;
        if (n > avail) n = (uint32_t)avail;     /* some files' last chunk runs past the end */
        if (!memcmp(d + o, "fmt ", 4) && n >= 16) {
            fmt = rd16(c); ch = rd16(c + 2); rate = (int)rd32(c + 4); bits = rd16(c + 14);
        } else if (!memcmp(d + o, "data", 4)) {
            if (fmt != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16) || rate <= 0) return -1;
            int bps = bits / 8 * ch;
            w->frames = (int)(n / (uint32_t)bps);
            w->channels = ch;
            w->rate = rate;
            w->pcm = malloc((size_t)(w->frames ? w->frames : 1) * ch * sizeof *w->pcm);
            if (!w->pcm) return -1;
            for (int i = 0; i < w->frames * ch; i++)
                w->pcm[i] = bits == 8 ? (int16_t)((c[i] - 128) << 8) : (int16_t)rd16(c + 2 * i);
            return 0;
        }
        o += 8 + n + (n & 1);
    }
    return -1;
}

void wave_free(hg_wave *w)
{
    free(w->pcm);
    memset(w, 0, sizeof *w);
}

/* ---- music sources: a WAV file, or a CD image's audio track (raw 16-bit stereo 44.1 kHz sectors) ---- */

typedef struct {
    FILE    *f;
    long     start;         /* first byte of the track's audio */
    int      sector;        /* bytes per sector in the image (2352, or 2448 with subchannel data) */
    long     nsec;          /* sectors in the track; the audio is the first 2352 bytes of each */
    long     at;            /* current frame */
    long     frames;
    int      wav;           /* a WAV file: start is its data, frames its length */
    int      channels, rate, bits;
} music_src;

static struct {
    SDL_AudioStream *st;
    SDL_Mutex       *mx;
    struct { const hg_wave *w; double pos, step; float vol, gl, gr; long wait; int loop, id; } v[VOICES];
    int              next_id;
    music_src        mus;
    int              mus_on;
    float            sfx_vol, mus_vol;
} A;

static int mus_read(music_src *m, float *out, int frames)
{
    int done = 0;
    uint8_t buf[2448];
    while (done < frames) {
        if (m->at >= m->frames) {           /* loop */
            m->at = 0;
            if (m->frames <= 0) break;
        }
        if (m->wav) {
            int bps = m->bits / 8 * m->channels;
            int n = frames - done;
            if (n > (int)(sizeof buf / bps)) n = (int)(sizeof buf / bps);
            if (n > m->frames - m->at) n = (int)(m->frames - m->at);
            if (fseek(m->f, m->start + m->at * bps, SEEK_SET) || fread(buf, (size_t)bps, (size_t)n, m->f) != (size_t)n) return done;
            /* played at 44.1 kHz, a CD rip's rate (a file at another rate plays off-speed) */
            for (int i = 0; i < n; i++) {
                const uint8_t *p = buf + i * bps;
                float l, r;
                if (m->bits == 8) { l = (p[0] - 128) / 128.0f; r = m->channels == 2 ? (p[1] - 128) / 128.0f : l; }
                else { l = (int16_t)rd16(p) / 32768.0f; r = m->channels == 2 ? (int16_t)rd16(p + 2) / 32768.0f : l; }
                out[2 * (done + i)] = l;
                out[2 * (done + i) + 1] = r;
            }
            m->at += n;
            done += n;
        } else {
            long sec = m->at / 588, in = m->at % 588;   /* 588 stereo frames per 2352-byte sector */
            if (fseek(m->f, m->start + sec * m->sector, SEEK_SET) || fread(buf, 1, 2352, m->f) != 2352) return done;
            int n = (int)(588 - in);
            if (n > frames - done) n = frames - done;
            for (int i = 0; i < n; i++) {
                const uint8_t *p = buf + (in + i) * 4;
                out[2 * (done + i)] = (int16_t)rd16(p) / 32768.0f;
                out[2 * (done + i) + 1] = (int16_t)rd16(p + 2) / 32768.0f;
            }
            m->at += n;
            done += n;
        }
    }
    return done;
}

static void SDLCALL feed(void *ud, SDL_AudioStream *st, int need, int total)
{
    (void)ud; (void)total;
    int frames = need / (int)(2 * sizeof(float));
    if (frames <= 0) return;
    float *mix = calloc((size_t)frames * 2, sizeof(float));
    float *mus = calloc((size_t)frames * 2, sizeof(float));
    if (!mix || !mus) { free(mix); free(mus); return; }
    SDL_LockMutex(A.mx);
    for (int k = 0; k < VOICES; k++) {
        if (!A.v[k].w) continue;
        const hg_wave *w = A.v[k].w;
        float g = A.v[k].vol * A.sfx_vol;
        int i0 = 0;
        if (A.v[k].wait > 0) {
            if (A.v[k].wait >= frames) { A.v[k].wait -= frames; continue; }
            i0 = (int)A.v[k].wait;
            A.v[k].wait = 0;
        }
        for (int i = i0; i < frames; i++) {
            int p = (int)A.v[k].pos;
            if (p >= w->frames) {
                if (!A.v[k].loop || !w->frames) { A.v[k].w = NULL; break; }
                A.v[k].pos -= w->frames;
                p = (int)A.v[k].pos;
            }
            float l = w->pcm[p * w->channels] / 32768.0f, r = w->channels == 2 ? w->pcm[p * 2 + 1] / 32768.0f : l;
            mix[2 * i] += l * g * A.v[k].gl;
            mix[2 * i + 1] += r * g * A.v[k].gr;
            A.v[k].pos += A.v[k].step;
        }
    }
    if (A.mus_on) {
        int got = mus_read(&A.mus, mus, frames);
        if (got < frames && A.mus.frames <= 0) A.mus_on = 0;
        for (int i = 0; i < 2 * got; i++) mix[i] += mus[i] * A.mus_vol;
    }
    SDL_UnlockMutex(A.mx);
    for (int i = 0; i < 2 * frames; i++) mix[i] = mix[i] > 1 ? 1 : mix[i] < -1 ? -1 : mix[i];
    SDL_PutAudioStreamData(st, mix, frames * 2 * (int)sizeof(float));
    free(mix);
    free(mus);
}

int audio_open(void)
{
    if (A.st) return 0;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return -1;
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, RATE};
    A.mx = SDL_CreateMutex();
    A.sfx_vol = A.mus_vol = 1.0f;
    A.st = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!A.st) { SDL_DestroyMutex(A.mx); A.mx = NULL; return -1; }
    SDL_ResumeAudioStreamDevice(A.st);
    return 0;
}

void audio_close(void)
{
    if (!A.st) return;
    SDL_DestroyAudioStream(A.st);
    A.st = NULL;
    if (A.mus.f) fclose(A.mus.f);
    SDL_DestroyMutex(A.mx);
    memset(&A, 0, sizeof A);
}

float audio_db(int hundredths)
{
    if (hundredths <= -10000) return 0;
    return powf(10.0f, (float)hundredths / 2000.0f);
}

/* DirectSound's pan: hundredths of a dB taken off the other side */
static void pan_gains(int pan, float *gl, float *gr)
{
    *gl = pan > 0 ? audio_db(-pan) : 1.0f;
    *gr = pan < 0 ? audio_db(pan) : 1.0f;
}

int audio_play_ex(const hg_wave *w, float vol, int pan, int delay_ms, int loop)
{
    int id = audio_play(w, vol, loop);
    if (!id) return 0;
    SDL_LockMutex(A.mx);
    for (int k = 0; k < VOICES; k++)
        if (A.v[k].w && A.v[k].id == id) {
            pan_gains(pan, &A.v[k].gl, &A.v[k].gr);
            A.v[k].wait = delay_ms > 0 ? (long)delay_ms * RATE / 1000 : 0;
        }
    SDL_UnlockMutex(A.mx);
    return id;
}

void audio_adjust(int voice, float vol, int pan)
{
    if (!A.st || voice <= 0) return;
    SDL_LockMutex(A.mx);
    for (int k = 0; k < VOICES; k++)
        if (A.v[k].w && A.v[k].id == voice) { A.v[k].vol = vol; pan_gains(pan, &A.v[k].gl, &A.v[k].gr); }
    SDL_UnlockMutex(A.mx);
}

int audio_play(const hg_wave *w, float vol, int loop)
{
    if (!A.st || !w || !w->pcm || !w->frames) return 0;
    SDL_LockMutex(A.mx);
    int k = 0;
    while (k < VOICES && A.v[k].w) k++;
    int id = 0;
    if (k < VOICES) {
        id = ++A.next_id;
        A.v[k].w = w; A.v[k].pos = 0; A.v[k].step = (double)w->rate / RATE; A.v[k].vol = vol; A.v[k].loop = loop;
        A.v[k].gl = A.v[k].gr = 1; A.v[k].wait = 0;
        A.v[k].id = id;
    }
    SDL_UnlockMutex(A.mx);
    return id;
}

void audio_stop_wave(const hg_wave *w)
{
    if (!A.st || !w) return;
    SDL_LockMutex(A.mx);
    for (int k = 0; k < VOICES; k++) if (A.v[k].w == w) A.v[k].w = NULL;
    SDL_UnlockMutex(A.mx);
}

void audio_stop(int voice)
{
    if (!A.st || voice <= 0) return;
    SDL_LockMutex(A.mx);
    for (int k = 0; k < VOICES; k++) if (A.v[k].w && A.v[k].id == voice) A.v[k].w = NULL;
    SDL_UnlockMutex(A.mx);
}

int audio_playing(int voice)
{
    int on = 0;
    if (!A.st || voice <= 0) return 0;
    SDL_LockMutex(A.mx);
    for (int k = 0; k < VOICES; k++) if (A.v[k].w && A.v[k].id == voice) on = 1;
    SDL_UnlockMutex(A.mx);
    return on;
}

void audio_set_volume(float sfx, float music)
{
    if (!A.st) return;
    SDL_LockMutex(A.mx);
    A.sfx_vol = sfx;
    A.mus_vol = music;
    SDL_UnlockMutex(A.mx);
}

/* ---- finding a track ---- */

static int open_wav_file(const char *path, music_src *m)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t h[12], c[8];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return -1; }
    int fmt = 0;
    memset(m, 0, sizeof *m);
    while (fread(c, 1, 8, f) == 8) {
        uint32_t n = rd32(c + 4);
        long here = ftell(f);
        if (!memcmp(c, "fmt ", 4)) {
            uint8_t b[16];
            if (n < 16 || fread(b, 1, 16, f) != 16) break;
            fmt = rd16(b); m->channels = rd16(b + 2); m->rate = (int)rd32(b + 4); m->bits = rd16(b + 14);
        } else if (!memcmp(c, "data", 4)) {
            if (fmt != 1 || (m->channels != 1 && m->channels != 2) || (m->bits != 8 && m->bits != 16)) break;
            m->f = f; m->wav = 1; m->start = here;
            m->frames = (long)(n / (uint32_t)(m->bits / 8 * m->channels));
            return 0;
        }
        if (fseek(f, here + (long)n + (long)(n & 1), SEEK_SET)) break;
    }
    fclose(f);
    return -1;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

/* wavmap.ini: [WAVMap] Track<n>=<path>; a path starting ":" is on the DVD drive (DVDDrive=), here the game folder */
static int from_wavmap(const char *dir, int track, music_src *m)
{
    char p[1200], line[512], key[32];
    if (fs_find_ci(dir, "wavmap.ini", p, sizeof p)) return -1;
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    snprintf(key, sizeof key, "track%d", track);
    int in = 0, ok = -1;
    while (fgets(line, sizeof line, f)) {
        trim(line);
        if (line[0] == '[') { in = !SDL_strncasecmp(line, "[wavmap]", 8); continue; }
        char *eq = strchr(line, '=');
        if (!in || !eq) continue;
        *eq = 0;
        trim(line);
        if (SDL_strcasecmp(line, key)) continue;
        char *v = eq + 1;
        while (*v == ' ') v++;
        if (v[0] && v[1] == ':') v += 2;            /* drive letter */
        else if (v[0] == ':') v++;
        for (char *c = v; *c; c++) if (*c == '\\') *c = '/';
        while (*v == '/') v++;
        /* relative to the game folder, matched case-insensitively component by component */
        char q[1200];
        snprintf(q, sizeof q, "%s", dir);
        for (char *t = strtok(v, "/"); t; t = strtok(NULL, "/")) {
            char nq[1200];
            if (fs_find_ci(q, t, nq, sizeof nq)) { q[0] = 0; break; }
            snprintf(q, sizeof q, "%s", nq);
        }
        if (q[0] && !open_wav_file(q, m)) ok = 0;
        break;
    }
    fclose(f);
    return ok;
}

static int from_music_dir(const char *dir, int track, music_src *m)
{
    char md[1200], name[32], p[1200];
    if (fs_find_ci(dir, "music", md, sizeof md)) return -1;
    const char *fmt[] = {"track%02d.wav", "track%d.wav"};
    for (int i = 0; i < 2; i++) {
        snprintf(name, sizeof name, fmt[i], track);
        if (!fs_find_ci(md, name, p, sizeof p) && !open_wav_file(p, m)) return 0;
    }
    return -1;
}

/* an Alcohol 120% image: the .mds descriptor's track table, the .mdf data */
static int from_mds(const char *mds, int track, music_src *m)
{
    FILE *f = fopen(mds, "rb");
    if (!f) return -1;
    uint8_t d[65536];
    size_t n = fread(d, 1, sizeof d, f);
    fclose(f);
    if (n < 0x58 || memcmp(d, "MEDIA DESCRIPTOR", 16)) return -1;
    int nsess = rd16(d + 0x14);
    uint32_t soff = rd32(d + 0x50);
    for (int s = 0; s < nsess; s++) {
        size_t b = soff + (size_t)s * 24;
        if (b + 24 > n) return -1;
        int nall = d[b + 10];
        uint32_t boff = rd32(d + b + 0x14);
        for (int t = 0; t < nall; t++) {
            size_t o = boff + (size_t)t * 80;
            if (o + 80 > n) return -1;
            if (d[o + 4] != track || d[o] != 0xa9) continue;    /* point = track number; mode 0xa9 = audio */
            int secsize = rd16(d + o + 0x10);
            uint32_t ssec = rd32(d + o + 0x24);
            uint64_t start = (uint64_t)rd32(d + o + 0x28) | (uint64_t)rd32(d + o + 0x2c) << 32;
            /* the track runs to the next track's start sector, or to the session's end */
            uint32_t end = (uint32_t)rd32(d + b + 4);
            for (int u = 0; u < nall; u++) {
                size_t q = boff + (size_t)u * 80;
                if (q + 80 <= n && d[q + 4] < 100 && rd32(d + q + 0x24) > ssec && rd32(d + q + 0x24) < end) end = rd32(d + q + 0x24);
            }
            /* the .mdf beside the .mds */
            char p[1700];
            snprintf(p, sizeof p, "%s", mds);
            size_t L = strlen(p);
            if (L < 4) return -1;
            memcpy(p + L - 3, isupper((unsigned char)p[L - 3]) ? "MDF" : "mdf", 3);
            FILE *g = fopen(p, "rb");
            if (!g) { memcpy(p + L - 3, isupper((unsigned char)p[L - 3]) ? "mdf" : "MDF", 3); g = fopen(p, "rb"); }
            if (!g) return -1;
            memset(m, 0, sizeof *m);
            m->f = g; m->start = (long)start; m->sector = secsize; m->nsec = (long)(end - ssec);
            m->frames = m->nsec * 588;
            return 0;
        }
    }
    return -1;
}

/* a .cue sheet with one BIN file (MODE1/2352 + AUDIO tracks, INDEX 01 positions) */
static int from_cue(const char *cue, int track, music_src *m)
{
    FILE *f = fopen(cue, "r");
    if (!f) return -1;
    char line[512], bin[512] = "";
    int cur = 0, found = 0, audio = 0;
    long start = -1, next = -1;
    while (fgets(line, sizeof line, f)) {
        char *l = line;
        while (*l == ' ' || *l == '\t') l++;
        trim(l);
        if (!SDL_strncasecmp(l, "FILE", 4)) {
            char *a = strchr(l, '"'), *b = a ? strchr(a + 1, '"') : NULL;
            if (a && b && !bin[0]) { *b = 0; snprintf(bin, sizeof bin, "%s", a + 1); }
        } else if (!SDL_strncasecmp(l, "TRACK", 5)) {
            cur = atoi(l + 5);
            audio = strstr(l, "AUDIO") != NULL;
        } else if (!SDL_strncasecmp(l, "INDEX 01", 8)) {
            int mm, ss, ff;
            if (sscanf(l + 8, "%d:%d:%d", &mm, &ss, &ff) != 3) continue;
            long sec = ((long)mm * 60 + ss) * 75 + ff;
            if (found && next < 0) next = sec;
            if (cur == track && audio) { start = sec; found = 1; }
        }
    }
    fclose(f);
    if (!found || !bin[0]) return -1;
    char dir[1700], p[1700];
    snprintf(dir, sizeof dir, "%s", cue);
    char *sl = strrchr(dir, '/');
    if (sl) *sl = 0; else snprintf(dir, sizeof dir, ".");
    for (char *c = bin; *c; c++) if (*c == '\\') *c = '/';
    const char *bn = strrchr(bin, '/') ? strrchr(bin, '/') + 1 : bin;
    if (fs_find_ci(dir, bn, p, sizeof p)) return -1;
    FILE *g = fopen(p, "rb");
    if (!g) return -1;
    fseek(g, 0, SEEK_END);
    long size = ftell(g);
    memset(m, 0, sizeof *m);
    m->f = g; m->sector = 2352; m->start = start * 2352;
    m->nsec = (next > start ? next : size / 2352) - start;
    m->frames = m->nsec * 588;
    return 0;
}

static int from_image(const char *dir, int track, music_src *m)
{
    /* any .mds or .cue in the game folder or its cd/ subfolder */
    const char *sub[] = {"", "cd"};
    for (int s = 0; s < 2; s++) {
        char d[1200];
        if (!s) snprintf(d, sizeof d, "%s", dir);
        else if (fs_find_ci(dir, sub[s], d, sizeof d)) continue;
        int n = 0;
        char **names = SDL_GlobDirectory(d, "*", SDL_GLOB_CASEINSENSITIVE, &n);
        if (!names) continue;
        int ok = -1;
        for (int i = 0; i < n && ok; i++) {
            size_t L = strlen(names[i]);
            if (L < 4) continue;
            char p[1600];
            snprintf(p, sizeof p, "%s/%s", d, names[i]);
            if (!SDL_strcasecmp(names[i] + L - 4, ".mds")) ok = from_mds(p, track, m);
            else if (!SDL_strcasecmp(names[i] + L - 4, ".cue")) ok = from_cue(p, track, m);
        }
        SDL_free(names);
        if (!ok) return 0;
    }
    return -1;
}

int audio_music_track(const char *game_dir, int track)
{
    if (!A.st) return -1;
    audio_music_stop();
    music_src m;
    if (from_wavmap(game_dir, track, &m) && from_music_dir(game_dir, track, &m) && from_image(game_dir, track, &m))
        return -1;
    SDL_LockMutex(A.mx);
    A.mus = m;
    A.mus_on = 1;
    SDL_UnlockMutex(A.mx);
    return 0;
}

void audio_music_stop(void)
{
    if (!A.st) return;
    SDL_LockMutex(A.mx);
    if (A.mus.f) fclose(A.mus.f);
    memset(&A.mus, 0, sizeof A.mus);
    A.mus_on = 0;
    SDL_UnlockMutex(A.mx);
}
