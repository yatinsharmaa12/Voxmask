#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$repo_dir/build/hal-tests"
clang++ -std=c++17 -O2 -Wall -Wextra \
 "$repo_dir/native/platform/macos/tests/hal_system_check.cpp" \
 -framework CoreAudio -framework CoreFoundation -o "$repo_dir/build/hal-tests/hal-system-check"
exec "$repo_dir/build/hal-tests/hal-system-check" "$@"
