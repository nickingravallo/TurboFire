#include "flop_solver.h"
#include "gto_solver.h"
#include "range.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_COMBOS 12

static uint64_t g_flop_board;

/* Sample a hand (c1|c2) from range grid; board = dead cards. Return hand bitmask (c1|c2). */
static uint64_t sample_hand_from_range(const float grid[FLOP_GRID_SIZE][FLOP_GRID_SIZE], uint64_t board_dead) {
	float total = 0.0f;
	float cum[FLOP_GRID_SIZE][FLOP_GRID_SIZE];
	int r, c;
	char hand_str[8];
	for (r = 0; r < FLOP_GRID_SIZE; r++) {
		for (c = 0; c < FLOP_GRID_SIZE; c++) {
			if (grid[r][c] <= 0.0f) { cum[r][c] = total; continue; }
			hand_at(r, c, hand_str, sizeof(hand_str));
			{
				uint64_t d1[MAX_COMBOS], d2[MAX_COMBOS];
				int n = hand_string_to_combos(hand_str, board_dead, d1, d2, MAX_COMBOS);
				if (n <= 0) { cum[r][c] = total; continue; }
				total += grid[r][c] * (float)n;
				cum[r][c] = total;
			}
		}
	}
	if (total <= 0.0f) return 0;
	float u = (float)rand() / (float)RAND_MAX * total;
	for (r = 0; r < FLOP_GRID_SIZE; r++) {
		for (c = 0; c < FLOP_GRID_SIZE; c++) {
			if (u >= cum[r][c]) continue;
			hand_at(r, c, hand_str, sizeof(hand_str));
			uint64_t c1[MAX_COMBOS], c2[MAX_COMBOS];
			int n = hand_string_to_combos(hand_str, board_dead, c1, c2, MAX_COMBOS);
			if (n <= 0) return 0;
			int idx = rand() % n;
			return c1[idx] | c2[idx];
		}
	}
	return 0;
}

void flop_solver_set_board(FlopSolver *fs, uint64_t board) {
	if (!fs) return;
	fs->board = board;
	fs->solved = 0;
	fs->iterations_done = 0;
}

void flop_solver_set_ranges(FlopSolver *fs,
	const float oop[FLOP_GRID_SIZE][FLOP_GRID_SIZE],
	const float ip[FLOP_GRID_SIZE][FLOP_GRID_SIZE]) {
	if (!fs) return;
	memcpy(fs->oop_weights, oop, sizeof(fs->oop_weights));
	memcpy(fs->ip_weights, ip, sizeof(fs->ip_weights));
	fs->solved = 0;
	fs->iterations_done = 0;
}

void flop_solver_solve(FlopSolver *fs, int n_iterations) {
	if (!fs || n_iterations <= 0) return;
	g_flop_board = fs->board;
	init_gto_table();
	for (int iter = 0; iter < n_iterations; iter++) {
		uint64_t p1 = sample_hand_from_range(fs->oop_weights, fs->board);
		uint64_t p2 = sample_hand_from_range(fs->ip_weights, fs->board);
		if (!p1 || !p2 || (p1 & p2)) continue;
		if ((p1 & fs->board) || (p2 & fs->board)) continue;
		GameState state;
		gto_init_flop_state(&state, p1, p2, fs->board);
		gto_mccfr(state, P1);
		gto_init_flop_state(&state, p1, p2, fs->board);
		gto_mccfr(state, P2);
	}
	fs->solved = 1;
	fs->iterations_done = n_iterations;
}

int flop_solver_get_hand_strategy(
	uint64_t history, uint64_t board, int num_actions, uint64_t hole_hand,
	float probs[FLOP_MAX_ACTIONS]
) {
	GameState state;
	uint8_t legal_actions;
	float legal_sum = 0.0f;
	int a;
	gto_replay_flop_history(history, board, num_actions, &state);
	InfoSet *node = gto_get_node(make_info_set_key(
		history, board, hole_hand, state.pot, state.p1_stack, state.p2_stack
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

int flop_solver_get_oop_strategy(const FlopSolver *fs, int row, int col, float probs[FLOP_MAX_ACTIONS]) {
	return flop_solver_get_strategy_at_history(fs, 0, 0, row, col, probs, NULL);
}

void flop_solver_get_state_at_history(const FlopSolver *fs, uint64_t history, int num_actions, GameState *out_state) {
	if (!fs || !out_state) return;
	gto_replay_flop_history(history, fs->board, num_actions, out_state);
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
		if (flop_solver_get_hand_strategy(history, fs->board, num_actions, hand, p) == 0) {
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
