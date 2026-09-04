#include "config.h"
#include "dcfr.h"
#include "isomorphism.h"
#include "ranges.h"
#if WALK_TREE
#include "walk_tree.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#define NUM_COMBOS 1326

static const char* street_label(int max_street) {
	switch (max_street) {
		case 0: return "flop only";
		case 1: return "flop + turn";
		default: return "flop + turn + river";
	}
}

static double timespec_seconds(struct timespec t) {
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static double elapsed_seconds(struct timespec start, struct timespec end) {
	return timespec_seconds(end) - timespec_seconds(start);
}

static void print_help(const char* argv0) {
	printf(
		"Usage: %s [board] [iterations] [options]\n"
		"\n"
		"Positional:\n"
		"  board         Flop string, e.g. AsKd4h (default: AsKd4h)\n"
		"  iterations    DCFR iterations (default: 200)\n"
		"\n"
		"Options:\n"
		"  -h, --help              Show this help\n"
		"  --range=wide            Wide starting ranges (default)\n"
		"                          BTN ~78%%, BB ~88%% of live combos\n"
		"  --range=condensed       Condensed starting ranges\n"
		"                          BTN ~25%%, BB ~28%% of live combos\n"
		"  --range=10pct           Top 10%% of live combos for both players\n"
		"  --condensed             Shortcut for --range=condensed\n"
		"  --tight                 Shortcut for --range=condensed\n"
		"  --wide                  Shortcut for --range=wide\n"
		"  --bets=PCT,...          Pot-percent bet sizes (1-5; default 10,25,52,100,123)\n"
		"  --raises=X,...          Raise multipliers (1-3; default 2,3,4)\n"
		"  --max-raises=N          Raises per street, 0-2 (default 2)\n"
		"  --allin                 Add an explicit all-in bet/raise action\n"
		"  --canonical-flops       Print the 1,755 suit-canonical flops and exit\n"
		"  --no-flop-iso           Do not map a 3-card board to its canonical flop\n"
		"\n"
		"Build-time (Makefile):\n"
		"  make                    turbofire       flop-only + soft JSONL\n"
		"  make turn               turbofire_turn  flop+turn, no walk_tree\n"
		"  make river              turbofire_river flop+turn+river, no traversal\n"
		"  make test               solver, isomorphism, and NashGPT tests\n"
		"\n"
		"Env:\n"
		"  OMP_NUM_THREADS         OpenMP thread count\n"
		"  SOFT_MAX_DEPTH          soft-label betting depth (walk_tree)\n"
		"  SOFT_MAX_COMBOS         soft-label combos per node (0 = all)\n"
		"\n"
		"Examples:\n"
		"  %s\n"
		"  %s AsKd4h 50\n"
		"  %s AsKd4h 50 --condensed\n"
		"  %s QsJh2c 200 --range=wide\n"
		"  %s AsKd4h 10 --range=10pct --bets=25,75 --raises=2 --max-raises=1\n",
		argv0,
		argv0,
		argv0,
		argv0,
		argv0,
		argv0
	);
}

static int parse_range_value(const char* value, RangeMode* out_mode) {
	if (strcmp(value, "wide") == 0) {
		*out_mode = RANGE_WIDE;
		return 1;
	}
	if (strcmp(value, "condensed") == 0 || strcmp(value, "tight") == 0) {
		*out_mode = RANGE_CONDENSED;
		return 1;
	}
	if (strcmp(value, "10pct") == 0 || strcmp(value, "10%") == 0) {
		*out_mode = RANGE_TEN_PERCENT;
		return 1;
	}
	return 0;
}

static int parse_size_list(const char* text, float* values, int max_values, float scale) {
	const char* cursor = text;
	int count = 0;

	if (!text || !*text)
		return 0;

	while (*cursor) {
		char* end;
		float value;

		if (count >= max_values)
			return 0;
		value = strtof(cursor, &end);
		if (end == cursor || value <= 0.0f)
			return 0;
		values[count++] = value * scale;
		if (*end == '\0')
			break;
		if (*end != ',')
			return 0;
		cursor = end + 1;
		if (!*cursor)
			return 0;
	}

	return count;
}

int main(int argc, char **argv) {
	const char* board_str = "AsKd4h";
	int iterations = 200;
	RangeMode range_mode = RANGE_WIDE;
	int have_board = 0;
	int have_iters = 0;
	int list_canonical = 0;
	int skip_flop_iso = 0;
	int allin_enabled = 0;
	uint64_t board;
	char canon_board_str[16];
	GameState initial_state;
	PublicNode* root;
	float* p1_range;
	float* p2_range;
	struct timespec project_start;
	struct timespec project_end;
	struct timespec train_start;
	struct timespec train_end;
	struct timespec iter_start;
	struct timespec iter_end;
	double train_total = 0.0;
	double iter_min = 0.0;
	double iter_max = 0.0;

	for (int i = 1; i < argc; i++) {
		const char* arg = argv[i];

		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
			print_help(argv[0]);
			return 0;
		}
		if (strcmp(arg, "--canonical-flops") == 0) {
			list_canonical = 1;
			continue;
		}
		if (strcmp(arg, "--no-flop-iso") == 0) {
			skip_flop_iso = 1;
			continue;
		}
		if (strcmp(arg, "--allin") == 0) {
			allin_enabled = 1;
			set_allin_enabled(1);
			continue;
		}
		if (strcmp(arg, "--condensed") == 0 || strcmp(arg, "--tight") == 0) {
			range_mode = RANGE_CONDENSED;
			continue;
		}
		if (strcmp(arg, "--wide") == 0) {
			range_mode = RANGE_WIDE;
			continue;
		}
		if (strncmp(arg, "--range=", 8) == 0) {
			if (!parse_range_value(arg + 8, &range_mode)) {
				fprintf(stderr, "unknown --range value '%s' (use wide|condensed|10pct)\n", arg + 8);
				return 1;
			}
			continue;
		}
		if (strncmp(arg, "--bets=", 7) == 0) {
			float sizes[MAX_BET_SIZES];
			int count = parse_size_list(arg + 7, sizes, MAX_BET_SIZES, 0.01f);
			if (!count || !set_bet_sizes(sizes, count)) {
				fprintf(stderr, "--bets requires 1-%d positive pot percentages, e.g. --bets=25,75\n", MAX_BET_SIZES);
				return 1;
			}
			continue;
		}
		if (strncmp(arg, "--raises=", 9) == 0) {
			float sizes[MAX_RAISE_SIZES];
			int count = parse_size_list(arg + 9, sizes, MAX_RAISE_SIZES, 1.0f);
			if (!count || !set_raise_sizes(sizes, count)) {
				fprintf(stderr, "--raises requires 1-%d multipliers greater than 1, e.g. --raises=2,3\n", MAX_RAISE_SIZES);
				return 1;
			}
			continue;
		}
		if (strncmp(arg, "--max-raises=", 13) == 0) {
			char* end;
			long value = strtol(arg + 13, &end, 10);
			if (*end || !set_max_raises((int)value)) {
				fprintf(stderr, "--max-raises requires 0, 1, or 2\n");
				return 1;
			}
			continue;
		}
		if (strcmp(arg, "--range") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "--range requires wide|condensed|10pct\n");
				return 1;
			}
			i += 1;
			if (!parse_range_value(argv[i], &range_mode)) {
				fprintf(stderr, "unknown --range value '%s' (use wide|condensed|10pct)\n", argv[i]);
				return 1;
			}
			continue;
		}
		if (arg[0] == '-') {
			fprintf(stderr, "unknown option '%s' (try --help)\n", arg);
			return 1;
		}
		if (!have_board) {
			board_str = arg;
			have_board = 1;
			continue;
		}
		if (!have_iters) {
			iterations = atoi(arg);
			have_iters = 1;
			continue;
		}

		fprintf(stderr, "unexpected argument '%s' (try --help)\n", arg);
		return 1;
	}

	if (iterations < 1)
		iterations = 1;

	if (list_canonical) {
		uint8_t flops[NUM_CANONICAL_FLOPS][3];
		int n = collect_canonical_flops(flops, NUM_CANONICAL_FLOPS);

		if (n != NUM_CANONICAL_FLOPS) {
			fprintf(stderr, "canonical flop count %d, expected %d\n", n, NUM_CANONICAL_FLOPS);
			return 1;
		}
		for (int i = 0; i < n; i++) {
			char buf[16];

			format_flop_string(flops[i], buf, sizeof(buf), 0);
			printf("%s\n", buf);
		}
		return 0;
	}

	clock_gettime(CLOCK_MONOTONIC, &project_start);
	init_evaluator();

	board = parse_board_string(board_str);
	if (!skip_flop_iso && __builtin_popcountll(board) == 3) {
		uint64_t canonical = canonicalize_flop_board(board);

		if (canonical != board) {
			uint8_t flop[3];
			int n = 0;

			for (int card = 0; card < 52 && n < 3; card++) {
				int rank = card % 13;
				int suit = card / 13;

				if (canonical & (1ULL << (rank + suit * 16)))
					flop[n++] = (uint8_t)card;
			}
			format_flop_string(flop, canon_board_str, sizeof(canon_board_str), 0);
			printf("flop iso: %s -> %s\n", board_str, canon_board_str);
			board = canonical;
			board_str = canon_board_str;
		}
	}
	initial_state = (GameState){
		.board = board,
		.pot = 200,
		.p1_stack = 900,
		.p2_stack = 900,
		.p1_commit = 0,
		.p2_commit = 0,
		.p1_invested = 100,
		.p2_invested = 100,
		.active_player = P1,
		.street = 0,
		.raises_this_street = 0,
		.num_actions_this_street = 0,
		.last_action_was_fold = 0,
	};

	p1_range = (float*)calloc(NUM_COMBOS, sizeof(float));
	p2_range = (float*)calloc(NUM_COMBOS, sizeof(float));

	init_bb_range(board, NUM_COMBOS, p1_range, range_mode);
	init_btn_range(board, NUM_COMBOS, p2_range, range_mode);

	printf("solving: %s (MAX_STREET=%d)\n", street_label(MAX_STREET), MAX_STREET);
	printf("board: %s\n", board_str);
	printf("iterations: %d\n", iterations);
	printf("seats: P1=OOP (BB), P2=IP (BTN) — %s\n", range_mode_label(range_mode));
	printf("betting: %d bet size(s), %d raise size(s), max %d raise(s) per street\n",
		configured_bet_count(), configured_raise_count(), configured_max_raises());
	printf("all-in action: %s\n", allin_enabled ? "enabled" : "disabled");
