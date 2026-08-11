#include "config.h"
#include "dcfr.h"
#include "ranges.h"
#if WALK_TREE
#include "walk_tree.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_COMBOS 1326

static const char* street_label(int max_street) {
	switch (max_street) {
		case 0: return "flop only";
		case 1: return "flop + turn";
		default: return "flop + turn + river";
	}
}

int main(int argc, char **argv) {
	const char* board_str = argc > 1 ? argv[1] : "AsKd4h";
	// DCFR (α=1.5, β=0.5, γ=2) converges faster than vanilla CFR; 200 is
	// usually enough for postflop mixes. Override: ./turbofire_turn AsKd4h 500
	int iterations = argc > 2 ? atoi(argv[2]) : 200;
	uint64_t board;
	GameState initial_state;
	PublicNode* root;
	float* p1_range;
	float* p2_range;

	if (iterations < 1)
		iterations = 1;

	init_evaluator();

	board = parse_board_string(board_str);
	initial_state = (GameState){
		.board = board,
		.pot = 200,
		.p1_stack = 900,
		.p2_stack = 900,
		.p1_commit = 0,
		.p2_commit = 0,
		.active_player = P1,
		.street = 0,
		.raises_this_street = 0,
		.num_actions_this_street = 0,
		.last_action_was_fold = 0,
	};

	p1_range = (float*)calloc(NUM_COMBOS, sizeof(float));
	p2_range = (float*)calloc(NUM_COMBOS, sizeof(float));

	init_bb_range(board, NUM_COMBOS, p1_range);
	init_btn_range(board, NUM_COMBOS, p2_range);

	printf("solving: %s (MAX_STREET=%d)\n", street_label(MAX_STREET), MAX_STREET);
	printf("ranges: P1=BB (OOP), P2=BTN (IP)\n");
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

	for (int iteration = 1; iteration <= iterations; iteration++) {
		printf("training iteration %d/%d\r", iteration, iterations);
		fflush(stdout);
		train(root, initial_state, NUM_COMBOS, p1_range, p2_range, iteration);
	}
	printf("training iteration %d/%d\n", iterations, iterations);

#if WALK_TREE
	printf("emitting soft-label JSONL...\n");
	fflush(stdout);
	walk_tree(root, initial_state, NUM_COMBOS, p1_range, p2_range);
#endif

	destroy_tree(root);
	free(p1_range);
	free(p2_range);

	return 0;
}
