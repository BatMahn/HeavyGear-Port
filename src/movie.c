#include "movie.h"

#include "audio.h"
#include "fsutil.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- where the file is: a plain file, or a file inside a CD image's ISO 9660 track ---- */

typedef struct {
    FILE    *f;
    long     base;        /* the data track's first byte in the image */
    int      raw;         /* bytes per sector in the image (2048 cooked, 2352 / 2448 raw) */
    int      off;         /* user data within a raw sector (16 for mode 1) */
    uint32_t lba;         /* the file's first sector; plain files: base is 0, lba 0 and raw 0 */
    int64_t  size, pos;
} msrc;

static int src_read(msrc *s, uint8_t *buf, int n)
{
    if (s->pos >= s->size) return 0;
    if (n > s->size - s->pos) n = (int)(s->size - s->pos);
    if (!s->raw) {
        if (fseek(s->f, (long)s->pos, SEEK_SET)) return -1;
        int got = (int)fread(buf, 1, (size_t)n, s->f);
        s->pos += got;
        return got;
    }
    int done = 0;
    while (done < n) {
        int64_t sec = s->pos / 2048, in = s->pos % 2048;
        int k = (int)(2048 - in);
        if (k > n - done) k = n - done;
        long at = s->base + (long)(s->lba + sec) * s->raw + s->off + (long)in;
        if (fseek(s->f, at, SEEK_SET) || fread(buf + done, 1, (size_t)k, s->f) != (size_t)k) break;
        done += k;
        s->pos += k;
    }
    return done;
}

static int rd_sector(FILE *f, int raw, int off, uint32_t lba, uint8_t out[2048])
{
    return fseek(f, (long)lba * raw + off, SEEK_SET) || fread(out, 1, 2048, f) != 2048 ? -1 : 0;
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* the directory record named `name` (case-insensitive, ";1" ignored) in the directory at lba / size */
static int iso_find(FILE *f, int raw, int off, uint32_t lba, uint32_t size, const char *name, uint32_t *olba,
                    uint32_t *osize, int *dir)
{
    uint8_t s[2048];
    for (uint32_t done = 0; done < size; done += 2048, lba++) {
        if (rd_sector(f, raw, off, lba, s)) return -1;
        for (int p = 0; p < 2048;) {
            int len = s[p];
            if (!len) break;
            int nl = s[p + 32];
            char nm[64];
            int k = 0;
            for (; k < nl && k < 63 && s[p + 33 + k] != ';'; k++) nm[k] = (char)s[p + 33 + k];
            nm[k] = 0;
            if (k && nm[k - 1] == '.') nm[k - 1] = 0;
            if (!SDL_strcasecmp(nm, name)) {
                *olba = le32(s + p + 2);
                *osize = le32(s + p + 10);
                *dir = (s[p + 25] & 2) != 0;
                return 0;
            }
            p += len;
        }
    }
    return -1;
}

/* VIDEO/<name>.AVI in an image: its data track at the file's start, cooked or raw */
static int from_image(const char *path, const char *name, msrc *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static const int raws[3][2] = {{2048, 0}, {2352, 16}, {2448, 16}};
    uint8_t pvd[2048];
    for (int r = 0; r < 3; r++) {
        if (rd_sector(f, raws[r][0], raws[r][1], 16, pvd) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5)) continue;
        uint32_t rl = le32(pvd + 156 + 2), rs = le32(pvd + 156 + 10), vl, vs, fl, fsz;
        int dir;
        char fn[32];
        snprintf(fn, sizeof fn, "%s.AVI", name);
        if (iso_find(f, raws[r][0], raws[r][1], rl, rs, "VIDEO", &vl, &vs, &dir) || !dir ||
            iso_find(f, raws[r][0], raws[r][1], vl, vs, fn, &fl, &fsz, &dir) || dir)
            break;
        memset(out, 0, sizeof *out);
        out->f = f; out->raw = raws[r][0]; out->off = raws[r][1]; out->lba = fl; out->size = fsz;
        return 0;
    }
    fclose(f);
    return -1;
}

