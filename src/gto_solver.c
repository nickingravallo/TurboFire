#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gto_solver.h"
#include "ranks.h"

HashTable gto_table[TABLE_SIZE];

// Initialize hash table with empty magic numbers
void init_gto_table(void) {
	for (int i = 0; i < TABLE_SIZE; i++)
		gto_table[i].key = EMPTY_MAGIC;
}

// Hash function (from murmurhash)
uint64_t hash_key(uint64_t key) {
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdLLU;
	key ^= key >> 33;
	return key % TABLE_SIZE;
}

// Create hash key from game state and player's private cards
uint64_t make_info_set_key(uint64_t history, uint64_t board, uint64_t private_hand) {
	uint64_t key = 0;
	
	// Encode private hand (low 64 bits)
	key |= private_hand;
	// Encode board (next 64 bits, but we'll use a simpler encoding)
	// For now, use a hash of the board combined with history
	key ^= board;
	key ^= history << 32;
	
	return key;
}

// Get or create information set node
InfoSet* gto_get_or_create_node(uint64_t key) {
	uint64_t hash_idx;
	int i;
	unsigned int probes = 0;
	
	hash_idx = hash_key(key) % TABLE_SIZE;
	while (gto_table[hash_idx].key != EMPTY_MAGIC) {
		if (gto_table[hash_idx].key == key)
			return &gto_table[hash_idx].infoSet;
		hash_idx = (hash_idx + 1) % TABLE_SIZE;
		if (++probes >= TABLE_SIZE)
			return NULL;  /* table full; avoid infinite loop */
	}
	
	/* Empty slot found */
	gto_table[hash_idx].key = key;
	for (i = 0; i < MAX_ACTIONS; i++) {
		gto_table[hash_idx].infoSet.regret_sum[i] = 0;
		gto_table[hash_idx].infoSet.strategy_sum[i] = 0;
	}
	gto_table[hash_idx].infoSet.key = key;
	return &gto_table[hash_idx].infoSet;
}

InfoSet* gto_get_node(uint64_t key) {
	uint64_t hash_idx = hash_key(key) % TABLE_SIZE;
	unsigned int probes = 0;
	while (gto_table[hash_idx].key != EMPTY_MAGIC) {
		if (gto_table[hash_idx].key == key)
			return &gto_table[hash_idx].infoSet;
		hash_idx = (hash_idx + 1) % TABLE_SIZE;
		if (++probes >= TABLE_SIZE)
			return NULL;
	}
	return NULL;
}

// Initialize game state
void init_game_state(GameState* state, uint64_t p1_hand, uint64_t p2_hand) {
	memset(state, 0, sizeof(GameState));
	state->p1_hand = p1_hand;
	state->p2_hand = p2_hand;
	state->board = 0;
	state->pot = 1.5f;  // SB + BB
	state->street = STREET_PREFLOP;
	state->active_player = P1;
	state->num_actions_this_street = 0;
	state->num_raises_this_street = 0;
	state->num_actions_total = 0;
	state->last_action = 0;
	state->is_terminal = false;
}

void gto_init_flop_state(GameState* state, uint64_t p1_hand, uint64_t p2_hand, uint64_t board) {
	memset(state, 0, sizeof(GameState));
	state->p1_hand = p1_hand;
	state->p2_hand = p2_hand;
	state->board = board;
	state->pot = 1.5f;
	state->street = STREET_FLOP;
	state->active_player = P1;
	state->num_actions_this_street = 0;
	state->num_raises_this_street = 0;
	state->num_actions_total = 0;
	state->last_action = 0;
	state->is_terminal = false;
}

// Check if game state is terminal
bool is_terminal_state(GameState* state) {
	if (state->is_terminal)
		return true;
	
	// Fold ends the hand
	if (state->last_action == FOLD_MASK)
		return true;
	
	// On river, call/check-check ends the hand
	if (state->street == STREET_RIVER) {
		if (state->last_action == CALL_MASK)
			return true;
		if (state->last_action == CHECK_MASK && state->num_actions_this_street >= 2)
			return true;
	}
	
	return false;
}

// Calculate terminal payouts using hand evaluation
float gto_get_payout(GameState* state, int traverser) {
	int opponent;
	uint64_t traverser_hand, opponent_hand;
	int traverser_strength, opponent_strength;
	
	// Fold case
	if (state->last_action == FOLD_MASK) {
		// If traverser folded, they lose their share of the pot
		return (state->active_player == traverser) ? -(state->pot / 2.0f) : (state->pot / 2.0f);
	}
	
	// Showdown - use evaluate() from ranks.h (expects 2 hole + 5 board = 7 cards)
	if (__builtin_popcount(state->board) != 5) {
		/* Avoid calling evaluate with < 7 cards; ranks.c canonicalize_hand assumes 7. */
		return 0.0f;
	}
	opponent = 1 - traverser;
	traverser_hand = (traverser == P1) ? state->p1_hand : state->p2_hand;
	opponent_hand = (opponent == P1) ? state->p1_hand : state->p2_hand;
	
	traverser_strength = evaluate(traverser_hand, state->board);
	opponent_strength = evaluate(opponent_hand, state->board);
	
	// Higher strength wins (ranks.c returns higher values for better hands)
	if (traverser_strength > opponent_strength)
		return state->pot / 2.0f;
	if (traverser_strength < opponent_strength)
		return -(state->pot / 2.0f);
	
	// Chop pot
	return 0.0f;
}

