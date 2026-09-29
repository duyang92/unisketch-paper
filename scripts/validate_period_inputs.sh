#!/usr/bin/env bash
set -euo pipefail

period_inputs=()

validate_period_inputs() {
  read -r -a period_inputs <<<"${PERIOD_INPUTS:-}"
  if (( ${#period_inputs[@]} < 2 )); then
    printf '%s\n' \
      'PERIOD_INPUTS must contain at least two files in chronological order.' \
      'Example: PERIOD_INPUTS="/path/to/period-1.txt /path/to/period-2.txt"' >&2
    return 2
  fi

  local input_path
  for input_path in "${period_inputs[@]}"; do
    if [[ ! -f "${input_path}" || ! -r "${input_path}" ]]; then
      printf 'PERIOD_INPUTS entry is not a readable file: %s\n' \
        "${input_path}" >&2
      return 2
    fi
  done
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  validate_period_inputs
fi
