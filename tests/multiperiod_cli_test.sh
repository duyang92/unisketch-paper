#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${MULTIPERIOD_BINARY:-${repo_root}/build/unisketch-multiperiod}"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

period_1="${tmp_dir}/period-1.txt"
period_2="${tmp_dir}/period-2.txt"
period_3="${tmp_dir}/period-3.txt"

cat >"${period_1}" <<'EOF'
1 10
2 10
1 20
1 20
EOF

cat >"${period_2}" <<'EOF'
1 10
3 10
2 20
EOF

cat >"${period_3}" <<'EOF'
1 10
2 10
1 20
EOF

expect_failure() {
  local expected="$1"
  shift
  if "$@" >"${tmp_dir}/stdout" 2>"${tmp_dir}/stderr"; then
    printf 'Expected command to fail: %s\n' "$*" >&2
    exit 1
  fi
  grep -Fq "${expected}" "${tmp_dir}/stderr"
}

kpse_output="$("${binary}" kpse --memory-kb 2048 --k 2 \
  --period-input "${period_1}" --period-input "${period_2}" \
  --period-input "${period_3}")"
grep -Fq 'Task: kpse' <<<"${kpse_output}"
grep -Fq 'Periods: 3' <<<"${kpse_output}"
grep -Fq 'K: 2' <<<"${kpse_output}"
grep -Fq 'Distinct flows: 2' <<<"${kpse_output}"
grep -Fq 'Evaluated flows: 2' <<<"${kpse_output}"
grep -Eq '^KPSE MRE: [0-9]+([.][0-9]+)?$' <<<"${kpse_output}"
kpse_mre="$(sed -n 's/^KPSE MRE: //p' <<<"${kpse_output}")"
awk -v value="${kpse_mre}" 'BEGIN { exit !(value >= 0 && value < 0.1) }'

expect_failure 'K must not exceed the number of periods' \
  "${binary}" kpse --k 4 --period-input "${period_1}" \
    --period-input "${period_2}" --period-input "${period_3}"

expect_failure 'Virtual bitmap bits must be at least 2' \
  "${binary}" kpse --k 2 --virtual-bitmap-bits 1 \
    --period-input "${period_1}" --period-input "${period_2}"

earlier="${tmp_dir}/earlier.txt"
later="${tmp_dir}/later.txt"
printf '1 10\n1 20\n1 30\n' >"${earlier}"
printf '1 10\n1 20\n1 30\n' >"${later}"
for element in $(seq 2 200); do
  printf '%s 20\n' "${element}" >>"${earlier}"
  printf '%s 10\n' "${element}" >>"${later}"
done

increase_output="$("${binary}" hscd --memory-kb 2048 --top-k 1 \
  --direction increase --seed 1 --period-input "${earlier}" \
  --period-input "${later}")"
grep -Fq 'Task: hscd' <<<"${increase_output}"
grep -Fq 'Direction: increase' <<<"${increase_output}"
grep -Fq 'Actual heavy spread changers: 1' <<<"${increase_output}"
grep -Fq 'Reported heavy spread changers: 1' <<<"${increase_output}"
grep -Fq 'HSCD true positives: 1' <<<"${increase_output}"
grep -Fq 'HSCD precision: 1.000000' <<<"${increase_output}"
grep -Fq 'HSCD recall: 1.000000' <<<"${increase_output}"
grep -Fq 'HSCD F1-score: 1.000000' <<<"${increase_output}"

decrease_output="$("${binary}" hscd --memory-kb 2048 --top-k 1 \
  --direction decrease --seed 1 --period-input "${earlier}" \
  --period-input "${later}")"
grep -Fq 'Direction: decrease' <<<"${decrease_output}"
grep -Fq 'HSCD true positives: 1' <<<"${decrease_output}"
grep -Fq 'HSCD precision: 1.000000' <<<"${decrease_output}"
grep -Fq 'HSCD recall: 1.000000' <<<"${decrease_output}"
grep -Fq 'HSCD F1-score: 1.000000' <<<"${decrease_output}"

expect_failure 'HSCD requires exactly two period inputs' \
  "${binary}" hscd --top-k 1 --direction increase \
    --period-input "${earlier}"

wrapper_output="$(MULTIPERIOD_BINARY="${binary}" \
  PERIOD_INPUTS="${period_1} ${earlier} ${later}" \
  KPSE_K=2 HSCD_TOP_K=1 HSCD_DIRECTION=increase \
  bash "${repo_root}/scripts/run_multi.sh")"
grep -Fq 'Task: kpse' <<<"${wrapper_output}"
grep -Fq 'Task: hscd' <<<"${wrapper_output}"
grep -Fq 'Direction: increase' <<<"${wrapper_output}"
grep -Fq 'Reported rank 1: flow=10' <<<"${wrapper_output}"

if MULTIPERIOD_BINARY="${binary}" PERIOD_INPUTS='' \
  bash "${repo_root}/scripts/run_multi.sh" \
  >"${tmp_dir}/stdout" 2>"${tmp_dir}/stderr"; then
  printf 'run_multi.sh must reject missing period inputs.\n' >&2
  exit 1
fi
grep -Fq 'PERIOD_INPUTS must contain at least two files' "${tmp_dir}/stderr"

for invalid_inputs in '   ' "${period_1}" \
  "${period_1} ${tmp_dir}/missing-period.txt"; do
  if PERIOD_INPUTS="${invalid_inputs}" \
    bash "${repo_root}/scripts/run_all.sh" \
    >"${tmp_dir}/stdout" 2>"${tmp_dir}/stderr"; then
    printf 'run_all.sh must reject invalid period inputs.\n' >&2
    exit 1
  fi
  grep -Eq 'at least two files|not a readable file' "${tmp_dir}/stderr"
  if grep -Fq 'Algorithm:' "${tmp_dir}/stdout"; then
    printf 'run_all.sh must validate periods before running single-period tasks.\n' >&2
    exit 1
  fi
done

printf 'Multi-period CLI tests passed.\n'
