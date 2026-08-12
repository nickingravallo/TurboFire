#include "tree.h"
#include "dcfr.h"
#include "config.h"
#include "isomorphism.h"

#include <stdio.h>
#include <sys/mman.h>

typedef struct {
	uint8_t* base;
	size_t used;
	size_t capacity;
} TreeArena;

static void* mapped_tree_base = NULL;
static size_t mapped_tree_size = 0;
static PublicNode* mapped_tree_root = NULL;

static size_t align_up(size_t value, size_t alignment) {
	size_t mask = alignment - 1;

	return (value + mask) & ~mask;
}

static void* arena_alloc(TreeArena* arena, size_t size, size_t alignment) {
	size_t start;
	size_t end;

	if (!size)
		return NULL;

	start = align_up(arena->used, alignment);
	end = start + size;

	if (end < start || end > arena->capacity)
		return NULL;

	arena->used = end;

	if (!arena->base)
		return (void*)(uintptr_t)(start + 1);

	return arena->base + start;
}

static int reserve_node_storage(TreeArena* arena, NodeType type, uint8_t num_children, int num_combos) {
	if (!arena_alloc(arena, sizeof(PublicNode), alignof(PublicNode)))
		return 0;

	if (num_children && !arena_alloc(arena, num_children * sizeof(PublicNode*), alignof(PublicNode*)))
		return 0;

	if (type == NODE_ACTION && num_children) {
		size_t strategy_bytes = num_children * (size_t)num_combos * sizeof(float);

		if (!arena_alloc(arena, strategy_bytes, alignof(float)))
			return 0;
		if (!arena_alloc(arena, strategy_bytes, alignof(float)))
			return 0;
	}

	if (type == NODE_CHANCE && num_children) {
		if (!arena_alloc(arena, num_children * sizeof(uint8_t), alignof(uint8_t)))
			return 0;
	}

	return 1;
}

static PublicNode* alloc_node(TreeArena* arena, NodeType type, uint8_t active_player, uint8_t num_children, int num_combos) {
	PublicNode* node = (PublicNode*)arena_alloc(arena, sizeof(PublicNode), alignof(PublicNode));

	if (!node)
		return NULL;

	node->type = type;
	node->active_player = active_player;
	node->num_children = num_children;

	if (num_children)
		node->children = (PublicNode**)arena_alloc(arena, num_children * sizeof(PublicNode*), alignof(PublicNode*));

	if (num_children && !node->children)
		return NULL;

	if (type == NODE_ACTION && num_children) {
		size_t strategy_bytes = num_children * (size_t)num_combos * sizeof(float);

		node->regret_sum = (float*)arena_alloc(arena, strategy_bytes, alignof(float));
		node->strategy_sum = (float*)arena_alloc(arena, strategy_bytes, alignof(float));

		if (!node->regret_sum || !node->strategy_sum)
			return NULL;
	}

	if (type == NODE_CHANCE && num_children) {
		node->dealt_cards = (uint8_t*)arena_alloc(arena, num_children * sizeof(uint8_t), alignof(uint8_t));

		if (!node->dealt_cards)
			return NULL;
	}

	return node;
}

static int betting_round_closed(GameState state) {
	if (state.last_action_was_fold)
		return 1;

	if (state.p1_commit != state.p2_commit)
		return 0;

	if (state.p1_stack == 0 || state.p2_stack == 0)
		return 1;

	return state.num_actions_this_street >= 2;
}

static int collect_runout_cards(uint64_t board, uint8_t* dealt_cards) {
	return collect_isomorphic_runout_cards(board, dealt_cards);
}

static PublicNode* build_terminal_node(TreeArena* arena, GameState state) {
	return alloc_node(arena, NODE_TERMINAL, state.active_player, 0, 0);
}

static PublicNode* build_tree_recursive(TreeArena* arena, GameState state, int num_combos);

