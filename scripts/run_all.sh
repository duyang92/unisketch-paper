#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"${repo_root}/build/unisketch" \
  --algorithm all \
  --memory-kb 2048 \
  --input "${repo_root}/data/00.txt" \
  --seed 1
