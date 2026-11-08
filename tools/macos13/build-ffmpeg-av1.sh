#!/usr/bin/env bash
# Builds a static FFmpeg with libdav1d for macOS 13 arm64, laid out like the project's bundled
# FFmpeg package, so CMake can use it through -DFFmpeg_CUSTOM_DIR=<out>.
#
# The prebuilt macOS package has no AV1 decoder, and Switch 2 titles (Ocarina of Time among them)
# play AV1 movies. Apple Silicon before M3 has no AV1 hardware decoder, so this is dav1d in software.
# FFmpeg is the same commit and configuration as the bundled package (crueter-ci/FFmpeg), plus dav1d.
#
# Usage: tools/macos13/build-ffmpeg-av1.sh <out dir>
set -euo pipefail

out="${1:?usage: build-ffmpeg-av1.sh <out dir>}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
root="$(cd "$(dirname "$0")/../.." && pwd)"

ffmpeg_commit=946fcce07b
dav1d_version=1.5.4
export MACOSX_DEPLOYMENT_TARGET=13.0
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
jobs="$(sysctl -n hw.ncpu)"
work="$out/_work"
rm -rf "$work"
mkdir -p "$work"

# dav1d
git clone --depth 1 --branch "$dav1d_version" https://github.com/videolan/dav1d.git "$work/dav1d"
meson setup "$work/dav1d-build" "$work/dav1d" \
    --prefix="$work/dav1d-prefix" --libdir=lib \
    --buildtype=release --default-library=static \
    -Denable_tools=false -Denable_tests=false -Denable_examples=false \
    -Dc_args="-mmacosx-version-min=13.0" -Dc_link_args="-mmacosx-version-min=13.0"
ninja -C "$work/dav1d-build" install

# FFmpeg
curl -sSfL "https://github.com/FFmpeg/FFmpeg/archive/$ffmpeg_commit.tar.gz" | tar -xz -C "$work"
src="$(echo "$work"/FFmpeg-*)"
(
    cd "$src"
    PKG_CONFIG_PATH="$work/dav1d-prefix/lib/pkgconfig" ./configure \
        --prefix="$work/ffmpeg-prefix" \
        --cc=clang --cxx=clang++ \
        --disable-avdevice --disable-avformat --disable-doc --disable-everything \
        --disable-ffmpeg --disable-ffprobe --disable-network --disable-iconv \
        --enable-static --enable-filter=yadif,scale --enable-small --enable-pic --enable-swresample \
        --enable-decoder=h264 --enable-decoder=vp8 --enable-decoder=vp9 --enable-decoder=opus \
        --enable-libdav1d --enable-decoder=libdav1d \
        --enable-videotoolbox --enable-hwaccel=h264_videotoolbox --enable-hwaccel=vp9_videotoolbox \
        --pkg-config-flags=--static \
        --extra-cflags="-mmacosx-version-min=13.0" --extra-ldflags="-mmacosx-version-min=13.0"
    make -j"$jobs"
    make install
)

# Package: same layout as the bundled one, with libdav1d.a beside the FFmpeg libraries.
rm -rf "$out/include" "$out/lib"
cp -R "$work/ffmpeg-prefix/include" "$out/include"
mkdir -p "$out/lib"
for lib in avcodec avfilter avutil swresample swscale; do
    cp "$work/ffmpeg-prefix/lib/lib$lib.a" "$out/lib/"
done
cp "$work/dav1d-prefix/lib/libdav1d.a" "$out/lib/"
cp "$root/tools/macos13/ffmpeg-package.cmake" "$out/CMakeLists.txt"
rm -rf "$work"
echo "FFmpeg with dav1d: $out"