static int count_tree_storage(TreeArena* arena, GameState state, int num_combos) {
	if (state.last_action_was_fold)
		return reserve_node_storage(arena, NODE_TERMINAL, 0, 0);

	if (betting_round_closed(state)) {
		if (state.street >= MAX_STREET)
			return reserve_node_storage(arena, NODE_TERMINAL, 0, 0);

		uint8_t dealt_cards[52];
		int child_count = collect_runout_cards(state.board, dealt_cards);

		if (!reserve_node_storage(arena, NODE_CHANCE, (uint8_t)child_count, num_combos))
			return 0;

		for (int i = 0; i < child_count; i++) {
			GameState next_state = apply_deal(state, dealt_cards[i]);

			if (!count_tree_storage(arena, next_state, num_combos))
				return 0;
		}

		return 1;
	}

	int8_t legal_actions[MAX_LEGAL_ACTIONS];
	int action_count = get_legal_actions(state, legal_actions);

	if (!reserve_node_storage(arena, NODE_ACTION, (uint8_t)action_count, num_combos))
		return 0;

	for (int i = 0; i < action_count; i++) {
		GameState next_state = apply_bet(state, legal_actions[i]);

		if (!count_tree_storage(arena, next_state, num_combos))
			return 0;
	}

	return 1;
}

static PublicNode* build_chance_node(TreeArena* arena, GameState state, int num_combos) {
	uint8_t dealt_cards[52];
	int child_count = collect_runout_cards(state.board, dealt_cards);

	PublicNode* node = alloc_node(arena, NODE_CHANCE, state.active_player, (uint8_t)child_count, num_combos);
	if (!node)
		return NULL;

	if (!child_count)
		return node;

	for (int i = 0; i < child_count; i++) {
		GameState next_state = apply_deal(state, dealt_cards[i]);

		node->dealt_cards[i] = dealt_cards[i];
		node->children[i] = build_tree_recursive(arena, next_state, num_combos);
		if (!node->children[i])
			return NULL;
	}

	return node;
}

PublicNode* build_tree(GameState state, int num_combos) {
	TreeArena arena = {0};

	if (mapped_tree_base)
		destroy_tree(mapped_tree_root);

	arena.capacity = SIZE_MAX;
	if (!count_tree_storage(&arena, state, num_combos)) {
		printf("ERR: failed to size tree arena\n");
		return NULL;
	}

	mapped_tree_size = arena.used;
	printf("tree arena size: %zu bytes (%.2f MiB)\n",
		mapped_tree_size,
		(double)mapped_tree_size / (1024.0 * 1024.0));
	mapped_tree_base = mmap(NULL, mapped_tree_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapped_tree_base == MAP_FAILED) {
		printf("ERR: mmap failed for tree arena (%zu bytes, %.2f MiB)\n",
			mapped_tree_size,
			(double)mapped_tree_size / (1024.0 * 1024.0));
		mapped_tree_base = NULL;
		mapped_tree_size = 0;
		return NULL;
	}

	arena.base = (uint8_t*)mapped_tree_base;
	arena.used = 0;
	arena.capacity = mapped_tree_size;
	mapped_tree_root = build_tree_recursive(&arena, state, num_combos);

	if (!mapped_tree_root) {
		munmap(mapped_tree_base, mapped_tree_size);
		mapped_tree_base = NULL;
		mapped_tree_size = 0;
		return NULL;
	}

	return mapped_tree_root;
}

static PublicNode* build_tree_recursive(TreeArena* arena, GameState state, int num_combos) {
	if (state.last_action_was_fold)
		return build_terminal_node(arena, state);

	if (betting_round_closed(state)) {
		if (state.street >= MAX_STREET)
			return build_terminal_node(arena, state);

		return build_chance_node(arena, state, num_combos);
	}

	int8_t legal_actions[MAX_LEGAL_ACTIONS];
	int action_count = get_legal_actions(state, legal_actions);
	PublicNode* node = alloc_node(arena, NODE_ACTION, state.active_player, (uint8_t)action_count, num_combos);

	if (!node)
		return NULL;

	for (int i = 0; i < action_count; i++) {
		GameState next_state = apply_bet(state, legal_actions[i]);
		node->children[i] = build_tree_recursive(arena, next_state, num_combos);
		if (!node->children[i])
			return NULL;
	}

	return node;
}

void destroy_tree(PublicNode* node) {
	(void)node;

	if (!mapped_tree_base)
		return;

	munmap(mapped_tree_base, mapped_tree_size);
	mapped_tree_base = NULL;
	mapped_tree_size = 0;
	mapped_tree_root = NULL;
}
