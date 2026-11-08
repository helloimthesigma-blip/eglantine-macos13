#!/usr/bin/env bash
# Packages the macOS 13 build into a self-contained Eglantine.app (eden-cli included) and a zip.
#
# The executables link Qt, FFmpeg, SDL3 and libc++ statically; MoltenVK is the one dylib, already
# in Contents/Frameworks. The bundle is ad-hoc signed (not notarized): on another Mac, remove the
# quarantine flag after downloading (xattr -dr com.apple.quarantine Eglantine.app).
#
# Usage: tools/macos13/package.sh [build dir] [out dir]
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/build/macos13}"
out="${2:-$build/package}"

app_in="$build/bin/eglantine.app"
test -x "$app_in/Contents/MacOS/eglantine"
test -x "$build/bin/eden-cli"
test -f "$app_in/Contents/Frameworks/libMoltenVK.dylib"

rm -rf "$out"
mkdir -p "$out"
app="$out/Eglantine.app"
cp -R "$app_in" "$app"
cp "$build/bin/eden-cli" "$app/Contents/MacOS/eden-cli"

# Nothing outside the system and the bundle may be linked.
for binary in "$app/Contents/MacOS/eglantine" "$app/Contents/MacOS/eden-cli"; do
    if otool -L "$binary" | tail -n +2 | grep -v -E "^\s+(/System/Library/|/usr/lib/)"; then
        echo "error: $binary links libraries outside the system" >&2
        exit 1
    fi
done

codesign --force --deep --sign - "$app"
codesign --verify --deep "$app"
(cd "$out" && ditto -c -k --keepParent Eglantine.app Eglantine-macos13-arm64.zip)
echo "packaged: $app"
echo "zip: $out/Eglantine-macos13-arm64.zip"
