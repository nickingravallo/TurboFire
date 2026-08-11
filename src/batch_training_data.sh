#!/usr/bin/env bash
# Deprecated: walk_tree now emits soft-label JSONL.
# Use ./batch_training_soft.sh instead.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "batch_training_data.sh is deprecated; running batch_training_soft.sh" >&2
exec "$SCRIPT_DIR/batch_training_soft.sh" "$@"
