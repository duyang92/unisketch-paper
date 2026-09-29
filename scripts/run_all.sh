#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

source "${repo_root}/scripts/validate_period_inputs.sh"
validate_period_inputs

bash "${repo_root}/scripts/run_single.sh"
bash "${repo_root}/scripts/run_multi.sh"
