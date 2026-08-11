#!/usr/bin/env bash
# Generate diverse-flop soft-label JSONL.
#
# Per flop we only emit a sparse slice of the tree (see SOFT_* env vars in
# walk_tree.c), then move on — maximizing unique flops instead of dumping
# ~100k rows on the same board.
#
# Usage:
#   ./batch_training_soft.sh
#   TARGET_FLOPS=3000 OUT=training_soft_diverse.jsonl ./batch_training_soft.sh
#
# Env:
#   TARGET_FLOPS      stop after this many unique flops (default 3000)
#   TARGET_BYTES      also stop if file reaches this size (default 2 GiB)
#   ITERATIONS        CFR iters per flop (default 50)
#   SOFT_MAX_DEPTH    betting depth to emit (default 1: SB open + BB reply)
#   SOFT_MAX_COMBOS   combos sampled per node (default 96)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BINARY="${BINARY:-./turbofire}"
OUT="${OUT:-training_soft_diverse.jsonl}"
SEEN="${SEEN:-${OUT}.seen_flops}"
ITERATIONS="${ITERATIONS:-50}"
TARGET_FLOPS="${TARGET_FLOPS:-10000}"
TARGET_BYTES="${TARGET_BYTES:-$((2 * 1024 * 1024 * 1024))}"
export SOFT_MAX_DEPTH="${SOFT_MAX_DEPTH:-1}"
export SOFT_MAX_COMBOS="${SOFT_MAX_COMBOS:-96}"

RANKS=(2 3 4 5 6 7 8 9 T J Q K A)
SUITS=(s h d c)

file_size() {
	if [[ ! -f "$1" ]]; then
		echo 0
		return
	fi
	if stat -f%z "$1" >/dev/null 2>&1; then
		stat -f%z "$1"
		return
	fi
	stat -c%s "$1"
}

random_card() {
	local rank="${RANKS[$((RANDOM % ${#RANKS[@]}))]}"
	local suit="${SUITS[$((RANDOM % ${#SUITS[@]}))]}"
	printf '%s%s' "$rank" "$suit"
}

random_flop() {
	local c1 c2 c3
	while true; do
		c1="$(random_card)"
		c2="$(random_card)"
		c3="$(random_card)"
		if [[ "$c1" != "$c2" && "$c1" != "$c3" && "$c2" != "$c3" ]]; then
			printf '%s%s%s' "$c1" "$c2" "$c3"
			return
		fi
	done
}

flop_seen() {
	local flop="$1"
	[[ -f "$SEEN" ]] && grep -Fxq "$flop" "$SEEN"
}

if [[ ! -x "$BINARY" ]]; then
	echo "error: binary not found or not executable: $BINARY" >&2
	echo "build with: make" >&2
	exit 1
fi

touch "$OUT"
touch "$SEEN"
size="$(file_size "$OUT")"
flops="$(wc -l < "$SEEN" | tr -d ' ')"
echo "diverse soft labels -> $OUT" >&2
echo "  flops=$flops/$TARGET_FLOPS  size=$size/$TARGET_BYTES" >&2
echo "  ITERATIONS=$ITERATIONS SOFT_MAX_DEPTH=$SOFT_MAX_DEPTH SOFT_MAX_COMBOS=$SOFT_MAX_COMBOS" >&2

while (( flops < TARGET_FLOPS && size < TARGET_BYTES )); do
	flop="$(random_flop)"
	if flop_seen "$flop"; then
		continue
	fi

	echo "solving flop $flop ($((flops + 1))/$TARGET_FLOPS, size=$size)" >&2
	echo "$flop" >> "$SEEN"

	# Soft-label rows are JSON objects; drop solver status chatter.
	"$BINARY" "$flop" "$ITERATIONS" 2>/dev/null \
		| grep '^{"context":' \
		>> "$OUT"

	size="$(file_size "$OUT")"
	flops="$(wc -l < "$SEEN" | tr -d ' ')"
done

echo "done: flops=$flops size=$size -> $OUT" >&2
