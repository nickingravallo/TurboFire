#ifndef PREFLOP_SOLVER_H
#define PREFLOP_SOLVER_H

#include <stdint.h>
#include <stdbool.h>
#include "gto_solver.h"

#define PREFLOP_GRID_SIZE 13
#define PREFLOP_MAX_ACTIONS 8

#define PREFLOP_MODE_HU       0
#define PREFLOP_MODE_BTN_SB   1

typedef struct {
	int game_mode;  /* PREFLOP_MODE_HU or PREFLOP_MODE_BTN_SB */
	float oop_weights[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE];
	float ip_weights[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE];
	
	/* No board required for preflop start, but we might want to store it if we did partial solves? */
	/* For now, assume board is empty at start. */
	
	uint64_t oop_combo_hands[1326];
	uint64_t ip_combo_hands[1326];
	float oop_combo_cum_weights[1326];
	float ip_combo_cum_weights[1326];
	float oop_combo_total_weight;
	float ip_combo_total_weight;
	int oop_combo_count;
	int ip_combo_count;
	int combo_cache_valid;
	int solved;               /* 1 after solve() */
	int iterations_done;
	
	/* Callbacks */
	void (*before_merge_cb)(void *user);
	void *before_merge_user;
	void (*merge_progress_cb)(void *user, int current, int total);
	void *merge_progress_user;
	
	/* Parallelism */
	void *parallel_thread_tables;  /* HashTable * per thread */
	int parallel_nthreads;
	int parallel_accumulate;
} PreflopSolver;

/* Set OOP and IP range grids (weights 0..1). Clears solved state. */
void preflop_solver_set_ranges(PreflopSolver *ps,
	const float oop[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE],
	const float ip[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE]);

/* Run MCCFR for n_iterations. Samples hands from ranges. */
void preflop_solver_solve(PreflopSolver *ps, int n_iterations);

/* Chunked run with merge once at end. */
void preflop_solver_begin_parallel_solve(PreflopSolver *ps);
void preflop_solver_end_parallel_solve(PreflopSolver *ps);

/* Replay preflop history and fill state. */
void preflop_solver_get_state_at_history(const PreflopSolver *ps, uint64_t history, int num_actions, GameState *out_state);

/* Get strategy for a specific hole hand (bitmask) at history/num_actions. */
int preflop_solver_get_hand_strategy(
	int game_mode, uint64_t history, int num_actions, uint64_t hole_hand,
	float probs[PREFLOP_MAX_ACTIONS]
);

/* Get strategy at history for (row,col) for whoever acts at that node. */
int preflop_solver_get_strategy_at_history(const PreflopSolver *ps, uint64_t history, int num_actions,
	int row, int col, float probs[PREFLOP_MAX_ACTIONS], int *num_actions_out);

/* Export the solved range at a given history node to JSON.
 * The output grid[r][c] = 1 - fold_frequency for the acting player.
 * Returns 0 on success, -1 on failure. */
int preflop_solver_export_range_grid(
	const PreflopSolver *ps, uint64_t history, int num_actions,
	float out_grid[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE]);

/* Write range grid as JSON to file. Returns 0 on success. */
int preflop_solver_save_range_json(
	const char *path, const char *name, const char *description,
	const float grid[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE]);

#endif
