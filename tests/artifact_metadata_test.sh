#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for path in README.md LICENSE data/README.md hardware/README.md Makefile; do
  test -s "${repo_root}/${path}"
done

test ! -e "${repo_root}/docs/superpowers"
test ! -e "${repo_root}/build"

grep -Fq 'Artifacts Available' "${repo_root}/README.md"
grep -Fq 'Artifacts Functional' "${repo_root}/README.md"
grep -Fq 'make run-minimal' "${repo_root}/README.md"
grep -Fq 'make run-all' "${repo_root}/README.md"
grep -Fq 'Intel Tofino' "${repo_root}/hardware/README.md"
grep -Fq '9.2.0' "${repo_root}/hardware/README.md"
grep -Fq '#include<tna.p4>' "${repo_root}/hardware/UniSketch.p4"
grep -Fq 'Switch(pipe) main;' "${repo_root}/hardware/UniSketch.p4"
grep -Fq '907463' "${repo_root}/data/README.md"
grep -Fq 'fe98bfa312a4db6a5c919c94e9428ec6e6f097f75d772f735ec079c3585f904b' \
  "${repo_root}/data/README.md"
grep -Fq 'It does not apply to data/00.txt.' "${repo_root}/LICENSE"

local_root='/home'"/suda"
local_host='suda'"alveo"
if grep -REIn --exclude='00.txt' --exclude-dir=.git --exclude-dir=.superpowers \
  --exclude-dir=superpowers "${local_root}|${local_host}" "${repo_root}"; then
  printf 'Local path or hostname leaked into the artifact.\n' >&2
  exit 1
fi

if ! find "${repo_root}" \
  -path "${repo_root}/.git" -prune -o \
  -path "${repo_root}/.superpowers" -prune -o \
  -path "${repo_root}/build" -prune -o \
  -path "${repo_root}/data/00.txt" -prune -o \
  -type f -print0 | xargs -0 perl -CS -Mutf8 -ne \
  'if (/\p{Han}/) { print "$ARGV:$.:$_"; $bad=1 } END { exit($bad ? 1 : 0) }'; then
  printf 'CJK text found in the artifact.\n' >&2
  exit 1
fi

printf 'Artifact metadata tests passed.\n'
