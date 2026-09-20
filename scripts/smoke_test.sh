#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT
smoke_input="${tmp_dir}/smoke.txt"

head -n 1000 "${repo_root}/data/00.txt" >"${smoke_input}"
test "$(wc -l <"${smoke_input}")" -eq 1000

"${repo_root}/build/unisketch" \
  --algorithm all \
  --memory-kb 2048 \
  --input "${smoke_input}" \
  --seed 1
