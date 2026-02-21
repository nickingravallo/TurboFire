#include "../include/preflop_solver.h"
#include "../include/gto_solver.h"
#include "../include/range.h"
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

/* True if this infoset has any learned signal on currently legal actions. */
static int infoset_has_learned_signal(const InfoSet *node, uint8_t legal_actions) {
	int a;
	float strategy_mass = 0.0f;
	if (!node || legal_actions == 0)
		return 0;
	for (a = 0; a < PREFLOP_MAX_ACTIONS; a++) {
		if (!(legal_actions & (1u << a)))
			continue;
		if (node->strategy_sum[a] > 0.0f)
			strategy_mass += node->strategy_sum[a];
		if (node->regret_sum[a] > 1e-6f)
			return 1;
	}
	if (strategy_mass > 1e-6f)
		return 1;
	return 0;
}

typedef struct {
	PreflopSolver *ps;
	int thread_id;
	int n_iters;
	unsigned int base_seed;
	HashTable *table;
	int actual_iters;
} worker_arg_t;

/* Context for translating per-table progress into overall merge progress. */
typedef struct {
	PreflopSolver *ps;
	int step;      /* 0-based merge step (0 .. nthreads-1) */
	int nthreads;
} merge_progress_ctx_t;

static void merge_progress_wrapper(void *user, int current, int total) {
	merge_progress_ctx_t *ctx = (merge_progress_ctx_t *)user;
	int overall_current = ctx->step * total + current;
	int overall_total = ctx->nthreads * total;
	if (ctx->ps->merge_progress_cb)
		ctx->ps->merge_progress_cb(ctx->ps->merge_progress_user, overall_current, overall_total);
}

static void *solve_worker(void *arg) {
	worker_arg_t *w = (worker_arg_t *)arg;
	PreflopSolver *ps = w->ps;
	int iter;
	gto_set_thread_table(w->table);
	gto_rng_seed(w->base_seed + (unsigned)w->thread_id);
	gto_reset_table_saturation();
	for (iter = 0; iter < w->n_iters; iter++) {
		uint64_t p1 = sample_hand_from_cache(
			ps->oop_combo_hands, ps->oop_combo_cum_weights,
			ps->oop_combo_count, ps->oop_combo_total_weight
		);
		uint64_t p2 = sample_hand_from_cache(
			ps->ip_combo_hands, ps->ip_combo_cum_weights,
			ps->ip_combo_count, ps->ip_combo_total_weight
		);
		if (!p1 || !p2 || (p1 & p2)) continue;
        
        // No board conflict check needed for preflop (board is empty)
        
		GameState state;
		if (ps->game_mode == PREFLOP_MODE_BTN_SB)
			init_game_state_btn_sb(&state, p1, p2);
		else
			init_game_state(&state, p1, p2);
		gto_mccfr(state, P1);

		if (ps->game_mode == PREFLOP_MODE_BTN_SB)
			init_game_state_btn_sb(&state, p1, p2);
		else
			init_game_state(&state, p1, p2);
		gto_mccfr(state, P2);

		if ((iter & 0xFF) == 0 && iter > 0 && gto_is_table_saturated())
			break;
	}
	w->actual_iters = iter;
	return NULL;
}

#define MAX_COMBOS 12
#define MAX_RANGE_COMBOS 1326

