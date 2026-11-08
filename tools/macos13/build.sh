#!/usr/bin/env bash
# Builds Eglantine for macOS 13 (Ventura) on Apple Silicon.
#
# Toolchain: Homebrew LLVM (for a C++ standard library new enough for this tree) with its libc++
# linked statically, so the app does not depend on the system libc++ of a newer macOS. The Xcode
# SDK supplies the system headers. Qt, FFmpeg, SDL3, OpenSSL and MoltenVK are the project's bundled
# packages (the Qt one is built for macOS 13.0).
#
# Usage: tools/macos13/build.sh [build dir] [extra cmake args...]
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/build/macos13}"
shift || true

llvm="${LLVM_PREFIX:-$(brew --prefix llvm)}"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
sdk="$(xcrun --sdk macosx --show-sdk-path)"
jobs="${JOBS:-$(( $(sysctl -n hw.ncpu) - 1 ))}"

libcxx_link="-nostdlib++ $llvm/lib/c++/libc++.a $llvm/lib/c++/libc++abi.a"
# The system frameworks load macOS's own libc++.1.dylib. If the executables export their weak C++
# definitions, dyld coalesces them with that library's, and objects made by the static libc++ get
# destroyed by the system one (heap corruption at the first iostream). Export only main.
exe_link="$libcxx_link -Wl,-exported_symbols_list,$root/tools/macos13/exported_symbols.txt"

# glslangValidator compiles the host shaders at build time. Homebrew has no macOS 13 bottle, so
# build the standalone tool once into the build tree when none is on PATH.
glslang_version=16.6.0
glslang_dir="$build/_tools/glslang"
if ! command -v glslangValidator >/dev/null 2>&1; then
    if [ ! -x "$glslang_dir/bin/glslangValidator" ]; then
        rm -rf "$glslang_dir/src" "$glslang_dir/build"
        git clone --depth 1 --branch "$glslang_version" \
            https://github.com/KhronosGroup/glslang.git "$glslang_dir/src"
        cmake -S "$glslang_dir/src" -B "$glslang_dir/build" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$glslang_dir" \
            -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DENABLE_GLSLANG_JS=OFF \
            -DBUILD_SHARED_LIBS=OFF -DGLSLANG_ENABLE_INSTALL=ON
        cmake --build "$glslang_dir/build" --target install --parallel "$jobs"
        rm -rf "$glslang_dir/src" "$glslang_dir/build"
    fi
    export PATH="$glslang_dir/bin:$PATH"
fi

# FFmpeg with dav1d for AV1 movies (the bundled macOS FFmpeg has no AV1 decoder).
ffmpeg_dir="$build/_tools/ffmpeg-av1"
if [ ! -f "$ffmpeg_dir/lib/libdav1d.a" ]; then
    "$root/tools/macos13/build-ffmpeg-av1.sh" "$ffmpeg_dir"
fi

configure_args=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_COMPILER="$llvm/bin/clang"
    -DCMAKE_CXX_COMPILER="$llvm/bin/clang++"
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
    -DCMAKE_OSX_SYSROOT="$sdk"
    -DCMAKE_OSX_ARCHITECTURES=arm64
    -DCMAKE_EXE_LINKER_FLAGS="$exe_link"
    -DCMAKE_SHARED_LINKER_FLAGS="$libcxx_link"
    -DCMAKE_MODULE_LINKER_FLAGS="$libcxx_link"
    -DBUILD_TESTING=OFF
    -DYUZU_TESTS=OFF
    -DYUZU_CMD=ON
    -DYUZU_ROOM=OFF
    -DYUZU_ROOM_STANDALONE=OFF
    -DYUZU_USE_BUNDLED_QT=ON
    -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
    -DVulkanHeaders_FORCE_BUNDLED=ON
    -DVulkanUtilityLibraries_FORCE_BUNDLED=ON
    -DENABLE_UPDATE_CHECKER=OFF
    -DUSE_DISCORD_PRESENCE=OFF
    -DENABLE_WEB_SERVICE=OFF
    -DFFmpeg_CUSTOM_DIR="$ffmpeg_dir"
    "$@"
)

# Configure only when the arguments changed: a configure re-extracts some bundled sources and
# rebuilds them. CMake itself reconfigures when a CMakeLists.txt changes.
stamp="$build/.macos13-configure-args"
if [ ! -f "$build/build.ninja" ] || [ "$(cat "$stamp" 2>/dev/null)" != "${configure_args[*]}" ]; then
    cmake -S "$root" -B "$build" -G Ninja "${configure_args[@]}"
    printf '%s' "${configure_args[*]}" > "$stamp"
fi

cmake --build "$build" --target yuzu yuzu-cmd --parallel "$jobs"
