#!/usr/bin/env bash
# Generate flop-only soft-label JSONL.
#
# Prefer the wrappers:
#   ./batch_training_soft_wide.sh   most hands play (single-raised pot)
#   ./batch_training_soft_tight.sh  only strong hands play (3-bet pot)
#
# SEED chooses which flops get solved. Same seed → same boards.
# If omitted, a seed is picked and saved next to the output file.
#
#   SEED=42 ./batch_training_soft_wide.sh
#   SEED=42 RANGE=tight TARGET_FLOPS=12000 ./batch_training_soft.sh
#
#   SEED           which flops to solve (saved to ${OUT}.seed)
#   RANGE          wide | tight
#   TARGET_FLOPS   how many different boards to solve
#   ITERATIONS     how thoroughly each board is solved
#   OUT            output JSONL path

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BINARY="${BINARY:-./turbofire}"
RANGE="${RANGE:-wide}"
ITERATIONS="${ITERATIONS:-50}"
TARGET_FLOPS="${TARGET_FLOPS:-10000}"
TARGET_BYTES="${TARGET_BYTES:-$((2 * 1024 * 1024 * 1024))}"
export SOFT_MAX_DEPTH="${SOFT_MAX_DEPTH:-1}"
export SOFT_MAX_COMBOS="${SOFT_MAX_COMBOS:-96}"

case "$RANGE" in
	wide)
		RANGE_FLAG="--range=wide"
		RANGE_SLUG="wide"
		;;
	tight|condensed)
		RANGE_FLAG="--range=condensed"
		RANGE_SLUG="tight"
		;;
	*)
		echo "error: RANGE must be wide|tight|condensed (got '$RANGE')" >&2
		exit 1
		;;
esac

OUT="${OUT:-training_soft_${RANGE_SLUG}.jsonl}"
SEEN="${SEEN:-${OUT}.seen_flops}"
SEED_FILE="${SEED_FILE:-${OUT}.seed}"

RANKS=(2 3 4 5 6 7 8 9 T J Q K A)
SUITS=(s h d c)

# 31-bit LCG (glibc). Use bits 16–30 like rand(); low bits are correlated
# and would collapse suits. Keep state in-process — never via $(...).
LCG_STATE=0
LCG_RAND=0

lcg_next() {
	LCG_STATE=$(( (1103515245 * LCG_STATE + 12345) & 0x7FFFFFFF ))
	LCG_RAND=$(( (LCG_STATE >> 16) & 0x7FFF ))
}

pick_seed() {
	if [[ -n "${SEED:-}" ]]; then
		return
	fi
	if [[ -f "$SEED_FILE" ]]; then
		SEED="$(tr -d '[:space:]' < "$SEED_FILE")"
		if [[ -n "$SEED" ]]; then
			echo "resuming seed $SEED from $SEED_FILE" >&2
			return
		fi
	fi
	SEED="$(od -An -N4 -tu4 /dev/urandom | tr -d ' ')"
}

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

card_from_idx() {
	local idx="$1"
	printf '%s%s' "${RANKS[$((idx % 13))]}" "${SUITS[$((idx / 13))]}"
}

next_flop() {
	local i1 i2 i3
	while true; do
		lcg_next
		i1=$(( LCG_RAND % 52 ))
		lcg_next
		i2=$(( LCG_RAND % 52 ))
		lcg_next
		i3=$(( LCG_RAND % 52 ))
		if [[ "$i1" -ne "$i2" && "$i1" -ne "$i3" && "$i2" -ne "$i3" ]]; then
			FLOP="$(card_from_idx "$i1")$(card_from_idx "$i2")$(card_from_idx "$i3")"
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

pick_seed
if ! [[ "$SEED" =~ ^[0-9]+$ ]]; then
	echo "error: SEED must be a non-negative integer (got '$SEED')" >&2
	exit 1
fi
LCG_STATE=$(( SEED & 0x7FFFFFFF ))
printf '%s\n' "$SEED" > "$SEED_FILE"

touch "$OUT"
touch "$SEEN"
size="$(file_size "$OUT")"
flops="$(wc -l < "$SEEN" | tr -d ' ')"
echo "diverse soft labels -> $OUT" >&2
echo "  range=$RANGE_SLUG ($RANGE_FLAG)  seed=$SEED" >&2
echo "  flops=$flops/$TARGET_FLOPS  size=$size/$TARGET_BYTES" >&2
echo "  ITERATIONS=$ITERATIONS SOFT_MAX_DEPTH=$SOFT_MAX_DEPTH SOFT_MAX_COMBOS=$SOFT_MAX_COMBOS" >&2

while (( flops < TARGET_FLOPS && size < TARGET_BYTES )); do
	next_flop
	if flop_seen "$FLOP"; then
		continue
	fi

	echo "solving flop $FLOP ($((flops + 1))/$TARGET_FLOPS, size=$size)" >&2
	echo "$FLOP" >> "$SEEN"

	# Soft-label rows are JSON objects; drop solver status chatter.
	"$BINARY" "$FLOP" "$ITERATIONS" "$RANGE_FLAG" 2>/dev/null \
		| grep '^{"context":' \
		>> "$OUT"

	size="$(file_size "$OUT")"
	flops="$(wc -l < "$SEEN" | tr -d ' ')"
done

echo "done: range=$RANGE_SLUG seed=$SEED flops=$flops size=$size -> $OUT" >&2
