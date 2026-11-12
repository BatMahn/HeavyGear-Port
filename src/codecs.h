/*
 * codecs.h - compressed soundtrack files (MP3, Ogg Vorbis, FLAC) decoded whole to 16-bit stereo at 44.1 kHz, for the music
 * folder (the original plays CD audio; this is the port's convenience). Third-party decoders: dr_mp3, dr_flac and stb_vorbis
 * (src/third_party/, public domain / MIT-0; THIRD_PARTY.md).
 */
#ifndef HG_CODECS_H
#define HG_CODECS_H

#include <stdint.h>

/* decode the file at path (by its content: Ogg Vorbis, FLAC, else MP3) into *pcm (frames x 2 int16, 44.1 kHz, malloc'd);
 * returns the frame count, or -1 */
long codec_decode_file(const char *path, int16_t **pcm);
/* frames of ch-channel PCM at rate to 44.1 kHz stereo (linear interpolation); the frame count or -1 */
long codec_to_cd(const int16_t *in, long frames, int ch, int rate, int16_t **out);

#endif
