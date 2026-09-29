#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

make -C "${repo_root}" clean
make -C "${repo_root}" make-single
test -x "${repo_root}/build/unisketch"
test ! -e "${repo_root}/build/unisketch-multiperiod"

make -C "${repo_root}" make-multi
test -x "${repo_root}/build/unisketch-multiperiod"

outside_dir="$(mktemp -d)"
trap 'rm -rf "${outside_dir}"' EXIT
(
  cd "${outside_dir}"
  "${repo_root}/scripts/run_minimal.sh"
)

make -C "${repo_root}" test-all
printf 'Workflow tests passed.\n'
