#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"${repo_root}/build/unisketch" \
  --algorithm all \
  --memory-kb "${MEMORY_KB:-2048}" \
  --input "${SINGLE_INPUT:-${repo_root}/data/00.txt}" \
  --seed "${SEED:-1}" \
  --ssd-threshold "${SSD_THRESHOLD:-100}"