// Get legal actions at current state
uint8_t gto_get_legal_actions(GameState* state) {
	uint8_t legal_mask = 0;
	bool is_facing_bet;
	
	legal_mask |= CHECK_MASK;  // Can always check/call
	
	is_facing_bet = (state->last_action == RAISE_MASK || state->last_action == BET_MASK);
	if (is_facing_bet)
		legal_mask |= FOLD_MASK;  // Can fold when facing a bet
	
	// Can bet/raise if under raise limit (simplified: max 2 raises per street)
	if (state->num_raises_this_street < 2)
		legal_mask |= RAISE_MASK;
	
	return legal_mask;
}

// Apply action and transition to next state
GameState gto_apply_action(GameState state, int action_id) {
	int previous_action;
	float bet_amount;
	
	previous_action = state.last_action;
	state.last_action = (1 << action_id);
	state.history |= (state.last_action << (state.num_actions_total * 3));
	state.num_actions_this_street++;
	state.num_actions_total++;
	
	if (action_id == 0) {  // Fold
		state.is_terminal = true;
		return state;
	}
	else if (action_id == 1) {  // Check or Call
		bool is_facing_bet = (previous_action == RAISE_MASK || previous_action == BET_MASK);
		
		if (is_facing_bet) {  // Call
			bet_amount = (state.street == STREET_PREFLOP) ? 0.5f : 1.0f;  // Call amount
			state.pot += bet_amount;
			
			// Advance street if calling on preflop/flop/turn
			if (state.street < STREET_RIVER) {
				state.street++;
				state.num_actions_this_street = 0;
				state.num_raises_this_street = 0;
				state.active_player = P1;
				state.last_action = 0;
			}
			else {
				// River call ends the hand
				state.is_terminal = true;
			}
		}
		else {  // Check
			if (state.num_actions_this_street >= 2) {
				// Both players checked - advance street or end hand
				if (state.street < STREET_RIVER) {
					state.street++;
					state.num_actions_this_street = 0;
					state.num_raises_this_street = 0;
					state.active_player = P1;
					state.last_action = 0;
				}
				else {
					// River check-check ends the hand
					state.is_terminal = true;
				}
			}
			else {
				// Switch active player
				state.active_player = 1 - state.active_player;
			}
		}
	}
	else if (action_id == 2) {  // Bet or Raise
		bet_amount = (state.street == STREET_PREFLOP) ? 2.0f : 1.0f;  // Bet size
		bool is_facing_bet = (previous_action == RAISE_MASK || previous_action == BET_MASK);
		
		if (is_facing_bet)
			state.pot += bet_amount * 2;  // Raise: match + raise
		else
			state.pot += bet_amount;  // Bet
		
		state.num_raises_this_street++;
		state.active_player = 1 - state.active_player;
	}
	
	return state;
}

// Calculate strategy from regret sums (regret matching)
void gto_get_strategy(float* regret, float* out_strategy, uint8_t legal_actions) {
	int i;
	uint8_t num_legal_actions;
	float normalized_sum;
	
	num_legal_actions = __builtin_popcount(legal_actions);
	
	normalized_sum = 0;
	for (i = 0; i < MAX_ACTIONS; i++) {
		if (legal_actions & (1 << i)) {
			out_strategy[i] = (regret[i] <= 0) ? 0 : regret[i];
			normalized_sum += out_strategy[i];
		}
		else {
			out_strategy[i] = 0.0f;
		}
	}
	
	for (i = 0; i < MAX_ACTIONS; i++) {
		if (legal_actions & (1 << i)) {
			if (normalized_sum > 0)
				out_strategy[i] = out_strategy[i] / normalized_sum;
			else
				out_strategy[i] = 1.0f / (float)num_legal_actions;
		}
	}
}

// Sample action from strategy
int gto_get_action(float* strategy, uint8_t legal_actions) {
	float r;
	float cumulative_strat;
	int a, last_legal_action;
	
	last_legal_action = 0;
	cumulative_strat = 0;
	r = (float)rand() / (float)RAND_MAX;
	
	for (a = 0; a < MAX_ACTIONS; a++) {
		if (legal_actions & (1 << a)) {
			last_legal_action = a;
			cumulative_strat += strategy[a];
			
			if (r < cumulative_strat)
				return a;
		}
	}
	
	return last_legal_action;
}

