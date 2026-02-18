#ifndef FLOP_SOLVER_H
#define FLOP_SOLVER_H

#include <stdint.h>
#include <stdbool.h>
#include "gto_solver.h"

#define FLOP_GRID_SIZE 13
#define FLOP_MAX_ACTIONS 8  /* OOP: 6 actions; IP facing bet: 5 actions. Only first N used per node. */

/* OOP = P1 (acts first on flop), IP = P2. */

typedef struct {
	float oop_weights[FLOP_GRID_SIZE][FLOP_GRID_SIZE];
	float ip_weights[FLOP_GRID_SIZE][FLOP_GRID_SIZE];
	uint64_t board;           /* Visible flop cards */
	uint64_t preset_turn_card;  /* Optional fixed turn card */
	uint64_t preset_river_card; /* Optional fixed river card */
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
	/* Optional: called after workers join, before merging into global table (e.g. to show "Merging...") */
	void (*before_merge_cb)(void *user);
	void *before_merge_user;
	/* Merge-once-at-end: set by begin_parallel_solve, cleared by end_parallel_solve */
	void *parallel_thread_tables;  /* HashTable * per thread, only when parallel_accumulate */
	int parallel_nthreads;
	int parallel_accumulate;
} FlopSolver;

/* Set visible flop board and optional fixed turn/river cards. Clears solved state. */
void flop_solver_set_board_runout(
	FlopSolver *fs, uint64_t flop_board, uint64_t preset_turn_card, uint64_t preset_river_card
);

/* Compatibility: set only visible flop board. */
void flop_solver_set_board(FlopSolver *fs, uint64_t board);

/* Set OOP and IP range grids (weights 0..1). Clears solved state. */
void flop_solver_set_ranges(FlopSolver *fs,
	const float oop[FLOP_GRID_SIZE][FLOP_GRID_SIZE],
	const float ip[FLOP_GRID_SIZE][FLOP_GRID_SIZE]);

/* Run MCCFR for n_iterations. Samples hands from ranges (no board overlap). */
void flop_solver_solve(FlopSolver *fs, int n_iterations);

/* Chunked run with merge once at end: call begin, then solve(fs,n) in a loop, then end.
 * Avoids repeated merge into the (full) global table; one merge at end is much faster. */
void flop_solver_begin_parallel_solve(FlopSolver *fs);
void flop_solver_end_parallel_solve(FlopSolver *fs);

/* Get OOP strategy at start of flop (history=0) for hand (row,col). Fills probs[0..5] = Check, Bet33..Bet123. Return 0 if ok. */
int flop_solver_get_oop_strategy(const FlopSolver *fs, int row, int col, float probs[FLOP_MAX_ACTIONS]);

/* Get strategy for a specific hole hand (bitmask) at history/num_actions and board. */
int flop_solver_get_hand_strategy(
	uint64_t history, uint64_t board, int num_actions, uint64_t hole_hand,
	float probs[FLOP_MAX_ACTIONS]
);
int flop_solver_get_hand_strategy_with_runout(
	uint64_t history, uint64_t flop_board, uint64_t preset_turn_card, uint64_t preset_river_card,
	int num_actions, uint64_t hole_hand, float probs[FLOP_MAX_ACTIONS]
);

/* Replay postflop history and fill state. Use to get active_player and legal_actions at a node. */
void flop_solver_get_state_at_history(const FlopSolver *fs, uint64_t history, int num_actions, GameState *out_state);

/* Get strategy at history for (row,col) for whoever acts at that node. Fills probs[], sets *num_actions to 6 or 5. Return 0 if ok. */
int flop_solver_get_strategy_at_history(const FlopSolver *fs, uint64_t history, int num_actions,
	int row, int col, float probs[FLOP_MAX_ACTIONS], int *num_actions_out);

#endif
