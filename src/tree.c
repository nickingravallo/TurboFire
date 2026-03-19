#include "tree.h"
#include <stdio.h>

void arena_init(Arena* a, size_t size) {
	a->memory = (uint8_t*)malloc(size);
	if (a->memory) {
		printf("cant allocate %zu bytes for arena\n", size);
		exit(1);
	}
	a->capacity = size;
	a->offset = 0;
}

void* arena_alloc(Arena* a, size_t size) {
	size_t aligned_size = (size + 31) & ~31; //force 32b align for simd

	if (a->offset + aligned_size > a->capacity) {
		printf("Arena out of memory! Tree too large\n");
		exit(1);
	}

	void* ptr = a->memory + a->offset;
	a->offset +=aligned_size;
	return ptr;
}

bool is_hand_over(GameState* state);
bool is_street_complete(GameState* state);
int generate_bet_sizes(GameState* state, int* out_action);

PublicNode* build_public_tree(Arena* arena, GameState state, int num_buckets) {
	PublicNode* node = (PublicNode*) arena_alloc(arena, sizeof(PublicNode));
	
	//terminal state (showdown or fold)
	if (is_hand_over(&state)) {
		node->type = NODE_TERMINAL;
		node->num_childern = 0;
		return node;
	}

	//chance state (dealing turn or river)
	if (is_street_complete(&state)) {
		node->type = NODE_CHANCE;
		
		int unique_cards[52];
		float weights[52];

		int num_deals = get_isomorphic_runouts(&state, unique_cards, weights);

		node->num_children = num_deals;
		node->children = (PublicNode**)arena_alloc(arena, num_deals * sizeof(PublicNode*));
		node->dealt_cards = (int*)arena_alloc(arena, num_deals * sizeof(int));

		//recurse to next street
		for (int i = 0; i < num_deals; i++) {
			GameState next_state = apply_deal(state, unique_cards[i]);
			node->dealt_cards[i] = unique_cards[i];
			node->chance_weights[i] = weights[i];
			node->children[i] = build_public_tree(arena, next_state, num_buckets);
		}
		return node;
	}

	// action state (check bet raise fold)
	node->type = NODE_ACTION;
	node->active_player = state.active_player;

	int legal_actions[8];
	int num_actions = generate_bet_sizes(&state, legal_actions);

	node->num_childern = num_actions;
	node->children = (PublicNode**)arena_alloc(arena, num_actions * sizeof(PublicNode*));

	//massive float arrays for dcfr (4 byte aligned for arm neon)
	size_t array_size = num_actions * num_buckets * sizeof(float);
	node->regret_sum = (float*)arena_alloc(arena, array_size);
	node->strategy_sum = (float*)arena_alloc(arena, array_size);

	//initialize to 0
	for (size_t i = 0; i < (num_actions * num_buckets); i++) {
		node->regret_sum[i] = 0.0f;
		node->strategy_sum[i] = 0.0f;
	}

	//reucrse down betting tree
	for (int i = 0; i < num_actions; i++) {
		GameState next_state = apply_bet(state, legal_actions[i]);
		node->children[i] = build_public_tree(arena, next_state, num_buckets);
	}

	return node;
}
