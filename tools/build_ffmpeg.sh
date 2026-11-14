#!/bin/sh
# Builds the small static FFmpeg the cutscenes need (LGPL): the AVI demuxer, the Indeo 3 / 4 / 5 and Cinepak video
# decoders and the PCM / ADPCM audio decoders, nothing else. The port reads the movies through its own I/O callbacks
# (src/movie.c), so no protocols are needed.
#
#   tools/build_ffmpeg.sh <ffmpeg source dir> <install prefix> [cross prefix, e.g. x86_64-w64-mingw32-]
#
# Then: make MOVIES=1 PKG_CONFIG_PATH=<prefix>/lib/pkgconfig AV_LIBS="$(PKG_CONFIG_PATH=<prefix>/lib/pkgconfig
#       pkg-config --static --libs libavformat libavcodec libavutil)"
set -e
src=$1
prefix=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
cross=$3
extra=""
if [ -n "$cross" ]; then
    case "$cross" in
    *mingw32*) extra="--enable-cross-compile --arch=x86_64 --target-os=mingw32 --cross-prefix=$cross" ;;
    *) extra="--enable-cross-compile --cross-prefix=$cross" ;;
    esac
fi
cd "$src"
./configure --prefix="$prefix" $extra \
    --enable-static --disable-shared --enable-pic \
    --disable-programs --disable-doc --disable-network --disable-autodetect --disable-everything \
    --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --disable-postproc \
    --disable-bzlib --disable-zlib --disable-lzma --disable-iconv \
    --enable-demuxer=avi \
    --enable-decoder=indeo3,indeo4,indeo5,cinepak \
    --enable-decoder=pcm_u8,pcm_s16le,adpcm_ima_wav,adpcm_ms \
    --enable-parser=mpegaudio --enable-decoder=mp3 \
    --pkg-config-flags=--static
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
make install
