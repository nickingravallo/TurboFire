#include "flop_solver.h"
#include "gto_solver.h"
#include "range.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

#define MAX_SOLVER_THREADS 8

static uint64_t sample_hand_from_cache(const uint64_t *, const float *, int, float);

static int get_solver_thread_count(void) {
	const char *env = getenv("TURBOFIRE_NTHREADS");
	if (env) {
		long n = strtol(env, NULL, 10);
		if (n > 0 && n <= MAX_SOLVER_THREADS)
			return (int)n;
		if (n > MAX_SOLVER_THREADS)
			return MAX_SOLVER_THREADS;
	}
	{
		long n = sysconf(_SC_NPROCESSORS_ONLN);
		if (n > 0 && n <= MAX_SOLVER_THREADS)
			return (int)n;
		if (n > MAX_SOLVER_THREADS)
			return MAX_SOLVER_THREADS;
	}
	return 1;
}

typedef struct {
	FlopSolver *fs;
	int thread_id;
	int n_iters;
	unsigned int base_seed;
	HashTable *table;
} worker_arg_t;

static void *solve_worker(void *arg) {
	worker_arg_t *w = (worker_arg_t *)arg;
	FlopSolver *fs = w->fs;
	int iter;
	gto_set_thread_table(w->table);
	gto_rng_seed(w->base_seed + (unsigned)w->thread_id);
	for (iter = 0; iter < w->n_iters; iter++) {
		uint64_t p1 = sample_hand_from_cache(
			fs->oop_combo_hands, fs->oop_combo_cum_weights,
			fs->oop_combo_count, fs->oop_combo_total_weight
		);
		uint64_t p2 = sample_hand_from_cache(
			fs->ip_combo_hands, fs->ip_combo_cum_weights,
			fs->ip_combo_count, fs->ip_combo_total_weight
		);
		if (!p1 || !p2 || (p1 & p2)) continue;
		if ((p1 & fs->board) || (p2 & fs->board)) continue;
		GameState state;
		gto_init_postflop_state(
			&state, p1, p2, fs->board, fs->preset_turn_card, fs->preset_river_card
		);
		gto_mccfr(state, P1);
		gto_init_postflop_state(
			&state, p1, p2, fs->board, fs->preset_turn_card, fs->preset_river_card
		);
		gto_mccfr(state, P2);
	}
	return NULL;
}

#define MAX_COMBOS 12
#define MAX_RANGE_COMBOS 1326

static uint64_t g_flop_board;

static void build_weighted_combo_cache(
	const float grid[FLOP_GRID_SIZE][FLOP_GRID_SIZE],
	uint64_t dead_cards,
	uint64_t out_hands[MAX_RANGE_COMBOS],
	float out_cum_weights[MAX_RANGE_COMBOS],
	int *out_count,
	float *out_total_weight
) {
	int r, c;
	int n_out = 0;
	float total = 0.0f;
	char hand_str[8];
	uint64_t c1[MAX_COMBOS], c2[MAX_COMBOS];
	if (!out_count || !out_total_weight)
		return;
	for (r = 0; r < FLOP_GRID_SIZE; r++) {
		for (c = 0; c < FLOP_GRID_SIZE; c++) {
			float weight = grid[r][c];
			int n, i;
			if (weight <= 0.0f)
				continue;
			hand_at(r, c, hand_str, sizeof(hand_str));
			n = hand_string_to_combos(hand_str, dead_cards, c1, c2, MAX_COMBOS);
			if (n <= 0)
				continue;
			for (i = 0; i < n && n_out < MAX_RANGE_COMBOS; i++) {
				out_hands[n_out] = c1[i] | c2[i];
				total += weight;
				out_cum_weights[n_out] = total;
				n_out++;
			}
		}
	}
	*out_count = n_out;
	*out_total_weight = total;
}

static uint64_t sample_hand_from_cache(
	const uint64_t *hands,
	const float *cum_weights,
	int n_hands,
	float total_weight
) {
	int lo, hi;
	float u;
	if (n_hands <= 0 || total_weight <= 0.0f)
		return 0;
	u = gto_rng_uniform() * total_weight;
	if (u >= total_weight)
		u = total_weight * 0.99999994f;
	lo = 0;
	hi = n_hands - 1;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (u < cum_weights[mid])
			hi = mid;
		else
			lo = mid + 1;
	}
	return hands[lo];
}

