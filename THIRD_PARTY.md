# Third-party components

| Component | Where | Version | Licence | Used for |
|---|---|---|---|---|
| dr_mp3 (David Reid, https://github.com/mackron/dr_libs) | `src/third_party/dr_mp3.h` | 0.7.5 (master, Nov 2026) | public domain (Unlicense) or MIT-0, at your choice | decoding the soundtrack's MP3 files |
| dr_flac (David Reid, https://github.com/mackron/dr_libs) | `src/third_party/dr_flac.h` | 0.13.5 (master, Nov 2026) | public domain (Unlicense) or MIT-0, at your choice | decoding the soundtrack's FLAC files |
| stb_vorbis (Sean Barrett, https://github.com/nothings/stb) | `src/third_party/stb_vorbis.c` | 1.23 | public domain (Unlicense) or MIT, at your choice | decoding the soundtrack's Ogg Vorbis files |

| FFmpeg (https://ffmpeg.org) | built by `tools/build_ffmpeg.sh` and linked statically into the release builds (not in this repository) | 7.1.1 | LGPL-2.1-or-later (this configuration: no GPL or non-free parts) | decoding the cutscenes (AVI, Indeo 5, PCM) |

FFmpeg's source is at https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz; `tools/build_ffmpeg.sh` is the exact configuration
the releases use, so the release binaries can be rebuilt (and relinked with another FFmpeg) from this repository.

The three bundled files are unmodified; their licence texts are at their ends. The port's own source is GPL-2.0-or-later (LICENSE).
