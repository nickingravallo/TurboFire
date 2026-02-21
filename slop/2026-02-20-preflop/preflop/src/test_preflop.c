/*
 * Headless test for preflop solver: run a small number of iterations,
 * print SB root strategy for a few hands to verify convergence.
 */
#include "../include/preflop_solver.h"
#include "../include/gto_solver.h"
#include "../include/range.h"
#include "../include/ranks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void) {
	PreflopSolver ps;
	float probs[PREFLOP_MAX_ACTIONS];
	int n_actions;
	char hand_str[8];
	
	memset(&ps, 0, sizeof(ps));
	
	/* 100% ranges for both players */
	for (int r = 0; r < PREFLOP_GRID_SIZE; r++)
		for (int c = 0; c < PREFLOP_GRID_SIZE; c++) {
			ps.oop_weights[r][c] = 1.0f;
			ps.ip_weights[r][c] = 1.0f;
		}
	
	init_rank_map();
	init_flush_map();
	
	printf("Running preflop solver (100k iterations, single thread)...\n");
	fflush(stdout);

	/* Force single thread for deterministic test */
	setenv("TURBOFIRE_NTHREADS", "1", 1);

	preflop_solver_solve(&ps, 100000);
	
	printf("Iterations done: %d\n", ps.iterations_done);
	printf("Solved: %d\n\n", ps.solved);
	
	/* Print SB (P1) root strategy for a selection of hands */
	/* At root: SB to act, facing BB (to_call=0.5), so actions are Fold/Call/Raise */
	printf("SB Root Strategy (Fold / Call / R100%% / R200%% / R500%%):\n");
	printf("%-5s  %7s %7s %7s %7s %7s\n", "Hand", "Fold", "Call", "R100%", "R200%", "R500%");
	printf("------------------------------------------------------\n");
	
	/* Check a variety of hands */
	int test_cells[][2] = {
		{0, 0},   /* AA */
		{0, 1},   /* AKs */
		{1, 0},   /* AKo */
		{1, 1},   /* KK */
		{2, 2},   /* QQ */
		{0, 5},   /* A8s */
		{5, 0},   /* A8o */
		{6, 6},   /* 77 */
		{0, 12},  /* A2s */
		{12, 0},  /* A2o */
		{10, 11}, /* 43o */
		{12, 12}, /* 22 */
		{11, 12}, /* 32s */
		{12, 11}, /* 32o */
	};
	int n_tests = sizeof(test_cells) / sizeof(test_cells[0]);
	
	for (int i = 0; i < n_tests; i++) {
		int r = test_cells[i][0];
		int c = test_cells[i][1];
		hand_at(r, c, hand_str, sizeof(hand_str));
		if (preflop_solver_get_strategy_at_history(&ps, 0, 0, r, c, probs, &n_actions) == 0) {
			printf("%-5s  %6.1f%% %6.1f%% %6.1f%% %6.1f%% %6.1f%%\n",
				hand_str,
				probs[0] * 100.0f,
				probs[1] * 100.0f,
				probs[2] * 100.0f,
				probs[3] * 100.0f,
				probs[4] * 100.0f);
		} else {
			printf("%-5s  (no data)\n", hand_str);
		}
	}
	
	printf("\nTest complete.\n");
	
	/* Also verify BB strategy after SB limp (Call action=1 at root) */
	printf("\n--- BB Strategy after SB Limp (history=1, action=Check/B75%%/B150%%/B300%%) ---\n");
	printf("%-5s  %7s %7s %7s %7s\n", "Hand", "Check", "B 75%", "B 150%", "B 300%");
	printf("----------------------------------------------\n");
	
	/* SB Call is action_id=1. History encodes it in bits 0-2. */
	uint64_t sb_limp_history = 1;
	int sb_limp_num_actions = 1;
	
	int bb_test_cells[][2] = {
		{0, 0},   /* AA */
		{1, 1},   /* KK */
		{0, 1},   /* AKs */
		{6, 6},   /* 77 */
		{12, 12}, /* 22 */
		{12, 11}, /* 32o */
	};
	int n_bb_tests = sizeof(bb_test_cells) / sizeof(bb_test_cells[0]);
	
	for (int i = 0; i < n_bb_tests; i++) {
		int r = bb_test_cells[i][0];
		int c = bb_test_cells[i][1];
		hand_at(r, c, hand_str, sizeof(hand_str));
		if (preflop_solver_get_strategy_at_history(&ps, sb_limp_history, sb_limp_num_actions, r, c, probs, &n_actions) == 0) {
			printf("%-5s  %6.1f%% %6.1f%% %6.1f%% %6.1f%%\n",
				hand_str,
				probs[0] * 100.0f,
				probs[1] * 100.0f,
				probs[2] * 100.0f,
				probs[3] * 100.0f);
		} else {
			printf("%-5s  (no data)\n", hand_str);
		}
	}
	
	printf("\nHU tests complete.\n");

	/* Export SB opening range to JSON */
	{
		float sb_range[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE];
		if (preflop_solver_export_range_grid(&ps, 0, 0, sb_range) == 0) {
			if (preflop_solver_save_range_json("output/sb_open.json", "sb_open", "SB opening range (HU)", sb_range) == 0) {
				printf("\nExported SB opening range to output/sb_open.json\n");
			} else {
				printf("\nFailed to write sb_open.json\n");
			}
		} else {
			printf("\nFailed to export SB range\n");
		}
	}

	/* Export BB defense range vs SB 2.5bb open (action_id=2, R100%) */
	{
		uint64_t sb_raise_history = 2;
		float bb_range[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE];
		if (preflop_solver_export_range_grid(&ps, sb_raise_history, 1, bb_range) == 0) {
			if (preflop_solver_save_range_json("output/bb_vs_25x.json", "bb_vs_25x", "BB defense vs SB 2.5x open (HU)", bb_range) == 0) {
				printf("Exported BB defense range to output/bb_vs_25x.json\n");
			} else {
				printf("Failed to write bb_vs_25x.json\n");
			}
		} else {
			printf("Failed to export BB range (may need more iterations)\n");
		}
	}

	/* ================================================================
	 * BTN vs SB test (100bb, BB dead money)
	 * ================================================================ */
	printf("\n========================================================\n");
	printf("BTN vs SB RFI test (PREFLOP_MODE_BTN_SB, 500k iters)\n");
	printf("========================================================\n");

	PreflopSolver ps2;
	memset(&ps2, 0, sizeof(ps2));
	ps2.game_mode = PREFLOP_MODE_BTN_SB;
	for (int r = 0; r < PREFLOP_GRID_SIZE; r++)
		for (int c = 0; c < PREFLOP_GRID_SIZE; c++) {
			ps2.oop_weights[r][c] = 1.0f;
			ps2.ip_weights[r][c] = 1.0f;
		}

	printf("Running BTN vs SB solver (500k iterations)...\n");
	fflush(stdout);
	preflop_solver_solve(&ps2, 500000);
	printf("Iterations done: %d\n", ps2.iterations_done);
	printf("Solved: %d\n\n", ps2.solved);

	/* BTN (P1) root strategy: Fold/Call/R100%/R200%/R500% */
	printf("BTN Root Strategy (Fold / Call / R100%% / R200%% / R500%%):\n");
	printf("%-5s  %7s %7s %7s %7s %7s\n", "Hand", "Fold", "Call", "R100%", "R200%", "R500%");
	printf("------------------------------------------------------\n");

	float btn_raise_total = 0.0f, btn_total_weight = 0.0f;
	for (int r = 0; r < PREFLOP_GRID_SIZE; r++) {
		for (int c = 0; c < PREFLOP_GRID_SIZE; c++) {
			hand_at(r, c, hand_str, sizeof(hand_str));
			if (preflop_solver_get_strategy_at_history(&ps2, 0, 0, r, c, probs, &n_actions) == 0) {
				float raise_freq = 0.0f;
				for (int a = 2; a < n_actions; a++)
					raise_freq += probs[a];
				float combos = (r == c) ? 6.0f : (r < c) ? 4.0f : 12.0f;
				btn_raise_total += raise_freq * combos;
				btn_total_weight += combos;
			}
		}
	}

	for (int i = 0; i < n_tests; i++) {
		int r = test_cells[i][0];
		int c = test_cells[i][1];
		hand_at(r, c, hand_str, sizeof(hand_str));
		if (preflop_solver_get_strategy_at_history(&ps2, 0, 0, r, c, probs, &n_actions) == 0) {
			printf("%-5s  %6.1f%% %6.1f%% %6.1f%% %6.1f%% %6.1f%%\n",
				hand_str,
				probs[0] * 100.0f,
				probs[1] * 100.0f,
				probs[2] * 100.0f,
				probs[3] * 100.0f,
				probs[4] * 100.0f);
		} else {
			printf("%-5s  (no data)\n", hand_str);
		}
	}

	float btn_rfi_pct = (btn_total_weight > 0.0f)
		? (btn_raise_total / btn_total_weight) * 100.0f : 0.0f;
	printf("\nBTN RFI frequency: %.1f%%\n", btn_rfi_pct);
	printf("(Expected: ~45-55%%)\n");

	/* SB (P2) strategy vs BTN open (action_id=2, R100% pot) */
	uint64_t btn_open_history = 2;
	printf("\nSB vs BTN Open (Fold / Call / 3b100%% / 3b200%% / 3b500%%):\n");
	printf("%-5s  %7s %7s %7s %7s %7s\n", "Hand", "Fold", "Call", "3b100%", "3b200%", "3b500%");
	printf("------------------------------------------------------\n");

	float sb_3bet_total = 0.0f, sb_call_total = 0.0f, sb_fold_total = 0.0f;
	float sb_total_weight = 0.0f;
	for (int r = 0; r < PREFLOP_GRID_SIZE; r++) {
		for (int c = 0; c < PREFLOP_GRID_SIZE; c++) {
			if (preflop_solver_get_strategy_at_history(&ps2, btn_open_history, 1, r, c, probs, &n_actions) == 0) {
				float combos = (r == c) ? 6.0f : (r < c) ? 4.0f : 12.0f;
				sb_fold_total += probs[0] * combos;
				sb_call_total += probs[1] * combos;
				float raise_freq = 0.0f;
				for (int a = 2; a < n_actions; a++)
					raise_freq += probs[a];
				sb_3bet_total += raise_freq * combos;
				sb_total_weight += combos;
			}
		}
	}

	for (int i = 0; i < n_tests; i++) {
		int r = test_cells[i][0];
		int c = test_cells[i][1];
		hand_at(r, c, hand_str, sizeof(hand_str));
		if (preflop_solver_get_strategy_at_history(&ps2, btn_open_history, 1, r, c, probs, &n_actions) == 0) {
			printf("%-5s  %6.1f%% %6.1f%% %6.1f%% %6.1f%% %6.1f%%\n",
				hand_str,
				probs[0] * 100.0f,
				probs[1] * 100.0f,
				probs[2] * 100.0f,
				probs[3] * 100.0f,
				probs[4] * 100.0f);
		} else {
			printf("%-5s  (no data)\n", hand_str);
		}
	}

	if (sb_total_weight > 0.0f) {
		printf("\nSB vs BTN open frequencies:\n");
		printf("  Fold:  %.1f%% (expected ~35-45%%)\n", (sb_fold_total / sb_total_weight) * 100.0f);
		printf("  Call:  %.1f%% (expected ~8-15%%)\n", (sb_call_total / sb_total_weight) * 100.0f);
		printf("  3-bet: %.1f%% (expected ~12-18%%)\n", (sb_3bet_total / sb_total_weight) * 100.0f);
	}

	/* Export BTN RFI and SB defense ranges */
	{
		float btn_range[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE];
		if (preflop_solver_export_range_grid(&ps2, 0, 0, btn_range) == 0) {
			if (preflop_solver_save_range_json("output/btn_rfi.json", "btn_rfi", "BTN RFI range (vs SB, 100bb)", btn_range) == 0)
				printf("\nExported BTN RFI range to output/btn_rfi.json\n");
		}
	}
	{
		float sb_def_range[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE];
		if (preflop_solver_export_range_grid(&ps2, btn_open_history, 1, sb_def_range) == 0) {
			if (preflop_solver_save_range_json("output/sb_vs_btn.json", "sb_vs_btn", "SB defense vs BTN open (100bb)", sb_def_range) == 0)
				printf("Exported SB defense range to output/sb_vs_btn.json\n");
		}
	}

	printf("\nAll tests complete.\n");
	return 0;
}
