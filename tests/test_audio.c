/* the sound settings and decoders: the shell's raw buffers, the levels (0x1000825f), hgsnd.cfg, resampling, FLAC,
 * the .sfl lookup */
#include "../src/audio.h"
#include "../src/codecs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    /* FUN_1003293b: every byte of the record, header included, a 22,050 Hz 8-bit sample */
    uint8_t rec[48] = {'R', 'I', 'F', 'F'};
    for (int i = 4; i < 48; i++) rec[i] = (uint8_t)(i * 5);
    hg_wave w;
    CHECK(!wave_raw8(rec, sizeof rec, 22050, &w));
    CHECK(w.frames == 48 && w.channels == 1 && w.rate == 22050);
    CHECK(w.pcm[0] == (int16_t)(('R' - 128) * 256) && w.pcm[47] == (int16_t)((235 - 128) << 8));
    wave_free(&w);

    /* 0x1000825f: full level keeps the volume (within a unit), 0 sends 0 dB to -10000, half halves towards it */
    CHECK(audio_level_db(0, 0x10000) == 0);
    CHECK(audio_level_db(-2000, 0x10000) == -1999 || audio_level_db(-2000, 0x10000) == -2000);
    CHECK(audio_level_db(0, 0) == -9999 || audio_level_db(0, 0) == -10000);
    int half = audio_level_db(0, 0x8000);
    CHECK(half < -4900 && half > -5100);

    /* hgsnd.cfg: the sim's defaults, and a round trip */
    hg_sndcfg c, d;
    audio_cfg_default(&c);
    CHECK(c.v[SNDCFG_FX] == 0x10000 && c.v[SNDCFG_VOICE] == 0x10000 && c.v[SNDCFG_MUSIC] == 0x10000);
    CHECK(c.v[SNDCFG_FLAGS] == 11 && c.v[17] == 8 && !memcmp(&c.v[18], "mcga.dll", 8));
    const char *dir = "test_audio_tmp";
    mkdir(dir, 0755);
    c.v[SNDCFG_MUSIC] = 0x4000;
    CHECK(!audio_cfg_save(dir, &c));
    CHECK(!audio_cfg_load(dir, &d));
    CHECK(!memcmp(&c, &d, sizeof c));

    /* resampling to 44.1 kHz stereo */
    int16_t in[100];
    for (int i = 0; i < 100; i++) in[i] = (int16_t)(i * 100);
    int16_t *o = NULL;
    long n = codec_to_cd(in, 100, 1, 22050, &o);
    CHECK(n == 200 && o && o[2] == 50 && o[3] == 50 && o[4] == 100);
    free(o);

    /* FLAC: 0.1 s of a 441 Hz sine at 22,050 Hz, decoded to 4410 frames at 44.1 kHz */
    o = NULL;
    n = codec_decode_file("tests/data/sine441.flac", &o);
    CHECK(n == 4410);
    if (n == 4410) {
        double peak = 0;
        for (long i = 0; i < n; i++) peak = fmax(peak, fabs((double)o[2 * i]));
        CHECK(peak > 1000 && o[0] == o[1]);
    }
    free(o);

    /* .sfl: KEATING\<name>.sfl in the game folder, found case-insensitively and read whole */
    char p[256];
    snprintf(p, sizeof p, "%s/KEATING", dir);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/KEATING/AVOM011S.SFL", dir);
    FILE *f = fopen(p, "wb");
    if (f) { fputs("line", f); fclose(f); }
    CHECK(audio_sfl_scan(dir) == 1);
    size_t len = 0;
    uint8_t *s = audio_sfl_load("avom011s", &len);
    CHECK(s && len == 4 && !memcmp(s, "line", 4));
    free(s);
    CHECK(!audio_sfl_load("avom012s", &len));
    remove(p);
    snprintf(p, sizeof p, "%s/KEATING", dir);
    rmdir(p);
    snprintf(p, sizeof p, "%s/hgsnd.cfg", dir);
    remove(p);
    rmdir(dir);

    printf("test_audio: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