static void build_weighted_combo_cache(
	const float grid[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE],
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
	for (r = 0; r < PREFLOP_GRID_SIZE; r++) {
		for (c = 0; c < PREFLOP_GRID_SIZE; c++) {
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

void preflop_solver_set_ranges(PreflopSolver *ps,
	const float oop[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE],
	const float ip[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE]) {
	if (!ps) return;
	memcpy(ps->oop_weights, oop, sizeof(ps->oop_weights));
	memcpy(ps->ip_weights, ip, sizeof(ps->ip_weights));
	ps->combo_cache_valid = 0;
	ps->solved = 0;
	ps->iterations_done = 0;
}

void preflop_solver_begin_parallel_solve(PreflopSolver *ps) {
	int nthreads, t;
	HashTable **tables;
	if (!ps) return;
	nthreads = get_solver_thread_count();
	ps->parallel_accumulate = 1;
	ps->parallel_nthreads = nthreads;
	ps->parallel_thread_tables = NULL;
	if (nthreads <= 1)
		return;
	if (ps->iterations_done == 0) {
		init_gto_table();
		gto_rng_seed((unsigned)time(NULL));
	}
	tables = (HashTable **)malloc((size_t)nthreads * sizeof(HashTable *));
	if (!tables) {
		ps->parallel_accumulate = 0;
		ps->parallel_nthreads = 0;
		return;
	}
	for (t = 0; t < nthreads; t++) {
		tables[t] = (HashTable *)malloc((size_t)TABLE_SIZE * sizeof(HashTable));
		if (!tables[t]) {
			while (t--) free(tables[t]);
			free(tables);
			ps->parallel_accumulate = 0;
			ps->parallel_nthreads = 0;
			return;
		}
		gto_init_table(tables[t]);
	}
	ps->parallel_thread_tables = tables;
}

void preflop_solver_end_parallel_solve(PreflopSolver *ps) {
	HashTable **thread_tables;
	int nthreads, t;
	if (!ps || !ps->parallel_accumulate) return;
	nthreads = ps->parallel_nthreads;
	thread_tables = (HashTable **)ps->parallel_thread_tables;
	if (thread_tables && nthreads > 1) {
		merge_progress_ctx_t mctx;
		mctx.ps = ps;
		mctx.nthreads = nthreads;
		if (ps->before_merge_cb)
			ps->before_merge_cb(ps->before_merge_user);
		if (ps->merge_progress_cb)
			ps->merge_progress_cb(ps->merge_progress_user, 0, nthreads * TABLE_SIZE);
		for (t = 0; t < nthreads; t++) {
			mctx.step = t;
			int saturated = gto_merge_table_into(gto_table, thread_tables[t],
				ps->merge_progress_cb ? merge_progress_wrapper : NULL,
				ps->merge_progress_cb ? &mctx : NULL);
			free(thread_tables[t]);
			thread_tables[t] = NULL;
			if (saturated) {
				for (t++; t < nthreads; t++) {
					free(thread_tables[t]);
					thread_tables[t] = NULL;
				}
				break;
			}
		}
		if (ps->merge_progress_cb)
			ps->merge_progress_cb(ps->merge_progress_user, nthreads * TABLE_SIZE, nthreads * TABLE_SIZE);
		free(thread_tables);
	}
	ps->parallel_thread_tables = NULL;
	ps->parallel_nthreads = 0;
	ps->parallel_accumulate = 0;
	ps->solved = 1;
}

void preflop_solver_solve(PreflopSolver *ps, int n_iterations) {
	if (!ps || n_iterations <= 0) return;
	if (!ps->combo_cache_valid) {
		uint64_t dead_cards = 0; // No board yet
		build_weighted_combo_cache(
			ps->oop_weights, dead_cards,
			ps->oop_combo_hands, ps->oop_combo_cum_weights,
			&ps->oop_combo_count, &ps->oop_combo_total_weight
		);
		build_weighted_combo_cache(
			ps->ip_weights, dead_cards,
			ps->ip_combo_hands, ps->ip_combo_cum_weights,
			&ps->ip_combo_count, &ps->ip_combo_total_weight
		);
		ps->combo_cache_valid = 1;
	}
	if (ps->oop_combo_count <= 0 || ps->ip_combo_count <= 0)
		return;
	
    if (!ps->parallel_accumulate && ps->iterations_done == 0) {
		init_gto_table();
		gto_rng_seed((unsigned)time(NULL));
	}

	{
		int nthreads = ps->parallel_accumulate ? ps->parallel_nthreads : get_solver_thread_count();
		HashTable **thread_table_ptrs = ps->parallel_accumulate ? (HashTable **)ps->parallel_thread_tables : NULL;
		HashTable *thread_tables_block = NULL;  /* single block only when !parallel_accumulate */
		int alloc_tables = 0;
		int actual_total = n_iterations;

		if (nthreads <= 1) {
			gto_reset_table_saturation();
			for (int iter = 0; iter < n_iterations; iter++) {
				uint64_t p1 = sample_hand_from_cache(
					ps->oop_combo_hands, ps->oop_combo_cum_weights,
					ps->oop_combo_count, ps->oop_combo_total_weight
				);
				uint64_t p2 = sample_hand_from_cache(
					ps->ip_combo_hands, ps->ip_combo_cum_weights,
					ps->ip_combo_count, ps->ip_combo_total_weight
				);
			if (!p1 || !p2 || (p1 & p2)) continue;

			GameState state;
			if (ps->game_mode == PREFLOP_MODE_BTN_SB)
				init_game_state_btn_sb(&state, p1, p2);
			else
				init_game_state(&state, p1, p2);
			gto_mccfr(state, P1);

			if (ps->game_mode == PREFLOP_MODE_BTN_SB)
				init_game_state_btn_sb(&state, p1, p2);
			else
				init_game_state(&state, p1, p2);
			gto_mccfr(state, P2);
				
                if ((iter & 0xFF) == 0 && iter > 0 && gto_is_table_saturated()) {
					actual_total = iter;
					break;
				}
			}
		} else {
			pthread_t *threads;
			worker_arg_t *args;
			unsigned int base_seed = (unsigned)time(NULL);
			int iters_per_thread = n_iterations / nthreads;
			int remainder = n_iterations % nthreads;
			int t;

			if (!thread_table_ptrs) {
				thread_tables_block = (HashTable *)malloc((size_t)nthreads * TABLE_SIZE * sizeof(HashTable));
				alloc_tables = 1;
				if (!thread_tables_block) return;
				for (t = 0; t < nthreads; t++)
					gto_init_table(thread_tables_block + (size_t)t * TABLE_SIZE);
			}

			threads = (pthread_t *)malloc((size_t)nthreads * sizeof(pthread_t));
			args = (worker_arg_t *)malloc((size_t)nthreads * sizeof(worker_arg_t));
			if (!threads || !args) {
				if (alloc_tables) free(thread_tables_block);
				free(threads);
				free(args);
				return;
			}
			for (t = 0; t < nthreads; t++) {
				args[t].ps = ps;
				args[t].thread_id = t;
				args[t].n_iters = iters_per_thread + (t < remainder ? 1 : 0);
				args[t].base_seed = base_seed;
				args[t].table = thread_table_ptrs ? thread_table_ptrs[t] : (thread_tables_block + (size_t)t * TABLE_SIZE);
			}
			for (t = 0; t < nthreads; t++) {
				if (pthread_create(&threads[t], NULL, solve_worker, &args[t]) != 0) {
					for (t--; t >= 0; t--)
						pthread_join(threads[t], NULL);
					if (alloc_tables) free(thread_tables_block);
					free(threads);
					free(args);
					return;
				}
			}
			for (t = 0; t < nthreads; t++)
				pthread_join(threads[t], NULL);
			actual_total = 0;
			for (t = 0; t < nthreads; t++)
				actual_total += args[t].actual_iters;
			free(threads);
			free(args);

			if (!ps->parallel_accumulate) {
				HashTable *merge_src = thread_tables_block;
				merge_progress_ctx_t mctx;
				mctx.ps = ps;
				mctx.nthreads = nthreads;
				if (ps->before_merge_cb)
					ps->before_merge_cb(ps->before_merge_user);
				if (ps->merge_progress_cb)
					ps->merge_progress_cb(ps->merge_progress_user, 0, nthreads * TABLE_SIZE);
				for (t = 0; t < nthreads; t++) {
					mctx.step = t;
					if (gto_merge_table_into(gto_table, merge_src + (size_t)t * TABLE_SIZE,
						ps->merge_progress_cb ? merge_progress_wrapper : NULL,
						ps->merge_progress_cb ? &mctx : NULL) != 0)
						break;
				}
				if (ps->merge_progress_cb)
					ps->merge_progress_cb(ps->merge_progress_user, nthreads * TABLE_SIZE, nthreads * TABLE_SIZE);
				if (alloc_tables)
					free(thread_tables_block);
			}
		}
		ps->solved = 1;
		ps->iterations_done += actual_total;
	}
}

void preflop_solver_replay_history(int game_mode, uint64_t history, int num_actions, GameState* out_state) {
    GameState s;
    int i;
    if (!out_state) return;
    if (game_mode == PREFLOP_MODE_BTN_SB)
        init_game_state_btn_sb(&s, 0, 0);
    else
        init_game_state(&s, 0, 0);
    if (num_actions <= 0) {
        *out_state = s;
        return;
    }
    for (i = 0; i < num_actions && !s.is_terminal; i++) {
        int a = (int)((history >> (i * BITS_PER_ACTION)) & 7u);
        s = gto_apply_action(s, a);
    }
    *out_state = s;
}

void preflop_solver_get_state_at_history(const PreflopSolver *ps, uint64_t history, int num_actions, GameState *out_state) {
	if (!ps || !out_state) return;
	preflop_solver_replay_history(ps->game_mode, history, num_actions, out_state);
}

int preflop_solver_get_hand_strategy(
	int game_mode, uint64_t history, int num_actions, uint64_t hole_hand,
	float probs[PREFLOP_MAX_ACTIONS]
) {
	GameState state;
	uint8_t legal_actions;
	float legal_sum = 0.0f;
	int a;

	preflop_solver_replay_history(game_mode, history, num_actions, &state);
    
	InfoSet *node = gto_get_node(make_info_set_key(
		history, state.board, hole_hand, state.pot, state.p1_stack, state.p2_stack
	));
	if (!node) return -1;
	legal_actions = gto_get_legal_actions(&state);
	if (!infoset_has_learned_signal(node, legal_actions))
		return -1;
	gto_get_average_strategy(node, probs);
	for (a = 0; a < PREFLOP_MAX_ACTIONS; a++) {
		if (!(legal_actions & (1u << a)))
			probs[a] = 0.0f;
		legal_sum += probs[a];
	}
	if (legal_sum <= 0.0f) {
		gto_get_strategy(node->regret_sum, probs, legal_actions);
	} else {
		for (a = 0; a < PREFLOP_MAX_ACTIONS; a++)
			probs[a] /= legal_sum;
	}
	return 0;
}

int preflop_solver_get_strategy_at_history(const PreflopSolver *ps, uint64_t history, int num_actions,
	int row, int col, float probs[PREFLOP_MAX_ACTIONS], int *num_actions_out) {
	GameState state;
	const float (*weights)[PREFLOP_GRID_SIZE];
	char hand_str[8];
	uint64_t c1[MAX_COMBOS], c2[MAX_COMBOS];
	int n, i, a, n_actions, n_seen;
	float sum[PREFLOP_MAX_ACTIONS], total;

	if (!ps || !ps->solved || row < 0 || row >= PREFLOP_GRID_SIZE || col < 0 || col >= PREFLOP_GRID_SIZE)
		return -1;
	preflop_solver_get_state_at_history(ps, history, num_actions, &state);
	if (state.is_terminal) return -1;
	weights = (state.active_player == P1) ? ps->oop_weights : ps->ip_weights;
	if (weights[row][col] <= 0.0f) return -1;
	
    // Legal Actions check (approximate count for UI)
    // For proper UI, we should check legal_actions mask from state.
    uint8_t mask = gto_get_legal_actions(&state);
    n_actions = 0;
    for(int k=0; k<PREFLOP_MAX_ACTIONS; k++) if(mask & (1<<k)) n_actions = k+1; // Highest action index + 1? No.
    // Actually we just want max size. The caller expects an int count.
    // If facing bet/open: 5. If not: 4.
    n_actions = state.facing_bet || (state.street == STREET_PREFLOP && state.to_call > 0) ? 5 : 4;

	if (num_actions_out) *num_actions_out = n_actions;

	hand_at(row, col, hand_str, sizeof(hand_str));
    // board is empty for preflop
	n = hand_string_to_combos(hand_str, 0, c1, c2, MAX_COMBOS);
	if (n <= 0) return -1;
	for (a = 0; a < PREFLOP_MAX_ACTIONS; a++) sum[a] = 0.0f;
	n_seen = 0;
	for (i = 0; i < n; i++) {
		uint64_t hand = c1[i] | c2[i];
		float p[PREFLOP_MAX_ACTIONS];
		if (preflop_solver_get_hand_strategy(
			ps->game_mode, history, num_actions, hand, p
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
	for (a = n_actions; a < PREFLOP_MAX_ACTIONS; a++) probs[a] = 0.0f;
	return 0;
}

int preflop_solver_export_range_grid(
	const PreflopSolver *ps, uint64_t history, int num_actions,
	float out_grid[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE])
{
	int r, c;
	GameState state;
	int is_facing_bet;

	if (!ps || !ps->solved || !out_grid)
		return -1;

	preflop_solver_get_state_at_history(ps, history, num_actions, &state);
	if (state.is_terminal)
		return -1;

	is_facing_bet = state.facing_bet || (state.street == STREET_PREFLOP && state.to_call > 0.0f);

	for (r = 0; r < PREFLOP_GRID_SIZE; r++) {
		for (c = 0; c < PREFLOP_GRID_SIZE; c++) {
			float probs[PREFLOP_MAX_ACTIONS];
			int n_actions;
			out_grid[r][c] = 0.0f;
			if (preflop_solver_get_strategy_at_history(ps, history, num_actions, r, c, probs, &n_actions) != 0)
				continue;
			if (is_facing_bet) {
				/* Action 0 = Fold. Range weight = 1 - fold frequency. */
				float fold_pct = probs[0];
				out_grid[r][c] = 1.0f - fold_pct;
			} else {
				/* Action 0 = Check. Range weight = 1 (all hands continue). */
				out_grid[r][c] = 1.0f;
			}
		}
	}
	return 0;
}

int preflop_solver_save_range_json(
	const char *path, const char *name, const char *description,
	const float grid[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE])
{
	FILE *f;
	int r, c, first;
	char hand_str[8];

	if (!path || !grid)
		return -1;

	f = fopen(path, "w");
	if (!f)
		return -1;

	fprintf(f, "{\n");
	fprintf(f, "  \"name\": \"%s\",\n", name ? name : "preflop_range");
	fprintf(f, "  \"description\": \"%s\",\n", description ? description : "Solved preflop range");
	fprintf(f, "  \"hands\": {\n");

	first = 1;
	for (r = 0; r < PREFLOP_GRID_SIZE; r++) {
		for (c = 0; c < PREFLOP_GRID_SIZE; c++) {
			float w = grid[r][c];
			if (w < 0.005f)
				continue;
			hand_at(r, c, hand_str, sizeof(hand_str));
			if (!first)
				fprintf(f, ",\n");
			if (w >= 0.995f)
				fprintf(f, "    \"%s\": 1.0", hand_str);
			else
				fprintf(f, "    \"%s\": %.3f", hand_str, w);
			first = 0;
		}
	}

	fprintf(f, "\n  }\n}\n");
	fclose(f);
	return 0;
}