void flop_solver_set_board_runout(
	FlopSolver *fs, uint64_t flop_board, uint64_t preset_turn_card, uint64_t preset_river_card
) {
	if (!fs) return;
	fs->board = flop_board;
	fs->preset_turn_card = preset_turn_card;
	fs->preset_river_card = preset_river_card;
	fs->combo_cache_valid = 0;
	fs->solved = 0;
	fs->iterations_done = 0;
}

void flop_solver_set_board(FlopSolver *fs, uint64_t board) {
	flop_solver_set_board_runout(fs, board, 0, 0);
}

void flop_solver_set_ranges(FlopSolver *fs,
	const float oop[FLOP_GRID_SIZE][FLOP_GRID_SIZE],
	const float ip[FLOP_GRID_SIZE][FLOP_GRID_SIZE]) {
	if (!fs) return;
	memcpy(fs->oop_weights, oop, sizeof(fs->oop_weights));
	memcpy(fs->ip_weights, ip, sizeof(fs->ip_weights));
	fs->combo_cache_valid = 0;
	fs->solved = 0;
	fs->iterations_done = 0;
}

void flop_solver_begin_parallel_solve(FlopSolver *fs) {
	int nthreads;
	if (!fs) return;
	nthreads = get_solver_thread_count();
	fs->parallel_accumulate = 1;
	fs->parallel_nthreads = nthreads;
	fs->parallel_thread_tables = NULL;
	if (nthreads <= 1)
		return;
	if (fs->iterations_done == 0) {
		init_gto_table();
		gto_rng_seed((unsigned)time(NULL));
	}
	fs->parallel_thread_tables = malloc((size_t)nthreads * TABLE_SIZE * sizeof(HashTable));
	if (!fs->parallel_thread_tables) {
		fs->parallel_accumulate = 0;
		fs->parallel_nthreads = 0;
		return;
	}
	for (int t = 0; t < nthreads; t++)
		gto_init_table((HashTable *)fs->parallel_thread_tables + (size_t)t * TABLE_SIZE);
}

void flop_solver_end_parallel_solve(FlopSolver *fs) {
	HashTable *thread_tables;
	int nthreads;
	if (!fs || !fs->parallel_accumulate) return;
	nthreads = fs->parallel_nthreads;
	thread_tables = (HashTable *)fs->parallel_thread_tables;
	if (thread_tables && nthreads > 1) {
		int t;
		if (fs->before_merge_cb)
			fs->before_merge_cb(fs->before_merge_user);
		for (t = 1; t < nthreads; t++)
			gto_merge_table_into(thread_tables + 0 * TABLE_SIZE, thread_tables + (size_t)t * TABLE_SIZE);
		gto_merge_table_into(gto_table, thread_tables + 0 * TABLE_SIZE);
		free(thread_tables);
	}
	fs->parallel_thread_tables = NULL;
	fs->parallel_nthreads = 0;
	fs->parallel_accumulate = 0;
	fs->solved = 1;
}

