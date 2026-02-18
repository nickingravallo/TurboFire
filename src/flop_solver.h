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
	uint64_t board;           /* 3 cards for flop */
	int solved;               /* 1 after solve() */
	int iterations_done;
} FlopSolver;

/* Set board (3 cards bitmask). Clears solved state. */
void flop_solver_set_board(FlopSolver *fs, uint64_t board);

/* Set OOP and IP range grids (weights 0..1). Clears solved state. */
void flop_solver_set_ranges(FlopSolver *fs,
	const float oop[FLOP_GRID_SIZE][FLOP_GRID_SIZE],
	const float ip[FLOP_GRID_SIZE][FLOP_GRID_SIZE]);

/* Run MCCFR for n_iterations. Samples hands from ranges (no board overlap). */
void flop_solver_solve(FlopSolver *fs, int n_iterations);

/* Get OOP strategy at start of flop (history=0) for hand (row,col). Fills probs[0..5] = Check, Bet33..Bet123. Return 0 if ok. */
int flop_solver_get_oop_strategy(const FlopSolver *fs, int row, int col, float probs[FLOP_MAX_ACTIONS]);

/* Get strategy for a specific hole hand (bitmask) at history and board. probs[] has FLOP_MAX_ACTIONS; only first N valid per node. */
int flop_solver_get_hand_strategy(uint64_t history, uint64_t board, uint64_t hole_hand, float probs[FLOP_MAX_ACTIONS]);

/* Replay flop history and fill state. Use to get active_player and legal_actions at a node. */
void flop_solver_get_state_at_history(const FlopSolver *fs, uint64_t history, int num_actions, GameState *out_state);

/* Get strategy at history for (row,col) for whoever acts at that node. Fills probs[], sets *num_actions to 6 or 5. Return 0 if ok. */
int flop_solver_get_strategy_at_history(const FlopSolver *fs, uint64_t history, int num_actions,
	int row, int col, float probs[FLOP_MAX_ACTIONS], int *num_actions_out);

#endif