// Combine hole cards and board
uint64_t combine_cards(uint64_t hand, uint64_t board) {
	return hand | board;
}

// Helper: Create card bitmask from rank (0-12) and suit (0-3)
// Compatible with ranks.c representation: suit * 16 + rank
static uint64_t make_card(int rank, int suit) {
	return 1ULL << (rank + suit * 16);
}

// Deal a random board card that doesn't conflict with hole cards or existing board
uint64_t deal_board_card(GameState state) {
	uint64_t used_cards;
	uint64_t available_cards[52];
	int num_available = 0;
	int rank, suit;
	
	// Collect all used cards
	used_cards = state.p1_hand | state.p2_hand | state.board;
	
	// Find all available cards (not in used_cards)
	// Standard deck: 13 ranks (2-A) x 4 suits
	for (rank = 0; rank < 13; rank++) {
		for (suit = 0; suit < 4; suit++) {
			uint64_t card_mask = make_card(rank, suit);
			if (!(used_cards & card_mask)) {
				available_cards[num_available++] = card_mask;
			}
		}
	}
	
	if (num_available == 0)
		return 0;  // No cards available (shouldn't happen)
	
	// Return random available card
	return available_cards[rand() % num_available];
}

// Main MCCFR traversal function
float gto_mccfr(GameState state, int traverser) {
	float strategy[MAX_ACTIONS];
	float action_values[MAX_ACTIONS];
	float node_value;
	InfoSet* node;
	uint8_t legal_actions;
	uint64_t active_hand;
	int a;
	
	/* Avoid use of uninitialized action_values */
	for (a = 0; a < MAX_ACTIONS; a++)
		action_values[a] = 0.0f;
	
	// Terminal node - return payout
	if (is_terminal_state(&state))
		return gto_get_payout(&state, traverser);
	
	// Chance node - deal board cards
	int cards_needed = 0;
	if (state.street == STREET_FLOP && __builtin_popcount(state.board) == 0)
		cards_needed = 3;  // Need flop
	else if (state.street == STREET_TURN && __builtin_popcount(state.board) == 3)
		cards_needed = 1;  // Need turn
	else if (state.street == STREET_RIVER && __builtin_popcount(state.board) == 4)
		cards_needed = 1;  // Need river
	
	if (cards_needed > 0) {
		GameState next_state = state;
		for (int i = 0; i < cards_needed; i++) {
			uint64_t new_card = deal_board_card(next_state);
			next_state.board |= new_card;
		}
		next_state.num_actions_this_street = 0;
		next_state.num_raises_this_street = 0;
		return gto_mccfr(next_state, traverser);
	}
	
	// Player node - get information set
	active_hand = (state.active_player == P1) ? state.p1_hand : state.p2_hand;
	node = gto_get_or_create_node(make_info_set_key(state.history, state.board, active_hand));
	if (!node)
		return 0.0f;  /* table full */
	legal_actions = gto_get_legal_actions(&state);
	gto_get_strategy(node->regret_sum, strategy, legal_actions);
	node_value = 0;
	
	if (state.active_player == traverser) {
		// Traverser node - evaluate all actions
		for (a = 0; a < MAX_ACTIONS; a++) {
			if (legal_actions & (1 << a)) {
				GameState next_state = gto_apply_action(state, a);
				action_values[a] = gto_mccfr(next_state, traverser);
				node_value += strategy[a] * action_values[a];
			}
		}
		
		// Update regrets
		for (a = 0; a < MAX_ACTIONS; a++) {
			if (legal_actions & (1 << a)) {
				float regret = action_values[a] - node_value;
				node->regret_sum[a] += regret;
			}
		}
	}
	else {
		// Opponent node - sample one action
		int sampled_action = gto_get_action(strategy, legal_actions);
		GameState next_state = gto_apply_action(state, sampled_action);
		
		node_value = gto_mccfr(next_state, traverser);
		
		// Update strategy sum
		for (int a = 0; a < MAX_ACTIONS; a++) {
			if (legal_actions & (1 << a))
				node->strategy_sum[a] += strategy[a];
		}
	}
	
	return node_value;
}

// Get average strategy from accumulated strategy sums
void gto_get_average_strategy(const InfoSet* node, float* out_probs) {
	float sum = 0;
	
	for (int a = 0; a < MAX_ACTIONS; a++) {
		float s = node->strategy_sum[a];
		out_probs[a] = (s > 0) ? s : 0;
		sum += out_probs[a];
	}
	
	if (sum > 0) {
		for (int a = 0; a < MAX_ACTIONS; a++)
			out_probs[a] /= sum;
	}
	else {
		for (int a = 0; a < MAX_ACTIONS; a++)
			out_probs[a] = 1.0f / MAX_ACTIONS;
	}
}