void flop_solver_solve(FlopSolver *fs, int n_iterations) {
	if (!fs || n_iterations <= 0) return;
	g_flop_board = fs->board;
	if (!fs->combo_cache_valid) {
		uint64_t dead_cards = fs->board | fs->preset_turn_card | fs->preset_river_card;
		build_weighted_combo_cache(
			fs->oop_weights, dead_cards,
			fs->oop_combo_hands, fs->oop_combo_cum_weights,
			&fs->oop_combo_count, &fs->oop_combo_total_weight
		);
		build_weighted_combo_cache(
			fs->ip_weights, dead_cards,
			fs->ip_combo_hands, fs->ip_combo_cum_weights,
			&fs->ip_combo_count, &fs->ip_combo_total_weight
		);
		fs->combo_cache_valid = 1;
	}
	if (fs->oop_combo_count <= 0 || fs->ip_combo_count <= 0)
		return;
	/* Preserve table across chunked solve calls; reset only at start of a fresh solve. */
	if (!fs->parallel_accumulate && fs->iterations_done == 0) {
		init_gto_table();
		gto_rng_seed((unsigned)time(NULL));
	}

	{
		int nthreads = fs->parallel_accumulate ? fs->parallel_nthreads : get_solver_thread_count();
		HashTable *thread_tables = (HashTable *)fs->parallel_thread_tables;
		int alloc_tables = 0;

		if (nthreads <= 1) {
			for (int iter = 0; iter < n_iterations; iter++) {
				uint64_t p1 = sample_hand_from_cache(
					fs->oop_combo_hands, fs->oop_combo_cum_weights,
					fs->oop_combo_count, fs->oop_combo_total_weight
				);
				uint64_t p2 = sample_hand_from_cache(
					fs->ip_combo_hands, fs->ip_combo_cum_weights,
					fs->ip_combo_count, fs->ip_combo_total_weight
				);
				if (!p1 || !p2 || (p1 & p2)) continue;
				if ((p1 & fs->board) || (p2 & fs->board)) continue;
				GameState state;
				gto_init_postflop_state(
					&state, p1, p2, fs->board, fs->preset_turn_card, fs->preset_river_card
				);
				gto_mccfr(state, P1);
				gto_init_postflop_state(
					&state, p1, p2, fs->board, fs->preset_turn_card, fs->preset_river_card
				);
				gto_mccfr(state, P2);
			}
		} else {
			pthread_t *threads;
			worker_arg_t *args;
			unsigned int base_seed = (unsigned)time(NULL);
			int iters_per_thread = n_iterations / nthreads;
			int remainder = n_iterations % nthreads;
			int t;

			if (!thread_tables) {
				thread_tables = (HashTable *)malloc((size_t)nthreads * TABLE_SIZE * sizeof(HashTable));
				alloc_tables = 1;
				if (!thread_tables) return;
				for (t = 0; t < nthreads; t++)
					gto_init_table(thread_tables + (size_t)t * TABLE_SIZE);
			}

			threads = (pthread_t *)malloc((size_t)nthreads * sizeof(pthread_t));
			args = (worker_arg_t *)malloc((size_t)nthreads * sizeof(worker_arg_t));
			if (!threads || !args) {
				if (alloc_tables) free(thread_tables);
				free(threads);
				free(args);
				return;
			}
			for (t = 0; t < nthreads; t++) {
				args[t].fs = fs;
				args[t].thread_id = t;
				args[t].n_iters = iters_per_thread + (t < remainder ? 1 : 0);
				args[t].base_seed = base_seed;
				args[t].table = thread_tables + (size_t)t * TABLE_SIZE;
			}
			for (t = 0; t < nthreads; t++) {
				if (pthread_create(&threads[t], NULL, solve_worker, &args[t]) != 0) {
					for (t--; t >= 0; t--)
						pthread_join(threads[t], NULL);
					if (alloc_tables) free(thread_tables);
					free(threads);
					free(args);
					return;
				}
			}
			for (t = 0; t < nthreads; t++)
				pthread_join(threads[t], NULL);
			free(threads);
			free(args);

			if (!fs->parallel_accumulate) {
				if (fs->before_merge_cb)
					fs->before_merge_cb(fs->before_merge_user);
				for (t = 1; t < nthreads; t++)
					gto_merge_table_into(thread_tables + 0 * TABLE_SIZE, thread_tables + (size_t)t * TABLE_SIZE);
				gto_merge_table_into(gto_table, thread_tables + 0 * TABLE_SIZE);
				if (alloc_tables)
					free(thread_tables);
			}
		}
	}
	fs->solved = 1;
	fs->iterations_done += n_iterations;
}

