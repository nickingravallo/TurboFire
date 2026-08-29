#!/usr/bin/env bash
# NashGPT 1.6 flop-only soft-label dump.
#
# Abstraction:
#   ranges       condensed (BTN ~25%, BB ~28%)
#   unopened     CHECK, BET40, BET100, ALLIN
#   facing bet   FOLD, CALL, RAISE_3X, ALLIN
#   facing raise FOLD, CALL
#   max line     four actions, e.g. CHECK -> BET -> RAISE -> CALL
#   boards       all 1,755 suit-canonical flops
#   combos       every retained live combo
#
# Expected full size: ~8.75M rows / ~1.4 GB JSONL. Exact bytes vary.
#
# Production:
#   make
#   SEED=42 ./batch_training_soft_nashgpt16.sh
#
# Smoke:
#   SEED=42 ITERATIONS=50 TARGET_FLOPS=5 \
#     OUT=training_soft_nashgpt16_smoke.jsonl \
#     ./batch_training_soft_nashgpt16.sh
#
# Resume uses the same OUT, OUT.seen_flops, and OUT.seed files.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export RANGE="${RANGE:-condensed}"
export DATASET_LABEL="${DATASET_LABEL:-NashGPT 1.6}"
export ITERATIONS="${ITERATIONS:-1000}"
export TARGET_FLOPS="${TARGET_FLOPS:-1755}"
export OUT="${OUT:-training_soft_nashgpt16_condensed.jsonl}"
export SOFT_MAX_DEPTH="${SOFT_MAX_DEPTH:-3}"
export SOFT_MAX_COMBOS="${SOFT_MAX_COMBOS:-0}"
export BET_SIZES="${BET_SIZES:-40,100}"
export RAISE_SIZES="${RAISE_SIZES:-3}"
export MAX_RAISES="${MAX_RAISES:-1}"
export ENABLE_ALLIN="${ENABLE_ALLIN:-1}"

exec "$SCRIPT_DIR/batch_training_soft.sh"
