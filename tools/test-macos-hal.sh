#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
sanitizers="${1:-address,undefined}"
case "$sanitizers" in address,undefined|thread) ;; *) echo 'Use address,undefined or thread' >&2; exit 2;; esac
mkdir -p "$repo_dir/build/hal-tests"
clang++ -std=c++17 -O1 -g -fno-omit-frame-pointer "-fsanitize=$sanitizers" -bundle \
 "$repo_dir/native/platform/macos/hal/VoxMaskSilent.cpp" \
 -framework CoreAudio -framework CoreFoundation \
 -o "$repo_dir/build/hal-tests/hal-$sanitizers.bundle"
clang++ -std=c++17 -O1 -g -fno-omit-frame-pointer "-fsanitize=$sanitizers" \
 "$repo_dir/native/platform/macos/tests/hal_test.cpp" \
 -framework CoreAudio -framework CoreFoundation \
 -o "$repo_dir/build/hal-tests/test-$sanitizers"
"$repo_dir/build/hal-tests/test-$sanitizers" "$repo_dir/build/hal-tests/hal-$sanitizers.bundle"
