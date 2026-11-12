/*
 * audio.h - the port's sound: a software mixer on one SDL3 audio stream (44.1 kHz stereo), for the game's WAV
 * samples (8- or 16-bit PCM, any rate; the shell plays them as 22.05 kHz 8-bit mono, FUN_1003293b) and the music.
 *
 * Music is the game CD's audio tracks: a mission names one ("track2": CD track 2, HGSIM 0x10015cdc). The original
 * plays them from the CD (MCI) or, when the game folder has a wavmap.ini, from the WAV files it lists
 * ([WAVMap] Track<n>=<file>, HGSIM 0x10015a38). The port takes, in order: wavmap.ini, a music/track<nn>.wav file,
 * then a CD image in the game folder (.mds + .mdf, or .cue + .bin) read directly.
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
/* overall volumes, 0..1 */
void audio_set_volume(float sfx, float music);

#endif
