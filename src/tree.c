#include "tree.h"
#include "dcfr.h"

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

static uint64_t card_mask_from_index(int card_idx) {
	int rank = card_idx % 13;
	int suit = card_idx / 13;

	return 1ULL << (rank + (suit * 16));
}

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
	int count = 0;

	for (int card = 0; card < 52; card++) {
		if (board & card_mask_from_index(card))
			continue;

		if (dealt_cards)
			dealt_cards[count] = (uint8_t)card;

		count += 1;
	}

	return count;
}

static PublicNode* build_terminal_node(TreeArena* arena, GameState state) {
	return alloc_node(arena, NODE_TERMINAL, state.active_player, 0, 0);
}

static PublicNode* build_tree_recursive(TreeArena* arena, GameState state, int num_combos);

static int count_tree_storage(TreeArena* arena, GameState state, int num_combos) {
	if (state.last_action_was_fold)
		return reserve_node_storage(arena, NODE_TERMINAL, 0, 0);

	if (betting_round_closed(state)) {
		if (state.street >= 2)
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

	int8_t legal_actions[8];
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
		if (state.street >= 2)
			return build_terminal_node(arena, state);

		return build_chance_node(arena, state, num_combos);
	}

	int8_t legal_actions[8];
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

static inline void fastswap(uint32_t* s, int a, int b) {
	uint32_t tmp = s[a] ^ s[b];
	uint32_t msk = (s[a] < s[b]) ? ~0U : 0U;
	s[a] ^= tmp & msk;
	s[b] ^= tmp & msk;
}

static unsigned __int128 get_canonical_hand(uint64_t private_hand, uint64_t board) {
	unsigned __int128 packed_suits = 0;
	//0x1FFF -> map of all suits -> 0001111111111111
	
	//[board_suit4][priv_suit_4] [board_suit3][priv_suit_3] [board_suit2][priv_suit_2] [board_suit1][priv_suit_1]
	//[    13b    ][     13b   ] [     13b   ][    13b    ] [     13b   ][    13b    ] [    13b    ][    13b    ]
	//13*2 = 26 bits for 1 suit board + private, 26*4 packed into 128b
	//s1
	packed_suits |= (unsigned __int128)((board >> 0) & 0x1FFF) << 0;
	packed_suits |= (unsigned __int128)((private_hand >> 0) & 0x1FFF) << 13;
	//s2
	packed_suits |= (unsigned __int128)((board >> 16) & 0x1FFF) << 26;
	packed_suits |= (unsigned __int128)((private_hand >> 16) & 0x1FFF) << 39;
	//s3
	packed_suits |= (unsigned __int128)((board >> 32) & 0x1FFF) << 52;
	packed_suits |= (unsigned __int128)((private_hand >> 32) & 0x1FFF) << 65;
	//s4
	packed_suits |= (unsigned __int128)((board >> 48) & 0x1FFF) << 78;
	packed_suits |= (unsigned __int128)((private_hand >> 48) & 0x1FFF) << 91;

	//0x3FFFFFF - 26b, or board suit + priv suit described above
	uint32_t s[4];
	s[0] = (packed_suits >> 0)  & 0x3FFFFFF;
	s[1] = (packed_suits >> 26) & 0x3FFFFFF;
	s[2] = (packed_suits >> 52) & 0x3FFFFFF;
	s[3] = (packed_suits >> 78) & 0x3FFFFFF;

	fastswap(0, 1);
	fastswap(2, 3); 
	fastswap(0, 2);
	fastswap(1, 3); 
	fastswap(1, 2);

	//pack into canonical 
	unsigned __int128 canonical = 0;
	canonical |= ((unsigned __int128)s[0]) << 0;
	canonical |= ((unsigned __int128)s[1]) << 26;
	canonical |= ((unsigned __int128)s[2]) << 52;
	canonical |= ((unsigned __int128)s[3]) << 78;

	return canonical;
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
