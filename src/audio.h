/*
 * audio.h - the port's sound: a software mixer on one SDL3 audio stream (44.1 kHz stereo), for the game's WAV
 * samples (8- or 16-bit PCM, any rate; the shell plays them as 22.05 kHz 8-bit mono, FUN_1003293b) and the music.
 *
 * Music is the game CD's audio tracks: a mission names one ("track2": CD track 2, HGSIM 0x10015cdc). The original
 * plays them from the CD (MCI) or, when the game folder has a wavmap.ini, from the WAV files it lists
 * ([WAVMap] Track<n>=<file>, HGSIM 0x10015a38). The port takes, in order: wavmap.ini, a music/track<nn>.wav file,
 * then a CD image in the game folder (.mds + .mdf, or .cue + .bin) read directly. A file at another rate than 44.1
 * kHz is resampled (linear); WAV, MP3, Ogg Vorbis and FLAC files are read.
 */
#ifndef HG_AUDIO_H
#define HG_AUDIO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t *pcm;       /* interleaved */
    int      frames, channels, rate;
} hg_wave;

/* parse a RIFF WAVE (PCM 8 / 16-bit, mono / stereo); 0 on success */
int  wave_parse(const uint8_t *d, size_t len, hg_wave *w);
void wave_free(hg_wave *w);

/* 0 on success; without an audio device everything below does nothing */
int  audio_open(void);
void audio_close(void);
/* play a sample (the wave must outlive the voice); vol 0..1. Returns a voice id (> 0), 0 if none. */
int  audio_play(const hg_wave *w, float vol, int loop);
/* pan in DirectSound's hundredths of a dB (-10000 left .. 10000 right), the start delayed by delay_ms */
int  audio_play_ex(const hg_wave *w, float vol, int pan, int delay_ms, int loop);
/* change a playing voice's volume and pan */
void audio_adjust(int voice, float vol, int pan);
/* DirectSound's volume, hundredths of a dB (0 full, -10000 silent), as a gain */
float audio_db(int hundredths);
void audio_stop(int voice);
/* stop every voice playing this wave (before freeing it) */
void audio_stop_wave(const hg_wave *w);
int  audio_playing(int voice);
/* a CD track (see above) from the game folder, looping as the sim does (it restarts the track when it ends,
 * HGSIM 0x10015e1a); 0 if a source was found */
int  audio_music_track(const char *game_dir, int track);
void audio_music_stop(void);
/* overall volumes, 0..1 (the port's own scale on top of the levels below) */
void audio_set_volume(float sfx, float music);

/* The shell's sounds (HGSHELL FUN_1003293b the screen loop, FUN_10032bb2 the voice): a DirectSound buffer of fixed
 * format 22,050 Hz 8-bit mono (block align 1) the size of the archive record, filled with the whole record, RIFF
 * header included - so the header's bytes play as samples at the start (and, looped, at every pass). */
int  wave_raw8(const uint8_t *d, size_t len, int rate, hg_wave *w);

/* a voice's playback rate (IDirectSoundBuffer::SetFrequency, vtable +0x44), Hz */
void audio_set_rate(int voice, int hz);

/* hgsnd.cfg (0x6c bytes; HGSIM 0x100396ca reads it at the start, 0x100397ec writes it at the end; HGSHELL
 * FUN_1011dfa3 / FUN_1011e206). The sound part (HGSIM 0x10015450 / 0x1001550d / 0x1001569c, by their index):
 *  v[0] index 3 (no sound call reads it), v[1] index 0 the effects' level (0x10108c24, passed to every effect play,
 *  0x1000825f), v[2] index 1 the voices' level (0x10108c28, the radio's plays; 0 clears flag 2: voices off, the
 *  text shown instead), v[3] index 2 the music's level (0x10108c2c; 0 clears flags 4 / 8 and stops the music),
 *  v[4] flags. Levels are 16.16 (0x10000 full). The rest is the shell's display settings (v[0x12] the 8-bit
 *  driver's name, ...). Defaults when the file is missing: the sim's table 0x10108c20 (27 dwords). */
typedef struct { int32_t v[27]; } hg_sndcfg;
enum { SNDCFG_V3 = 0, SNDCFG_FX = 1, SNDCFG_VOICE = 2, SNDCFG_MUSIC = 3, SNDCFG_FLAGS = 4 };
void audio_cfg_default(hg_sndcfg *c);
/* from / to the game folder; 0 on success (load fills the defaults on failure) */
int  audio_cfg_load(const char *game_dir, hg_sndcfg *c);
int  audio_cfg_save(const char *game_dir, const hg_sndcfg *c);
/* 0x1000825f: a play's volume (hundredths of a dB) under a 16.16 level: vol - (level - 0xffff) x (vol - 10000) /
 * 65535, truncated; the caller skips the sound below -6000 and holds it to 0 */
int  audio_level_db(int vol, int level);
/* the levels in force (16.16): effects, voices, music. The music's: a CD image's track as the CD's aux volume
 * (auxSetVolume, min(level, 0xffff) linear, 0x1008bd06), a file's (wavmap.ini's WAVs, the port's MP3 / Ogg / FLAC)
 * as its DirectSound buffer's volume -3333 + 3333 x min(level, 0xffff) / 65535 (0x1002e855) */
void audio_set_levels(int fx, int voice, int music);
int  audio_level(int which);   /* SNDCFG_FX / SNDCFG_VOICE / SNDCFG_MUSIC */
void audio_cfg_apply(const hg_sndcfg *c);

/* The spoken lines' .sfl files (HGSIM 0x1006a65c at the sim's start: FindFirstFile "keating\\*.sfl" in the game
 * folder, else "<CD drive>:\\keating\\*.sfl"; every file found is listed, 0x1006a560). Neither disc has the folder.
 * The port looks in the game folder's KEATING folder (case-insensitive), then a "cd" subfolder's (the port's stand-in
 * for the CD root). audio_sfl_load: 0x1006a7a6, <name>.sfl read whole if listed (NULL if not). */
int      audio_sfl_scan(const char *game_dir);
uint8_t *audio_sfl_load(const char *name, size_t *len);

#endif
