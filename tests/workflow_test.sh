#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

make -C "${repo_root}" clean
make -C "${repo_root}" all
test -x "${repo_root}/build/unisketch"

outside_dir="$(mktemp -d)"
trap 'rm -rf "${outside_dir}"' EXIT
(
  cd "${outside_dir}"
  "${repo_root}/scripts/smoke_test.sh"
)

make -C "${repo_root}" test
printf 'Workflow tests passed.\n'
