#include "dcfr.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_COMBOS 1326

static void init_uniform_range(uint64_t board, int num_combos, float* range) {
	int live_combos = 0;

	for (int combo = 0; combo < num_combos; combo++) {
		if ((get_mask_for_combo(combo) & board) == 0)
			live_combos += 1;
	}

	if (!live_combos)
		return;

	for (int combo = 0; combo < num_combos; combo++) {
		if ((get_mask_for_combo(combo) & board) == 0)
			range[combo] = 1.0f / (float)live_combos;
		else
			range[combo] = 0.0f;
	}
}

int main(int argc, char **argv) {
	const char* board_str = argc > 1 ? argv[1] : "AsKd4h";
	int iterations = argc > 2 ? atoi(argv[2]) : 1000;
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

	init_uniform_range(board, NUM_COMBOS, p1_range);
	init_uniform_range(board, NUM_COMBOS, p2_range);

	//isomorphism map for the solver
	IsoMap map;
	build_isomap(board, &map);
	/*
	for (int i = 0; i < 1326; i++)
		printf("h%d: %d\n", i, map.combo_to_bucket[i]);
	printf("num_unique_buckets: %d, padd_buckets: %d\n", map.num_unique_buckets, map.padded_buckets);
	*/
	root = build_tree(initial_state, map.num_unique_buckets, &map);
	if (!root) {
		free(p1_range);
		free(p2_range);
		return 1;
	}

	for (int iteration = 1; iteration <= iterations; iteration++)
		train(root, initial_state, NUM_COMBOS, p1_range, p2_range, iteration);

	destroy_tree(root);
	free(p1_range);
	free(p2_range);

	return 0;
}
