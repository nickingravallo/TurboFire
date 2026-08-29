#!/usr/bin/env bash
# Flop-only training data with a wide range (most hands play).
#
#   SEED=42 ./batch_training_soft_wide.sh
#   SEED=42 TARGET_FLOPS=12000 ./batch_training_soft_wide.sh
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export RANGE=wide
export OUT="${OUT:-training_soft_wide.jsonl}"
exec "$SCRIPT_DIR/batch_training_soft.sh" "$@"
