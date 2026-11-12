/*
 * codecs.h - compressed soundtrack files (MP3, Ogg Vorbis) decoded whole to 16-bit stereo at 44.1 kHz, for the music
 * folder (the original plays CD audio; this is the port's convenience). Third-party decoders: dr_mp3 and stb_vorbis
 * (src/third_party/, public domain / MIT-0; THIRD_PARTY.md).
 */
#ifndef HG_CODECS_H
#define HG_CODECS_H

#include <stdint.h>

/* decode the file at path (by its content: MP3 or Ogg Vorbis) into *pcm (frames x 2 int16, 44.1 kHz, malloc'd);
 * returns the frame count, or -1 */
long codec_decode_file(const char *path, int16_t **pcm);

#endif
