#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${MULTIPERIOD_BINARY:-${repo_root}/build/unisketch-multiperiod}"

source "${repo_root}/scripts/validate_period_inputs.sh"
validate_period_inputs

period_arguments=()
for input_path in "${period_inputs[@]}"; do
  period_arguments+=(--period-input "${input_path}")
done

"${binary}" kpse \
  --memory-kb "${MEMORY_KB:-2048}" \
  --k "${KPSE_K:-2}" \
  --virtual-bitmap-bits "${VIRTUAL_BITMAP_BITS:-5000}" \
  "${period_arguments[@]}"

last_index=$((${#period_inputs[@]} - 1))
earlier_index=$((last_index - 1))
"${binary}" hscd \
  --memory-kb "${MEMORY_KB:-2048}" \
  --top-k "${HSCD_TOP_K:-10}" \
  --direction "${HSCD_DIRECTION:-increase}" \
  --seed "${SEED:-1}" \
  --period-input "${period_inputs[earlier_index]}" \
  --period-input "${period_inputs[last_index]}"
