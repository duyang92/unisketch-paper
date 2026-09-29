#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

dry_run() {
  make -C "${repo_root}" -Bn "$@"
}

single_build="$(dry_run make-single)"
grep -Fq 'simulation/main.cpp' <<<"${single_build}"
if grep -Fq 'simulation/multiperiod.cpp' <<<"${single_build}"; then
  printf 'make-single must not build the multi-period driver.\n' >&2
  exit 1
fi

multi_build="$(dry_run make-multi)"
grep -Fq 'simulation/multiperiod.cpp' <<<"${multi_build}"
if grep -Fq 'simulation/main.cpp' <<<"${multi_build}"; then
  printf 'make-multi must not build the single-period driver.\n' >&2
  exit 1
fi

all_build="$(dry_run all)"
grep -Fq 'simulation/main.cpp' <<<"${all_build}"
grep -Fq 'simulation/multiperiod.cpp' <<<"${all_build}"

single_test="$(dry_run test-single)"
grep -Fq 'cli_test.sh' <<<"${single_test}"
grep -Fq 'sanitizer_test.sh single' <<<"${single_test}"
if grep -Fq 'multiperiod_cli_test.sh' <<<"${single_test}"; then
  printf 'test-single must not run multi-period tests.\n' >&2
  exit 1
fi

multi_test="$(dry_run test-multi)"
grep -Fq 'multiperiod_cli_test.sh' <<<"${multi_test}"
grep -Fq 'sanitizer_test.sh multi' <<<"${multi_test}"
if grep -Fq './tests/cli_test.sh' <<<"${multi_test}"; then
  printf 'test-multi must not run single-period CLI tests.\n' >&2
  exit 1
fi

single_run="$(dry_run run-single)"
grep -Fq 'run_single.sh' <<<"${single_run}"
if grep -Fq 'run_multi.sh' <<<"${single_run}"; then
  printf 'run-single must not run multi-period tasks.\n' >&2
  exit 1
fi

multi_run="$(dry_run run-multi)"
grep -Fq 'run_multi.sh' <<<"${multi_run}"
if grep -Fq 'run_single.sh' <<<"${multi_run}"; then
  printf 'run-multi must not run single-period tasks.\n' >&2
  exit 1
fi

all_run="$(dry_run run-all)"
grep -Fq 'run_all.sh' <<<"${all_run}"
grep -Fq 'simulation/main.cpp' <<<"${all_run}"
grep -Fq 'simulation/multiperiod.cpp' <<<"${all_run}"

printf 'Make target separation tests passed.\n'
