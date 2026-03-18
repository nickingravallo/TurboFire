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


	}
}
