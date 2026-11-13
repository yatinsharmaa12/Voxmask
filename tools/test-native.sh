#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
sanitizers="${1:-address,undefined}"
case "$sanitizers" in address,undefined|thread) ;; *) echo "Use address,undefined or thread" >&2; exit 2;; esac
mkdir -p "$repo_dir/build/native-tests"
"${CXX:-clang++}" -std=c++17 -O1 -g -fno-omit-frame-pointer -pthread \
  "-fsanitize=$sanitizers" "$repo_dir/native/tests/processor_test.cpp" \
  -o "$repo_dir/build/native-tests/processor-$sanitizers"
"$repo_dir/build/native-tests/processor-$sanitizers"

"${CXX:-clang++}" -std=c++17 -O1 -g -fno-omit-frame-pointer -pthread \
  "-fsanitize=$sanitizers" "$repo_dir/native/tests/frame_ring_test.cpp" \
  "$repo_dir/native/audio/buffer_check.cpp" \
  -o "$repo_dir/build/native-tests/frame-ring-$sanitizers"
"$repo_dir/build/native-tests/frame-ring-$sanitizers"

"${CXX:-clang++}" -std=c++17 -O1 -g -fno-omit-frame-pointer -pthread \
  "-fsanitize=$sanitizers" "$repo_dir/native/tests/worker_pipeline_test.cpp" \
  -o "$repo_dir/build/native-tests/worker-pipeline-$sanitizers"
"$repo_dir/build/native-tests/worker-pipeline-$sanitizers"

"${CXX:-clang++}" -std=c++17 -O1 -g -fno-omit-frame-pointer -pthread \
  "-fsanitize=$sanitizers" "$repo_dir/native/tests/producer_protocol_test.cpp" \
  -o "$repo_dir/build/native-tests/producer-protocol-$sanitizers"
"$repo_dir/build/native-tests/producer-protocol-$sanitizers"
