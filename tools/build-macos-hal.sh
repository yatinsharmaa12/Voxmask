#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS SDK required' >&2; exit 2; }
bundle="$repo_dir/build/macos/VoxMaskSilent.driver"
mkdir -p "$bundle/Contents/MacOS"
clang++ -std=c++17 -O2 -Wall -Wextra -fvisibility=hidden -bundle \
  "$repo_dir/native/platform/macos/hal/VoxMaskSilent.cpp" \
  -framework CoreAudio -framework CoreFoundation \
  -o "$bundle/Contents/MacOS/VoxMaskSilent"
cp "$repo_dir/native/platform/macos/hal/Info.plist" "$bundle/Contents/Info.plist"
plutil -lint "$bundle/Contents/Info.plist"
codesign --force --sign - "$bundle"
codesign --verify --strict "$bundle"
echo "Built development-only bundle: $bundle"
echo 'Not installed. This bundle produces silence and has no audio producer connection.'
