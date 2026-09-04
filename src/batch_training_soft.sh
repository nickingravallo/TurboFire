#!/usr/bin/env bash
# NashGPT 1.5 soft-label dump.
#
# Solves every suit-canonical flop (1,755 textures) at SOFT_MAX_DEPTH=1
# with every in-range hole combo (no 96-combo subsample). Default range
# is wide (SRP-ish). Inference must rewrite hole+flop into the same
# canonical frame via llm/flop_iso.py (sample.py does this automatically).
#
# Resume: keep ${OUT}, ${OUT}.seen_flops, and ${OUT}.seed. Already-listed
# flops are skipped. A flop is recorded in .seen_flops only after its rows
# are appended, so a killed solve is retried next run.
#
#   make
#   SEED=42 ./batch_training_soft.sh
#   SEED=42 ITERATIONS=50 ./batch_training_soft.sh   # smoke
#
#   SEED           shuffle solve order (saved to ${OUT}.seed)
#   RANGE          wide | tight   (default wide)
#   ITERATIONS     DCFR iterations per flop (default 1000)
#   TARGET_FLOPS   cap how many canonical boards to solve (default 1755)
#   OUT            output JSONL (default training_soft_nashgpt15_wide.jsonl)
#   SOFT_MAX_DEPTH betting depth (default 1)
#   SOFT_MAX_COMBOS 0 = every in-range combo (default)
#   BET_SIZES      optional comma-separated pot percentages passed to --bets
#   RAISE_SIZES    optional comma-separated raise multipliers passed to --raises
#   MAX_RAISES     optional raise cap passed to --max-raises
#   ENABLE_ALLIN   1 = add explicit all-in actions (default 0)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BINARY="${BINARY:-./turbofire}"
DATASET_LABEL="${DATASET_LABEL:-NashGPT 1.5}"
RANGE="${RANGE:-wide}"
ITERATIONS="${ITERATIONS:-1000}"
TARGET_FLOPS="${TARGET_FLOPS:-1755}"
# 0 = no size cap. Full wide dump is ~2.1 GB / ~11.5M rows.
TARGET_BYTES="${TARGET_BYTES:-0}"
export SOFT_MAX_DEPTH="${SOFT_MAX_DEPTH:-1}"
export SOFT_MAX_COMBOS="${SOFT_MAX_COMBOS:-0}"
BET_SIZES="${BET_SIZES:-}"
RAISE_SIZES="${RAISE_SIZES:-}"
MAX_RAISES="${MAX_RAISES:-}"
ENABLE_ALLIN="${ENABLE_ALLIN:-0}"

SOLVER_ARGS=()
[[ -n "$BET_SIZES" ]] && SOLVER_ARGS+=("--bets=$BET_SIZES")
[[ -n "$RAISE_SIZES" ]] && SOLVER_ARGS+=("--raises=$RAISE_SIZES")
[[ -n "$MAX_RAISES" ]] && SOLVER_ARGS+=("--max-raises=$MAX_RAISES")
[[ "$ENABLE_ALLIN" == "1" ]] && SOLVER_ARGS+=("--allin")

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

OUT="${OUT:-training_soft_nashgpt15_${RANGE_SLUG}.jsonl}"
SEEN="${SEEN:-${OUT}.seen_flops}"
SEED_FILE="${SEED_FILE:-${OUT}.seed}"
FLOP_TMP="${OUT}.tmp"
PENDING="${PENDING:-${OUT}.pending}"

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

flop_seen() {
	local flop="$1"
	[[ -f "$SEEN" ]] && grep -Fxq "$flop" "$SEEN"
}

recover_pending_append() {
	local pending_flop pending_size pending_seen_size

	[[ -f "$PENDING" ]] || return
	read -r pending_flop pending_size pending_seen_size < "$PENDING" || {
		echo "error: invalid pending marker $PENDING" >&2
		exit 1
	}
	if flop_seen "$pending_flop"; then
		rm -f "$PENDING"
		return
	fi
	if ! [[ "$pending_size" =~ ^[0-9]+$ && "$pending_seen_size" =~ ^[0-9]+$ ]]; then
		echo "error: invalid recovery offset in $PENDING" >&2
		exit 1
	fi
	echo "recovering interrupted append for $pending_flop at byte $pending_size" >&2
	python3 - "$OUT" "$pending_size" "$SEEN" "$pending_seen_size" <<'PY'
import os
import sys

os.truncate(sys.argv[1], int(sys.argv[2]))
os.truncate(sys.argv[3], int(sys.argv[4]))
PY
	rm -f "$PENDING"
}

format_duration() {
	local s="$1"
	if (( s < 0 )); then
		s=0
	fi
	local h=$((s / 3600))
	local m=$(((s % 3600) / 60))
	local r=$((s % 60))
	if (( h > 0 )); then
		printf '%dh%02dm' "$h" "$m"
	elif (( m > 0 )); then
		printf '%dm%02ds' "$m" "$r"
	else
		printf '%ds' "$s"
	fi
}

format_bytes() {
	awk -v n="$1" 'BEGIN {
		if (n >= 1073741824) printf "%.2fGB", n / 1073741824
		else if (n >= 1048576) printf "%.1fMB", n / 1048576
		else if (n >= 1024) printf "%dKB", n / 1024
		else printf "%dB", n
	}'
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
export SEED
printf '%s\n' "$SEED" > "$SEED_FILE"

