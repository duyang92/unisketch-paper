#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${UNISKETCH_BINARY:-${repo_root}/build/unisketch}"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

valid_input="${tmp_dir}/valid.txt"
printf '1 10\n2 10\n3 20\n' >"${valid_input}"

duplicate_input="${tmp_dir}/duplicate.txt"
printf '1 10\n1 10\n2 10\n3 20\n4 30\n5 30\n6 30\n' >"${duplicate_input}"

expect_failure() {
  local expected="$1"
  shift
  if "$@" >"${tmp_dir}/stdout" 2>"${tmp_dir}/stderr"; then
    printf 'Expected command to fail: %s\n' "$*" >&2
    exit 1
  fi
  grep -Fq "${expected}" "${tmp_dir}/stderr"
}

"${binary}" --help | grep -Fq 'Usage:'

for algorithm in unisketch vbitmap-ss vbitmap-ss-rskt m2d all; do
  output="$("${binary}" --algorithm "${algorithm}" --memory-kb 2048 \
    --input "${valid_input}" --seed 1)"
  grep -Fq 'Input records: 3' <<<"${output}"
  grep -Fq 'Distinct flows: 2' <<<"${output}"
  grep -Fq 'SSD threshold: 100' <<<"${output}"
  grep -Fq 'Actual super-spreaders: 0' <<<"${output}"
  grep -Fq 'PFSE MRE:' <<<"${output}"
  grep -Fq 'SSD precision:' <<<"${output}"
  grep -Fq 'SSD recall:' <<<"${output}"
  grep -Fq 'SSD F1-score:' <<<"${output}"
  grep -Fq 'Estimate checksum:' <<<"${output}"
done

duplicate_output="$("${binary}" --algorithm unisketch --memory-kb 2048 \
  --input "${duplicate_input}" --seed 1 --ssd-threshold 3)"
grep -Fq 'Input records: 7' <<<"${duplicate_output}"
grep -Fq 'Distinct flows: 3' <<<"${duplicate_output}"
grep -Fq 'SSD threshold: 3' <<<"${duplicate_output}"
grep -Fq 'Actual super-spreaders: 1' <<<"${duplicate_output}"

for metric in 'PFSE MRE' 'SSD precision' 'SSD recall' 'SSD F1-score'; do
  value="$(sed -n "s/^${metric}: //p" <<<"${duplicate_output}")"
  awk -v value="${value}" 'BEGIN { exit !(value ~ /^[0-9]+([.][0-9]+)?$/ && value >= 0) }'
done

mkdir -p "${tmp_dir}/legacy/data"
cp "${valid_input}" "${tmp_dir}/legacy/data/00.txt"
legacy_output="$(cd "${tmp_dir}/legacy" && "${binary}" 2048)"
grep -Fq 'Algorithm: unisketch' <<<"${legacy_output}"

expect_failure 'Unknown algorithm' "${binary}" --algorithm invalid --input "${valid_input}"
expect_failure 'Missing value for --input' "${binary}" --input
expect_failure 'Missing value for --input' "${binary}" --input --seed 1
expect_failure 'Memory must be a positive integer' "${binary}" --memory-kb 0 --input "${valid_input}"
expect_failure 'Memory value is out of range' "${binary}" --memory-kb 2147483648 --input "${valid_input}"
expect_failure 'Seed value is out of range' "${binary}" --seed 4294967296 --input "${valid_input}"
expect_failure 'SSD threshold must be a positive integer' "${binary}" --ssd-threshold 0 --input "${valid_input}"
expect_failure 'SSD threshold value is out of range' "${binary}" --ssd-threshold 4294967296 --input "${valid_input}"
expect_failure 'Cannot open input file' "${binary}" --input "${tmp_dir}/missing.txt"

: >"${tmp_dir}/empty.txt"
expect_failure 'Input file is empty' "${binary}" --input "${tmp_dir}/empty.txt"

for bad_row in '-1 2' '1 4294967296' '1 nope' '1 2 3'; do
  printf '%s\n' "${bad_row}" >"${tmp_dir}/malformed.txt"
  expect_failure 'Invalid input at line 1' "${binary}" --input "${tmp_dir}/malformed.txt"
done

printf 'CLI tests passed.\n'