static int from_file(const char *path, msrc *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    memset(out, 0, sizeof *out);
    out->f = f;
    fseek(f, 0, SEEK_END);
    out->size = ftell(f);
    return 0;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

/* wavmap.ini [WAVMap] DVDMoviePath, relative to the game folder (its drive letter dropped) */
static int from_wavmap(const char *dir, const char *fn, msrc *out)
{
    char p[1200], line[512];
    if (fs_find_ci(dir, "wavmap.ini", p, sizeof p)) return -1;
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    int in = 0, ok = -1;
    while (ok && fgets(line, sizeof line, f)) {
        trim(line);
        if (line[0] == '[') { in = !SDL_strncasecmp(line, "[wavmap]", 8); continue; }
        char *eq = strchr(line, '=');
        if (!in || !eq) continue;
        *eq = 0;
        trim(line);
        if (SDL_strcasecmp(line, "DVDMoviePath")) continue;
        char *v = eq + 1;
        while (*v == ' ') v++;
        if (v[0] && v[1] == ':') v += 2;
        for (char *c = v; *c; c++) if (*c == '\\') *c = '/';
        while (*v == '/') v++;
        char q[1200];
        snprintf(q, sizeof q, "%s", dir);
        for (char *t = strtok(v, "/"); t; t = strtok(NULL, "/")) {
            char nq[1200];
            if (fs_find_ci(q, t, nq, sizeof nq)) { q[0] = 0; break; }
            snprintf(q, sizeof q, "%s", nq);
        }
        char fp[1300];
        if (q[0] && !fs_find_ci(q, fn, fp, sizeof fp) && !from_file(fp, out)) ok = 0;
    }
    fclose(f);
    return ok;
}

static int find(const char *dir, const char *name, msrc *out)
{
    char fn[32], p[1300], d[1200];
    snprintf(fn, sizeof fn, "%s.AVI", name);
    if (!from_wavmap(dir, fn, out)) return 0;
    if (!fs_find_ci(dir, "VIDEO", d, sizeof d) && !fs_find_ci(d, fn, p, sizeof p) && !from_file(p, out)) return 0;
    if (!fs_find_ci(dir, fn, p, sizeof p) && !from_file(p, out)) return 0;
    const char *sub[] = {"", "cd"};
    for (int s = 0; s < 2; s++) {
        if (!s) snprintf(d, sizeof d, "%s", dir);
        else if (fs_find_ci(dir, sub[s], d, sizeof d)) continue;
        int n = 0;
        char **names = SDL_GlobDirectory(d, "*", SDL_GLOB_CASEINSENSITIVE, &n);
        if (!names) continue;
        int ok = -1;
        for (int i = 0; i < n && ok; i++) {
            size_t L = strlen(names[i]);
            if (L < 4) continue;
            const char *e = names[i] + L - 4;
            if (SDL_strcasecmp(e, ".mdf") && SDL_strcasecmp(e, ".bin") && SDL_strcasecmp(e, ".iso") &&
                SDL_strcasecmp(e, ".img"))
                continue;
            snprintf(p, sizeof p, "%s/%s", d, names[i]);
            ok = from_image(p, name, out);
        }
        SDL_free(names);
        if (!ok) return 0;
    }
    return -1;
}

#ifdef HG_MOVIES
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>

int movie_supported(void) { return 1; }

static int io_read(void *opaque, uint8_t *buf, int n)
{
    int got = src_read(opaque, buf, n);
    return got > 0 ? got : AVERROR_EOF;
}

static int64_t io_seek(void *opaque, int64_t off, int whence)
{
    msrc *s = opaque;
    whence &= ~AVSEEK_FORCE;
    if (whence == AVSEEK_SIZE) return s->size;
    if (whence == SEEK_SET) s->pos = off;
    else if (whence == SEEK_CUR) s->pos += off;
    else if (whence == SEEK_END) s->pos = s->size + off;
    else return -1;
    return s->pos;
}

typedef struct {
    AVFormatContext *fc;
    AVIOContext     *io;
    msrc             src;
} demux;

static int dm_open(demux *d, const msrc *src)
{
    memset(d, 0, sizeof *d);
    d->src = *src;
    d->src.pos = 0;
    uint8_t *buf = av_malloc(65536);
    d->io = avio_alloc_context(buf, 65536, 0, &d->src, io_read, NULL, io_seek);
    d->fc = avformat_alloc_context();
    if (!d->io || !d->fc) return -1;
    d->fc->pb = d->io;
    if (avformat_open_input(&d->fc, NULL, NULL, NULL) < 0) { d->fc = NULL; return -1; }
    if (avformat_find_stream_info(d->fc, NULL) < 0) return -1;
    return 0;
}

static void dm_close(demux *d)
{
    if (d->fc) avformat_close_input(&d->fc);
    if (d->io) { av_freep(&d->io->buffer); avio_context_free(&d->io); }
}

static AVCodecContext *open_codec(AVFormatContext *fc, int si)
{
    const AVCodec *c = avcodec_find_decoder(fc->streams[si]->codecpar->codec_id);
    if (!c) return NULL;
    AVCodecContext *cc = avcodec_alloc_context3(c);
    if (!cc || avcodec_parameters_to_context(cc, fc->streams[si]->codecpar) < 0 || avcodec_open2(cc, c, NULL) < 0) {
        avcodec_free_context(&cc);
        return NULL;
    }
    return cc;
}

/* the sound first, whole (two minutes of 11025 Hz mono at most), so the pictures can follow its clock */
static int load_sound(const msrc *src, hg_wave *w)
{
    demux d;
    memset(w, 0, sizeof *w);
    if (dm_open(&d, src)) { dm_close(&d); return -1; }
    int si = av_find_best_stream(d.fc, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    AVCodecContext *cc = si >= 0 ? open_codec(d.fc, si) : NULL;
    if (!cc) { dm_close(&d); return -1; }
    AVPacket *pk = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    int cap = 0, ch = cc->ch_layout.nb_channels > 1 ? 2 : 1;
    w->channels = ch;
    w->rate = cc->sample_rate;
    while (av_read_frame(d.fc, pk) >= 0) {
        if (pk->stream_index == si && avcodec_send_packet(cc, pk) >= 0)
            while (avcodec_receive_frame(cc, fr) >= 0) {
                if (w->frames + fr->nb_samples > cap) {
                    cap = (w->frames + fr->nb_samples) * 2;
                    int16_t *np = realloc(w->pcm, (size_t)cap * (size_t)ch * sizeof *np);
                    if (!np) break;
                    w->pcm = np;
                }
                for (int i = 0; i < fr->nb_samples; i++)
                    for (int c = 0; c < ch; c++) {
                        int16_t v = 0;
                        if (fr->format == AV_SAMPLE_FMT_S16) v = ((int16_t *)fr->data[0])[i * cc->ch_layout.nb_channels + c];
                        else if (fr->format == AV_SAMPLE_FMT_U8) v = (int16_t)((((uint8_t *)fr->data[0])[i * cc->ch_layout.nb_channels + c] - 128) << 8);
                        else if (fr->format == AV_SAMPLE_FMT_S16P) v = ((int16_t *)fr->data[c])[i];
                        w->pcm[(w->frames + i) * ch + c] = v;
                    }
                w->frames += fr->nb_samples;
            }
        av_packet_unref(pk);
    }
    av_frame_free(&fr);
    av_packet_free(&pk);
    avcodec_free_context(&cc);
    dm_close(&d);
    return w->frames ? 0 : -1;
}

static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* any planar YUV frame (Indeo 5 gives 4:1:0) to 0xAARRGGBB, BT.601 */
static void to_rgb(const AVFrame *fr, uint32_t *out)
{
    const AVPixFmtDescriptor *pd = av_pix_fmt_desc_get(fr->format);
    int sx = pd ? pd->log2_chroma_w : 1, sy = pd ? pd->log2_chroma_h : 1;
    for (int y = 0; y < fr->height; y++)
        for (int x = 0; x < fr->width; x++) {
            int Y = fr->data[0][y * fr->linesize[0] + x] - 16;
            int U = fr->data[1][(y >> sy) * fr->linesize[1] + (x >> sx)] - 128;
            int V = fr->data[2][(y >> sy) * fr->linesize[2] + (x >> sx)] - 128;
            int r = (298 * Y + 409 * V + 128) >> 8, g = (298 * Y - 100 * U - 208 * V + 128) >> 8,
                b = (298 * Y + 516 * U + 128) >> 8;
            out[y * fr->width + x] = 0xff000000u | (uint32_t)clamp8(r) << 16 | (uint32_t)clamp8(g) << 8 | clamp8(b);
        }
}

int movie_play(SDL_Renderer *ren, const char *game_dir, const char *name)
{
    msrc src;
    if (!name || !name[0] || find(game_dir, name, &src)) return -1;
    hg_wave snd;
    int have_snd = !load_sound(&src, &snd);
    demux d;
    int ret = -1;
    if (dm_open(&d, &src)) goto done;
    int vi = av_find_best_stream(d.fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    AVCodecContext *vc = vi >= 0 ? open_codec(d.fc, vi) : NULL;
    if (!vc) goto done;
    AVPacket *pk = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    SDL_Texture *tx = NULL;
    uint32_t *rgb = NULL;
    AVRational tb = d.fc->streams[vi]->time_base;
    int voice = have_snd ? audio_play(&snd, 1.0f, 0) : 0;
    Uint64 t0 = SDL_GetTicks();
    ret = 0;
    int stop = 0;
    while (!stop && av_read_frame(d.fc, pk) >= 0) {
        if (pk->stream_index == vi && avcodec_send_packet(vc, pk) >= 0) {
            while (!stop && avcodec_receive_frame(vc, fr) >= 0) {
                if (!tx) {
                    tx = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, fr->width, fr->height);
                    rgb = malloc((size_t)fr->width * (size_t)fr->height * 4);
                    if (!tx || !rgb) { stop = 1; break; }
                }
                to_rgb(fr, rgb);
                SDL_UpdateTexture(tx, NULL, rgb, fr->width * 4);
                /* shown at its time (the frame's pts on the stream's clock, from the start) */
                double at = fr->pts != AV_NOPTS_VALUE ? (double)fr->pts * tb.num / tb.den : 0;
                for (;;) {
                    SDL_Event e;
                    while (SDL_PollEvent(&e)) {
                        if (e.type == SDL_EVENT_QUIT) { stop = 1; ret = 1; }
                        if ((e.type == SDL_EVENT_KEY_DOWN && !(e.key.mod & SDL_KMOD_ALT)) || (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)) stop = 1;   /* WM_KEYDOWN, WM_LBUTTONDOWN (0x10022dce) */
                    }
                    if (stop || (double)(SDL_GetTicks() - t0) / 1000.0 >= at) break;
                    SDL_Delay(2);
                }
                /* the original's player (0x100319d5 -> 0x10022dce, DirectShow): a video window of 640 x 480 (IVideoWindow
                 * Left 0, Top 0, Width 0x280, Height 0x1e0, BorderColor 0) showing the video twice its size
                 * (IBasicVideo DestinationWidth = width x 2, DestinationHeight = height x 2) at DestinationTop
                 * 0xf0 - height, its left 0; on the 640 x 480 screen, scaled to the window as the shell is */
                int ow = 0, oh = 0;
                SDL_RendererLogicalPresentation lm;
                float sc = 1.0f, ox = 0.0f, oy = 0.0f;
                if (!SDL_GetRenderLogicalPresentation(ren, &ow, &oh, &lm) || !ow || !oh) {
                    SDL_GetCurrentRenderOutputSize(ren, &ow, &oh);
                    sc = SDL_min((float)ow / 640.0f, (float)oh / 480.0f);
                    ox = ((float)ow - 640.0f * sc) / 2;
                    oy = ((float)oh - 480.0f * sc) / 2;
                }
                SDL_FRect dst = {ox, oy + (240.0f - (float)fr->height) * sc, (float)fr->width * 2 * sc, (float)fr->height * 2 * sc};
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
                SDL_RenderClear(ren);
                SDL_RenderTexture(ren, tx, NULL, &dst);
                SDL_RenderPresent(ren);
            }
        }
        av_packet_unref(pk);
    }
    /* the sound plays out unless skipped */
    while (!stop && voice && audio_playing(voice)) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) { stop = 1; ret = 1; }
            if ((e.type == SDL_EVENT_KEY_DOWN && !(e.key.mod & SDL_KMOD_ALT)) || (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)) stop = 1;   /* WM_KEYDOWN, WM_LBUTTONDOWN (0x10022dce) */
        }
        SDL_Delay(10);
    }
    audio_stop(voice);
    if (tx) SDL_DestroyTexture(tx);
    free(rgb);
    av_frame_free(&fr);
    av_packet_free(&pk);
    avcodec_free_context(&vc);
done:
    dm_close(&d);
    if (have_snd) { audio_stop_wave(&snd); wave_free(&snd); }
    fclose(src.f);
    return ret;
}

#else

int movie_supported(void) { return 0; }

int movie_play(SDL_Renderer *ren, const char *game_dir, const char *name)
{
    (void)ren;
    (void)src_read;
    msrc src;
    if (!name || !name[0] || find(game_dir, name, &src)) return -1;
    fclose(src.f);
    fprintf(stderr, "movie %s: this build has no video decoder (build with FFmpeg: make MOVIES=1)\n", name);
    return -1;
}

#endif