touch "$OUT"
touch "$SEEN"
trap 'rm -f "$FLOP_TMP"' EXIT
recover_pending_append
size="$(file_size "$OUT")"
flops="$(wc -l < "$SEEN" | tr -d ' ')"
remaining=$((TARGET_FLOPS - flops))
if (( remaining < 0 )); then
	remaining=0
fi

echo "$DATASET_LABEL soft labels -> $OUT" >&2
echo "  range=$RANGE_SLUG ($RANGE_FLAG)  seed=$SEED  canonical=1755" >&2
echo "  flops=$flops/$TARGET_FLOPS  remaining=$remaining  size=$(format_bytes "$size")" >&2
echo "  ITERATIONS=$ITERATIONS SOFT_MAX_DEPTH=$SOFT_MAX_DEPTH SOFT_MAX_COMBOS=$SOFT_MAX_COMBOS (0=all)" >&2
if (( ${#SOLVER_ARGS[@]} > 0 )); then
	echo "  solver args: ${SOLVER_ARGS[*]}" >&2
fi
if (( flops > 0 )); then
	echo "  resume: skipping $flops already-solved flop(s) in $SEEN" >&2
fi

CANON_LIST=()
while IFS= read -r flop; do
	[[ -n "$flop" ]] && CANON_LIST+=("$flop")
done < <("$BINARY" --canonical-flops)
if [[ "${#CANON_LIST[@]}" -ne 1755 ]]; then
	echo "error: expected 1755 canonical flops, got ${#CANON_LIST[@]}" >&2
	exit 1
fi

n="${#CANON_LIST[@]}"
for (( i = n - 1; i > 0; i-- )); do
	lcg_next
	j=$(( LCG_RAND % (i + 1) ))
	tmp="${CANON_LIST[i]}"
	CANON_LIST[i]="${CANON_LIST[j]}"
	CANON_LIST[j]="$tmp"
done

CANON_IDX=0
SOLVED_THIS_SESSION=0
SOLVE_SECS=0
SESSION_START=$SECONDS

while (( flops < TARGET_FLOPS )); do
	if (( TARGET_BYTES > 0 && size >= TARGET_BYTES )); then
		echo "stopping: size $size >= TARGET_BYTES $TARGET_BYTES" >&2
		break
	fi
	if (( CANON_IDX >= ${#CANON_LIST[@]} )); then
		break
	fi
	FLOP="${CANON_LIST[CANON_IDX]}"
	CANON_IDX=$((CANON_IDX + 1))

	if flop_seen "$FLOP"; then
		continue
	fi

	eta_txt="eta=?"
	if (( SOLVED_THIS_SESSION > 0 )); then
		avg=$(( (SOLVE_SECS + SOLVED_THIS_SESSION - 1) / SOLVED_THIS_SESSION ))
		left=$((TARGET_FLOPS - flops))
		eta_txt="avg=$(format_duration "$avg")  eta=$(format_duration "$((avg * left))")"
	fi
	echo "solving flop $FLOP ($((flops + 1))/$TARGET_FLOPS, remaining=$((TARGET_FLOPS - flops)), size=$(format_bytes "$size"), $eta_txt)" >&2

	flop_start=$SECONDS
	set +e
	"$BINARY" "$FLOP" "$ITERATIONS" "$RANGE_FLAG" --no-flop-iso "${SOLVER_ARGS[@]}" 2>/dev/null \
		| grep '^{"context":' \
		> "$FLOP_TMP"
	pipe_status=("${PIPESTATUS[@]}")
	set -e
	if [[ "${pipe_status[0]}" -ne 0 ]]; then
		echo "error: solver failed for $FLOP (exit ${pipe_status[0]})" >&2
		exit 1
	fi
	if [[ ! -s "$FLOP_TMP" ]]; then
		echo "error: no JSONL rows for $FLOP" >&2
		exit 1
	fi
	seen_size="$(file_size "$SEEN")"
	printf '%s %s %s\n' "$FLOP" "$size" "$seen_size" > "$PENDING"
	cat "$FLOP_TMP" >> "$OUT"
	rm -f "$FLOP_TMP"
	echo "$FLOP" >> "$SEEN"
	rm -f "$PENDING"

	elapsed=$((SECONDS - flop_start))
	SOLVED_THIS_SESSION=$((SOLVED_THIS_SESSION + 1))
	SOLVE_SECS=$((SOLVE_SECS + elapsed))
	size="$(file_size "$OUT")"
	flops="$(wc -l < "$SEEN" | tr -d ' ')"
	avg=$((SOLVE_SECS / SOLVED_THIS_SESSION))
	left=$((TARGET_FLOPS - flops))
	echo "  done $FLOP in $(format_duration "$elapsed")  avg=$(format_duration "$avg")  eta=$(format_duration "$((avg * left))")  session=$(format_duration "$((SECONDS - SESSION_START))")" >&2
done

echo "done: range=$RANGE_SLUG seed=$SEED flops=$flops size=$(format_bytes "$size") session=$(format_duration "$((SECONDS - SESSION_START))") -> $OUT" >&2
