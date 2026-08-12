#!/usr/bin/env bash
# Flop-only training data with a tight range (only strong hands play).
#
#   SEED=42 ./batch_training_soft_tight.sh
#   SEED=42 TARGET_FLOPS=12000 ./batch_training_soft_tight.sh
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export RANGE=tight
export OUT="${OUT:-training_soft_tight.jsonl}"
exec "$SCRIPT_DIR/batch_training_soft.sh" "$@"
