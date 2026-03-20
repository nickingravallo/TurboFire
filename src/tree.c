#include "tree.h"
#include <stdbool.h>
#include <stdio.h>

void arena_init(Arena* a, size_t size) {
	a->memory = (uint8_t*)malloc(size);
	if (!a->memory) {
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

bool is_street_complete(GameState* state) {
	if (state->last_action_was_fold == 1)
		return true;

	//allin or no chips left
	if ((state->p1_stack == 0 || state->p2_stack == 0) && (state->p1_commit == state->p2_commit))
		return true;

	//two actions or bet is matched
	if (state->num_actions_this_street >= 2 && state->p1_commit == state->p2_commit)
		return true;

	return false;
}

bool is_hand_over(GameState* state) {
	if (state->last_action_was_fold == 1)
		return true;

	if (state->street == 2 && is_street_complete(state))
		return true;

	return false;
}

int generate_bet_sizes(GameState* state, int* out_actions) {
	int num_actions = 0;
    
	int active = state->active_player;
	int my_commit  = (active == 0) ? state->p1_commit : state->p2_commit;
	int opp_commit = (active == 0) ? state->p2_commit : state->p1_commit;
	int my_stack   = (active == 0) ? state->p1_stack  : state->p2_stack;

	int to_call = opp_commit - my_commit;
	
	// 1 -> fold
	if (to_call > 0) 
        	out_actions[num_actions++] = -1; 

	// 2-> check or call
	if (to_call == 0)
		out_actions[num_actions++] = 0; // Check
	else {
		//cap at stack
		int call_amount = (my_stack < to_call) ? my_stack : to_call;
		out_actions[num_actions++] = call_amount; // Call
	}

	// 3-> bet or raise
	if (state->raises_this_street < 3 && my_stack > to_call) { 
		int current_pot = state->pot + to_call;

		int bet_33  = (current_pot * 33) / 100;
		int bet_52  = (current_pot * 52) / 100;
		int bet_100 = (current_pot);

		int total_raise_33  = to_call + bet_33;
		int total_raise_52  = to_call + bet_52;
		int total_raise_100 = to_call + bet_100;

		if (total_raise_33 > to_call && total_raise_33 < my_stack)
			out_actions[num_actions++] = total_raise_33;
		if (total_raise_52 > to_call && total_raise_52 < my_stack)
			out_actions[num_actions++] = total_raise_52;
		if (total_raise_100 > to_call && total_raise_100 < my_stack)
			out_actions[num_actions++] = total_raise_100;

		out_actions[num_actions++] = my_stack;
	}

	//return the total number of branches generated for this node
	return num_actions; 
}

GameState apply_bet(GameState current_state, int action_amount) {
	GameState next_state = current_state;

	next_state.num_actions_this_street += 1;

	if (action_amount == -1) { //fold
		next_state.last_action_was_fold = 1;
		return next_state;
	}

	//move from stack to committed amount
	if (next_state.active_player == 0) { //p1 oop
		next_state.p1_commit += action_amount;
		next_state.p1_stack -= action_amount;
	}
	else { //p2 ip
		next_state.p2_commit += action_amount;
		next_state.p2_stack  -= action_amount;
	}

	next_state.pot += action_amount;

	//check for raise
	if (next_state.p1_commit != next_state.p2_commit && action_amount > 0)
		next_state.raises_this_street += 1;

	//opponent next turn
	next_state.active_player = 1 - next_state.active_player;

	return next_state; //pass back 
}

GameState apply_deal(GameState current_state, int card_idx) {
	GameState next_state = current_state;

	int rank = card_idx % 13;
	int suit = card_idx / 13;

	uint64_t new_card_mask = 1ULL << (rank + (suit * 16));

	next_state.board |= new_card_mask;

	next_state.street += 1;
	
	next_state.p1_commit = 0;
	next_state.p2_commit = 0;
	next_state.raises_this_street = 0;
	next_state.num_actions_this_street = 0;
	next_state.last_action_was_fold = 0;

	next_state.active_player = 0; //oop always acts first
	
	return next_state;
}

int get_isomorphic_runouts(GameState* state, int* unique_cards, float* weights) {
	uint64_t dead_cards = state->board; //board cards are dead

	int num_unique = 0;

	//get suits on board
	int suit_present[4] = {0};
	for (int suit = 0 ; suit < 4 ; suit++ ) {
		uint64_t suit_mask = 0x1FFFULL << (suit * 16);
		if ((state->board & suit_mask) != 0)
			suit_present[suit] = 1;
	}

	//create mapping for symmetric suits
	//all absent suits are mapped to first absent suit (canonical)
	int canonical_suit[4];
	int first_absent = -1;

	for (int suit = 0; suit < 4; suit++) {
		if (suit_present[suit] == 1)
			canonical_suit[suit] = suit;
		else {
			if (first_absent == -1)
				first_absent = suit;
			canonical_suit[suit] = first_absent;
		}
	}

	//group 52card deck
	int card_weight[52] = {0};

	for (int card_idx = 0; card_idx < 52; card_idx++) {
		int rank = card_idx % 13;
		int suit = card_idx / 13;
		uint64_t card_mask = 1ULL << (rank + (suit * 16));

		if ((dead_cards & card_mask) == 0) {
			int canon_suit = canonical_suit[suit];
			int canon_idx = (canon_suit * 13) + rank;

			card_weight[canon_idx]++;
		}
	}

	//populate the output arrays for tree builder
	for (int card_idx = 0; card_idx < 52; card_idx++) {
		if (card_weight[card_idx] > 0) {
			unique_cards[num_unique] = card_idx;
			weights[num_unique] = (float)card_weight[card_idx];
			num_unique++;
		}
	}

	return num_unique;
}

PublicNode* build_public_tree(Arena* arena, GameState state, int num_buckets) {
	PublicNode* node = (PublicNode*) arena_alloc(arena, sizeof(PublicNode));
	
	//terminal state (showdown or fold)
	if (is_hand_over(&state)) {
		node->type = NODE_TERMINAL;
		node->num_children = 0;
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
		node->chance_weights = (float*)arena_alloc(arena, num_deals * sizeof(float));

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

	node->num_children = num_actions;
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