int flop_solver_get_hand_strategy_with_runout(
	uint64_t history, uint64_t flop_board, uint64_t preset_turn_card, uint64_t preset_river_card,
	int num_actions, uint64_t hole_hand, float probs[FLOP_MAX_ACTIONS]
) {
	GameState state;
	uint8_t legal_actions;
	float legal_sum = 0.0f;
	int a;
	gto_replay_postflop_history(
		history, flop_board, preset_turn_card, preset_river_card, num_actions, &state
	);
	InfoSet *node = gto_get_node(make_info_set_key(
		history, state.board, hole_hand, state.pot, state.p1_stack, state.p2_stack
	));
	if (!node) return -1;
	legal_actions = gto_get_legal_actions(&state);
	gto_get_average_strategy(node, probs);
	for (a = 0; a < FLOP_MAX_ACTIONS; a++) {
		if (!(legal_actions & (1u << a)))
			probs[a] = 0.0f;
		legal_sum += probs[a];
	}
	if (legal_sum <= 0.0f) {
		/* Sparse nodes can have zero average mass; fall back to current regret-matched strategy. */
		gto_get_strategy(node->regret_sum, probs, legal_actions);
	} else {
		for (a = 0; a < FLOP_MAX_ACTIONS; a++)
			probs[a] /= legal_sum;
	}
	return 0;
}

int flop_solver_get_hand_strategy(
	uint64_t history, uint64_t board, int num_actions, uint64_t hole_hand,
	float probs[FLOP_MAX_ACTIONS]
) {
	return flop_solver_get_hand_strategy_with_runout(
		history, board, 0, 0, num_actions, hole_hand, probs
	);
}

int flop_solver_get_oop_strategy(const FlopSolver *fs, int row, int col, float probs[FLOP_MAX_ACTIONS]) {
	return flop_solver_get_strategy_at_history(fs, 0, 0, row, col, probs, NULL);
}

void flop_solver_get_state_at_history(const FlopSolver *fs, uint64_t history, int num_actions, GameState *out_state) {
	if (!fs || !out_state) return;
	gto_replay_postflop_history(
		history, fs->board, fs->preset_turn_card, fs->preset_river_card, num_actions, out_state
	);
}

int flop_solver_get_strategy_at_history(const FlopSolver *fs, uint64_t history, int num_actions,
	int row, int col, float probs[FLOP_MAX_ACTIONS], int *num_actions_out) {
	GameState state;
	const float (*weights)[FLOP_GRID_SIZE];
	char hand_str[8];
	uint64_t c1[MAX_COMBOS], c2[MAX_COMBOS];
	int n, i, a, n_actions, n_seen;
	float sum[FLOP_MAX_ACTIONS], total;

	if (!fs || !fs->solved || row < 0 || row >= FLOP_GRID_SIZE || col < 0 || col >= FLOP_GRID_SIZE)
		return -1;
	flop_solver_get_state_at_history(fs, history, num_actions, &state);
	if (state.is_terminal) return -1;
	weights = (state.active_player == P1) ? fs->oop_weights : fs->ip_weights;
	if (weights[row][col] <= 0.0f) return -1;
	n_actions = state.facing_bet ? 5 : 6;
	if (num_actions_out) *num_actions_out = n_actions;

	hand_at(row, col, hand_str, sizeof(hand_str));
	n = hand_string_to_combos(hand_str, fs->board, c1, c2, MAX_COMBOS);
	if (n <= 0) return -1;
	for (a = 0; a < FLOP_MAX_ACTIONS; a++) sum[a] = 0.0f;
	n_seen = 0;
	for (i = 0; i < n; i++) {
		uint64_t hand = c1[i] | c2[i];
		float p[FLOP_MAX_ACTIONS];
		if (flop_solver_get_hand_strategy_with_runout(
			history, fs->board, fs->preset_turn_card, fs->preset_river_card, num_actions, hand, p
		) == 0) {
			for (a = 0; a < n_actions; a++) sum[a] += p[a];
			n_seen++;
		}
	}
	if (n_seen <= 0) return -1;
	for (a = 0; a < n_actions; a++) probs[a] = sum[a] / (float)n_seen;
	total = 0.0f;
	for (a = 0; a < n_actions; a++) total += probs[a];
	if (total > 0.0f) {
		for (a = 0; a < n_actions; a++) probs[a] /= total;
	}
	for (a = n_actions; a < FLOP_MAX_ACTIONS; a++) probs[a] = 0.0f;
	return 0;
}
