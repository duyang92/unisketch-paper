#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

binary="${tmp_dir}/unisketch-sanitized"
heap_binary="${tmp_dir}/m2d-heap-sanitized"
input="${tmp_dir}/input.txt"
head -n 1000 "${repo_root}/data/00.txt" >"${input}"

g++ "${repo_root}/tests/m2d_heap_test.cpp" \
  "${repo_root}/utils/MurmurHash3.cpp" -o "${heap_binary}" \
  -std=c++17 -I "${repo_root}" -mavx2 -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer

g++ "${repo_root}/simulation/main.cpp" "${repo_root}/utils/MurmurHash3.cpp" \
  -o "${binary}" -std=c++17 -I "${repo_root}" -mavx2 -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer

ASAN_OPTIONS=detect_leaks=0 "${heap_binary}" >/dev/null

ASAN_OPTIONS=detect_leaks=0 \
  "${binary}" --algorithm all --memory-kb 2048 --input "${input}" \
    --seed 1 >/dev/null

printf 'Sanitizer test passed.\n'
