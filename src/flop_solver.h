#ifndef FLOP_SOLVER_H
#define FLOP_SOLVER_H

#include <stdint.h>
#include <stdbool.h>

#define FLOP_GRID_SIZE 13
#define FLOP_MAX_ACTIONS 3

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

/* Get OOP strategy at start of flop for hand (row,col). Fills probs[0]=Fold, [1]=Call, [2]=Raise. Return 0 if not in range or no data. */
int flop_solver_get_oop_strategy(const FlopSolver *fs, int row, int col, float probs[FLOP_MAX_ACTIONS]);

/* Get strategy for a specific hole hand (bitmask) at history and board. Used for action breakdown. */
int flop_solver_get_hand_strategy(uint64_t history, uint64_t board, uint64_t hole_hand, float probs[FLOP_MAX_ACTIONS]);

#endif
