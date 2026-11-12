#include "codecs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "third_party/dr_mp3.h"
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "third_party/stb_vorbis.c"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "third_party/dr_flac.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static uint8_t *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *d = NULL;
    if (!fseek(f, 0, SEEK_END)) {
        long n = ftell(f);
        if (n > 0 && !fseek(f, 0, SEEK_SET) && (d = malloc((size_t)n))) {
            if (fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
            else *len = (size_t)n;
        }
    }
    fclose(f);
    return d;
}

/* to stereo 44.1 kHz (linear interpolation when the file's rate differs) */
long codec_to_cd(const int16_t *in, long frames, int ch, int rate, int16_t **out)
{
    if (frames <= 0 || ch < 1 || rate <= 0) return -1;
    long n = rate == 44100 ? frames : (long)((double)frames * 44100.0 / rate);
    int16_t *o = malloc((size_t)(n > 0 ? n : 1) * 4);
    if (!o) return -1;
    for (long i = 0; i < n; i++) {
        double p = rate == 44100 ? (double)i : (double)i * rate / 44100.0;
        long a = (long)p;
        double t = p - (double)a;
        long b = a + 1 < frames ? a + 1 : a;
        for (int c = 0; c < 2; c++) {
            int cc = ch == 1 ? 0 : c;
            double v = in[a * ch + cc] * (1 - t) + in[b * ch + cc] * t;
            o[i * 2 + c] = (int16_t)v;
        }
    }
    *out = o;
    return n;
}

long codec_decode_file(const char *path, int16_t **pcm)
{
    size_t len = 0;
    uint8_t *d = read_all(path, &len);
    if (!d) return -1;
    long r = -1;
    if (len >= 4 && !memcmp(d, "OggS", 4)) {
        int ch = 0, rate = 0;
        short *s = NULL;
        int frames = stb_vorbis_decode_memory(d, (int)len, &ch, &rate, &s);
        if (frames > 0 && s) r = codec_to_cd(s, frames, ch, rate, pcm);
        free(s);
    } else if (len >= 4 && !memcmp(d, "fLaC", 4)) {
        unsigned int ch = 0, rate = 0;
        drflac_uint64 frames = 0;
        drflac_int16 *s = drflac_open_memory_and_read_pcm_frames_s16(d, len, &ch, &rate, &frames, NULL);
        if (s && frames > 0) r = codec_to_cd(s, (long)frames, (int)ch, (int)rate, pcm);
        drflac_free(s, NULL);
    } else {
        drmp3_config cfg;
        drmp3_uint64 frames = 0;
        drmp3_int16 *s = drmp3_open_memory_and_read_pcm_frames_s16(d, len, &cfg, &frames, NULL);
        if (s && frames > 0) r = codec_to_cd(s, (long)frames, (int)cfg.channels, (int)cfg.sampleRate, pcm);
        drmp3_free(s, NULL);
    }
    free(d);
    return r;
}