#ifdef _OPENMP
	printf("OpenMP: %d threads\n", omp_get_max_threads());
#endif
#if WALK_TREE
	printf("walk_tree: enabled\n");
#else
	printf("walk_tree: disabled (add -DWALK_TREE=1 to CFLAGS to enable)\n");
#endif

	root = build_tree(initial_state, NUM_COMBOS);
	if (!root) {
		free(p1_range);
		free(p2_range);
		return 1;
	}

	clock_gettime(CLOCK_MONOTONIC, &train_start);
	for (int iteration = 1; iteration <= iterations; iteration++) {
		double iter_secs;

		printf("training iteration %d/%d...\r", iteration, iterations);
		fflush(stdout);

		clock_gettime(CLOCK_MONOTONIC, &iter_start);
		train(root, initial_state, NUM_COMBOS, p1_range, p2_range, iteration);
		clock_gettime(CLOCK_MONOTONIC, &iter_end);

		iter_secs = elapsed_seconds(iter_start, iter_end);
		train_total += iter_secs;
		if (iteration == 1) {
			iter_min = iter_secs;
			iter_max = iter_secs;
		}
		else {
			if (iter_secs < iter_min)
				iter_min = iter_secs;
			if (iter_secs > iter_max)
				iter_max = iter_secs;
		}

		printf(
			"training iteration %d/%d  (%.3fs, avg %.3fs)\n",
			iteration,
			iterations,
			iter_secs,
			train_total / (double)iteration
		);
		fflush(stdout);
	}
	clock_gettime(CLOCK_MONOTONIC, &train_end);

#if WALK_TREE
	printf("emitting soft-label JSONL...\n");
	fflush(stdout);
	walk_tree(root, initial_state, NUM_COMBOS, p1_range, p2_range);
#endif

	destroy_tree(root);
	free(p1_range);
	free(p2_range);

	clock_gettime(CLOCK_MONOTONIC, &project_end);
	printf(
		"timing: train %.3fs  (min %.3fs / avg %.3fs / max %.3fs over %d iters)  project %.3fs\n",
		elapsed_seconds(train_start, train_end),
		iter_min,
		train_total / (double)iterations,
		iter_max,
		iterations,
		elapsed_seconds(project_start, project_end)
	);

	return 0;
}
