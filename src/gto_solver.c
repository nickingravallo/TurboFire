#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "gto_solver.h"
#include "ranks.h"

/* Thread-local RNG (xorshift64) for solver and sampling. */
static _Thread_local uint64_t gto_rng_state;

void gto_rng_seed(unsigned int seed) {
	gto_rng_state = (uint64_t)seed;
	if (gto_rng_state == 0)
		gto_rng_state = 1;
}

float gto_rng_uniform(void) {
	uint64_t x = gto_rng_state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	gto_rng_state = x;
	return (float)(x >> 11) / (float)(UINT64_C(1) << 53);
}

HashTable gto_table[TABLE_SIZE];

/* When set by a worker thread, node get/create use this; otherwise use gto_table. */
static _Thread_local HashTable *gto_thread_table;

static HashTable *gto_current_table(void) {
	return (gto_thread_table != NULL) ? gto_thread_table : gto_table;
}

void gto_set_thread_table(HashTable *table) {
	gto_thread_table = table;
}

void gto_clear_thread_table(void) {
	gto_thread_table = NULL;
}

void gto_init_table(HashTable *table) {
	int i;
	if (!table) return;
	for (i = 0; i < TABLE_SIZE; i++)
		table[i].key = EMPTY_MAGIC;
}

// Initialize hash table with empty magic numbers (global table only). Also seeds main-thread RNG for standalone use (e.g. tests).
void init_gto_table(void) {
	gto_init_table(gto_table);
	gto_rng_seed(1);
}

// Hash function (from murmurhash)
uint64_t hash_key(uint64_t key) {
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdLLU;
	key ^= key >> 33;
	return key % TABLE_SIZE;
}

// Create hash key from game state and player's private cards
static uint64_t mix64(uint64_t h, uint64_t v) {
	h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
	return h;
}

static float estimate_showdown_utility(const GameState *state, int traverser);

static uint64_t quantize_cents(float value) {
	if (value <= 0.0f) return 0ULL;
	return (uint64_t)(value * 100.0f + 0.5f);
}

static int seen_commit(const uint64_t seen_values[], int seen_count, uint64_t commit_cents) {
	int i;
	for (i = 0; i < seen_count; i++) {
		if (seen_values[i] == commit_cents)
			return 1;
	}
	return 0;
}

uint64_t make_info_set_key(
	uint64_t history, uint64_t board, uint64_t private_hand,
	float pot, float p1_stack, float p2_stack
) {
	uint64_t key = 0xcbf29ce484222325ULL;
	key = mix64(key, private_hand);
	key = mix64(key, board);
	key = mix64(key, history);
	key = mix64(key, quantize_cents(pot));
	key = mix64(key, quantize_cents(p1_stack));
	key = mix64(key, quantize_cents(p2_stack));
	return key;
}

// Get or create information set node (uses thread table if set, else global)
InfoSet* gto_get_or_create_node(uint64_t key) {
	HashTable *tbl = gto_current_table();
	uint64_t hash_idx;
	int i;
	unsigned int probes = 0;
	
	hash_idx = hash_key(key) % TABLE_SIZE;
	while (tbl[hash_idx].key != EMPTY_MAGIC) {
		if (tbl[hash_idx].key == key)
			return &tbl[hash_idx].infoSet;
		hash_idx = (hash_idx + 1) % TABLE_SIZE;
		if (++probes >= TABLE_SIZE)
			return NULL;  /* table full; avoid infinite loop */
	}
	
	/* Empty slot found */
	tbl[hash_idx].key = key;
	for (i = 0; i < MAX_ACTIONS; i++) {
		tbl[hash_idx].infoSet.regret_sum[i] = 0;
		tbl[hash_idx].infoSet.strategy_sum[i] = 0;
	}
	tbl[hash_idx].infoSet.key = key;
	return &tbl[hash_idx].infoSet;
}

InfoSet* gto_get_node(uint64_t key) {
	HashTable *tbl = gto_current_table();
	uint64_t hash_idx = hash_key(key) % TABLE_SIZE;
	unsigned int probes = 0;
	while (tbl[hash_idx].key != EMPTY_MAGIC) {
		if (tbl[hash_idx].key == key)
			return &tbl[hash_idx].infoSet;
		hash_idx = (hash_idx + 1) % TABLE_SIZE;
		if (++probes >= TABLE_SIZE)
			return NULL;
	}
	return NULL;
}

/* Get or create node in a specific table (used during merge). */
static InfoSet* get_or_create_in_table(HashTable *tbl, uint64_t key) {
	uint64_t hash_idx;
	int i;
	unsigned int probes = 0;
	if (!tbl) return NULL;
	hash_idx = hash_key(key) % TABLE_SIZE;
	while (tbl[hash_idx].key != EMPTY_MAGIC) {
		if (tbl[hash_idx].key == key)
			return &tbl[hash_idx].infoSet;
		hash_idx = (hash_idx + 1) % TABLE_SIZE;
		if (++probes >= TABLE_SIZE)
			return NULL;
	}
	tbl[hash_idx].key = key;
	for (i = 0; i < MAX_ACTIONS; i++) {
		tbl[hash_idx].infoSet.regret_sum[i] = 0;
		tbl[hash_idx].infoSet.strategy_sum[i] = 0;
	}
	tbl[hash_idx].infoSet.key = key;
	return &tbl[hash_idx].infoSet;
}

void gto_merge_table_into(HashTable *dst, const HashTable *src) {
	int i, a;
	if (!dst || !src) return;
	for (i = 0; i < TABLE_SIZE; i++) {
		if (src[i].key == EMPTY_MAGIC)
			continue;
		{
			InfoSet *node = get_or_create_in_table(dst, src[i].key);
			if (!node) continue;
			for (a = 0; a < MAX_ACTIONS; a++) {
				node->regret_sum[a] += src[i].infoSet.regret_sum[a];
				node->strategy_sum[a] += src[i].infoSet.strategy_sum[a];
			}
		}
	}
}

// Initialize game state
void init_game_state(GameState* state, uint64_t p1_hand, uint64_t p2_hand) {
	memset(state, 0, sizeof(GameState));
	state->p1_hand = p1_hand;
	state->p2_hand = p2_hand;
	state->board = 0;
	state->pot = 1.5f;  // SB + BB
	state->to_call = 0.0f;
	state->p1_stack = STARTING_STACK_BB;
	state->p2_stack = STARTING_STACK_BB;
	state->p1_contribution = 0.5f;
	state->p2_contribution = 1.0f;
	state->street = STREET_PREFLOP;
	state->active_player = P1;
	state->num_actions_this_street = 0;
	state->num_raises_this_street = 0;
	state->num_actions_total = 0;
	state->last_action = 0;
	state->facing_bet = false;
	state->is_terminal = false;
}

/* Bet fractions (pot): 0=check, 33, 52, 75, 100, 123 */
static const int BET_PCT[] = { 0, 33, 52, 75, 100, 123 };
/* Raise fractions (pot) for IP: 33, 75, 123 */
static const int RAISE_PCT[] = { 33, 75, 123 };
/* Keep tree finite to avoid runaway recursion in deep lines. */
#define MAX_RAISES_PER_STREET 2

void gto_init_postflop_state(
	GameState* state, uint64_t p1_hand, uint64_t p2_hand,
	uint64_t flop_board, uint64_t preset_turn_card, uint64_t preset_river_card
) {
	memset(state, 0, sizeof(GameState));
	state->p1_hand = p1_hand;
	state->p2_hand = p2_hand;
	state->board = flop_board;
	state->preset_turn_card = preset_turn_card;
	state->preset_river_card = preset_river_card;
	state->pot = STARTING_FLOP_POT_BB;
	state->to_call = 0.0f;
	state->p1_stack = STARTING_STACK_BB;
	state->p2_stack = STARTING_STACK_BB;
	state->p1_contribution = STARTING_FLOP_POT_BB * 0.5f;
	state->p2_contribution = STARTING_FLOP_POT_BB * 0.5f;
	state->street = STREET_FLOP;
	state->active_player = P1;
	state->num_actions_this_street = 0;
	state->num_raises_this_street = 0;
	state->num_actions_total = 0;
	state->last_action = 0;
	state->facing_bet = false;
	state->is_terminal = false;
}

void gto_init_flop_state(GameState* state, uint64_t p1_hand, uint64_t p2_hand, uint64_t board) {
	gto_init_postflop_state(state, p1_hand, p2_hand, board, 0, 0);
}

// Check if game state is terminal
bool is_terminal_state(GameState* state) {
	if (state->is_terminal)
		return true;
	return false;
}

// Calculate terminal payouts using hand evaluation
float gto_get_payout(GameState* state, int traverser) {
	float traverser_contribution;
	if (!state)
		return 0.0f;
	traverser_contribution = (traverser == P1) ? state->p1_contribution : state->p2_contribution;
	// Fold case: last_action 0 when facing_bet meant Fold; folder is active_player (they just acted)
	if (state->is_terminal && state->last_action == 0 && state->facing_bet) {
		if (state->active_player == traverser)
			return -traverser_contribution;
		return state->pot - traverser_contribution;
	}

	// Showdown: if turn/river are missing, evaluate exact expected utility over completions.
	return estimate_showdown_utility(state, traverser);
}

void gto_replay_postflop_history(
	uint64_t history, uint64_t flop_board, uint64_t preset_turn_card, uint64_t preset_river_card,
	int num_actions, GameState* out_state
) {
	GameState s;
	int i;
	if (!out_state) return;
	gto_init_postflop_state(&s, 0, 0, flop_board, preset_turn_card, preset_river_card);
	if (num_actions <= 0) {
		*out_state = s;
		return;
	}
	for (i = 0; i < num_actions && !s.is_terminal; i++) {
		int a = (int)((history >> (i * BITS_PER_ACTION)) & 7u);
		s = gto_apply_action(s, a);
	}
	*out_state = s;
}

// Replay flop-only history API retained for compatibility.
void gto_replay_flop_history(uint64_t history, uint64_t board, int num_actions, GameState* out_state) {
	gto_replay_postflop_history(history, board, 0, 0, num_actions, out_state);
}

// Get legal actions at current state (OOP/IP facing check: 6 actions, facing bet: up to 5 actions)
uint8_t gto_get_legal_actions(GameState* state) {
	uint8_t legal_mask = 0;
	int i;
	float actor_stack;
	uint64_t seen_values[6];
	int seen_count = 0;
	if (!state)
		return 0;
	actor_stack = (state->active_player == P1) ? state->p1_stack : state->p2_stack;
	if (state->street < STREET_FLOP || state->street > STREET_RIVER)
		return 0;
	if (state->facing_bet) {
		/* Facing bet: Fold(0), Call(1), optional Raise33/75/123 (2..4). */
		legal_mask |= (1u << 0);
		if (actor_stack > 0.0f)
			legal_mask |= (1u << 1);
		/* Track unique total commits in cents to avoid duplicated all-in branches. */
		{
			float call_commit = (state->to_call < actor_stack) ? state->to_call : actor_stack;
			uint64_t call_cents = quantize_cents(call_commit);
			seen_values[seen_count++] = call_cents;
		}
		if (state->num_raises_this_street < MAX_RAISES_PER_STREET) {
			for (i = 2; i <= 4; i++) {
				float desired_raise = state->pot * (float)RAISE_PCT[i - 2] / 100.0f;
				float total_commit = state->to_call + desired_raise;
				if (total_commit > actor_stack)
					total_commit = actor_stack;
				if (total_commit <= state->to_call)
					continue;
				{
					uint64_t commit_cents = quantize_cents(total_commit);
					if (seen_commit(seen_values, seen_count, commit_cents))
						continue;
					if (seen_count < 6)
						seen_values[seen_count++] = commit_cents;
				}
				legal_mask |= (1u << i);
			}
		}
		return legal_mask;
	}
	/* OOP first node or IP facing check: Check(0), Bet33(1), Bet52(2), Bet75(3), Bet100(4), Bet123(5) */
	legal_mask |= (1u << 0);
	for (i = 1; i <= 5; i++) {
		float desired_bet = state->pot * (float)BET_PCT[i] / 100.0f;
		float commit = (desired_bet < actor_stack) ? desired_bet : actor_stack;
		if (commit <= 0.0f)
			continue;
		{
			uint64_t commit_cents = quantize_cents(commit);
			if (seen_commit(seen_values, seen_count, commit_cents))
				continue;
			if (seen_count < 6)
				seen_values[seen_count++] = commit_cents;
		}
		legal_mask |= (1u << i);
	}
	return legal_mask;
}

static void gto_advance_to_next_street(GameState *state) {
	if (!state) return;
	state->to_call = 0.0f;
	if (state->street < STREET_RIVER) {
		state->street++;
		state->num_actions_this_street = 0;
		state->num_raises_this_street = 0;
		state->active_player = P1;
		state->last_action = 0;
		state->facing_bet = false;
	} else {
		state->is_terminal = true;
	}
}

// Apply action and transition to next state (postflop action set)
GameState gto_apply_action(GameState state, int action_id) {
	float desired_bet, desired_raise;
	float *actor_stack;
	float *actor_contribution;
	float actor_commit;
	float call_commit;
	float previous_to_call;
	/* Prevent undefined shifts in history encoding and bound recursion depth. */
	if (state.num_actions_total >= MAX_HISTORY) {
		state.is_terminal = true;
		return state;
	}
	/* Encode history: store action_id in BITS_PER_ACTION bits */
	state.history |= ((uint64_t)(action_id & 7) << (state.num_actions_total * BITS_PER_ACTION));
	state.last_action = (uint8_t)(action_id & 0xFF);
	state.num_actions_this_street++;
	state.num_actions_total++;
	actor_stack = (state.active_player == P1) ? &state.p1_stack : &state.p2_stack;
	actor_contribution = (state.active_player == P1) ? &state.p1_contribution : &state.p2_contribution;

	if (state.facing_bet) {
		/* IP facing bet: 0=Fold, 1=Call, 2=Raise33, 3=Raise75, 4=Raise123 */
		if (action_id == 0) {
			state.is_terminal = true;
			return state;
		}
		if (action_id == 1) {
			call_commit = (state.to_call < *actor_stack) ? state.to_call : *actor_stack;
			*actor_stack -= call_commit;
			if (*actor_stack < 0.0f) *actor_stack = 0.0f;
			*actor_contribution += call_commit;
			state.pot += call_commit;
			state.to_call = 0.0f;
			gto_advance_to_next_street(&state);
			return state;
		}
		if (action_id >= 2 && action_id <= 4) {
			previous_to_call = state.to_call;
			desired_raise = state.pot * (float)RAISE_PCT[action_id - 2] / 100.0f;
			actor_commit = previous_to_call + desired_raise;
			if (actor_commit > *actor_stack)
				actor_commit = *actor_stack;
			if (actor_commit <= previous_to_call) {
				/* Cannot produce a valid raise; treat as a call-sized commit. */
				call_commit = (previous_to_call < *actor_stack) ? previous_to_call : *actor_stack;
				*actor_stack -= call_commit;
				if (*actor_stack < 0.0f) *actor_stack = 0.0f;
				*actor_contribution += call_commit;
				state.pot += call_commit;
				state.to_call = 0.0f;
				gto_advance_to_next_street(&state);
				return state;
			}
			*actor_stack -= actor_commit;
			if (*actor_stack < 0.0f) *actor_stack = 0.0f;
			*actor_contribution += actor_commit;
			state.pot += actor_commit;
			state.to_call = actor_commit - previous_to_call;
			state.num_raises_this_street++;
			/* After a raise, action passes back to the other player. */
			state.active_player = (uint8_t)(1 - state.active_player);
			state.facing_bet = true;
			return state;
		}
		return state;
	}
	/* OOP or IP facing check: 0=Check, 1..5=Bet33..Bet123 */
	if (action_id == 0) {
		if (state.num_actions_this_street >= 2) {
			gto_advance_to_next_street(&state);
		} else {
			state.active_player = 1 - state.active_player;
			state.facing_bet = false;
		}
		return state;
	}
	if (action_id >= 1 && action_id <= 5) {
		desired_bet = state.pot * (float)BET_PCT[action_id] / 100.0f;
		actor_commit = (desired_bet < *actor_stack) ? desired_bet : *actor_stack;
		if (actor_commit <= 0.0f)
			return state;
		*actor_stack -= actor_commit;
		if (*actor_stack < 0.0f) *actor_stack = 0.0f;
		*actor_contribution += actor_commit;
		state.pot += actor_commit;
		state.to_call = actor_commit;
		state.num_raises_this_street++;
		state.active_player = 1 - state.active_player;
		state.facing_bet = true;
		return state;
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
	
	if (num_legal_actions == 0) {
		for (i = 0; i < MAX_ACTIONS; i++)
			out_strategy[i] = 1.0f / (float)MAX_ACTIONS;
		return;
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
	r = gto_rng_uniform();
	
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

static int build_remaining_cards(uint64_t used_cards, uint64_t out_cards[52]) {
	int n = 0;
	int rank, suit;
	for (rank = 0; rank < 13; rank++) {
		for (suit = 0; suit < 4; suit++) {
			uint64_t card = make_card(rank, suit);
			if ((used_cards & card) == 0)
				out_cards[n++] = card;
		}
	}
	return n;
}

static float showdown_payoff_for_traverser(
	int traverser_strength,
	int opponent_strength,
	float pot,
	float traverser_contribution
) {
	if (traverser_strength > opponent_strength)
		return pot - traverser_contribution;
	if (traverser_strength < opponent_strength)
		return -traverser_contribution;
	return (pot * 0.5f) - traverser_contribution;
}

/* Exact showdown utility from current board to 5 board cards (no rollouts). */
static float estimate_showdown_utility(const GameState *state, int traverser) {
	const int n_board = __builtin_popcountll(state->board);
	const int cards_needed = 5 - n_board;
	const uint64_t traverser_hand = (traverser == P1) ? state->p1_hand : state->p2_hand;
	const uint64_t opponent_hand = (traverser == P1) ? state->p2_hand : state->p1_hand;
	const float traverser_contribution = (traverser == P1) ? state->p1_contribution : state->p2_contribution;
	uint64_t remaining_cards[52];
	uint64_t used_cards;
	float total_utility = 0.0f;
	int remaining_count;
	int outcomes = 0;
	int i, j;

	if (cards_needed < 0)
		return 0.0f;

	if (cards_needed == 0) {
		int traverser_strength = evaluate(traverser_hand, state->board);
		int opponent_strength = evaluate(opponent_hand, state->board);
		return showdown_payoff_for_traverser(
			traverser_strength, opponent_strength, state->pot, traverser_contribution
		);
	}

	used_cards = state->board | state->p1_hand | state->p2_hand;
	remaining_count = build_remaining_cards(used_cards, remaining_cards);
	if (remaining_count <= 0)
		return 0.0f;

	if (cards_needed == 1) {
#ifdef _OPENMP
		#pragma omp parallel for reduction(+:total_utility)
#endif
		for (i = 0; i < remaining_count; i++) {
			uint64_t board_full = state->board | remaining_cards[i];
			int traverser_strength = evaluate(traverser_hand, board_full);
			int opponent_strength = evaluate(opponent_hand, board_full);
			total_utility += showdown_payoff_for_traverser(
				traverser_strength, opponent_strength, state->pot, traverser_contribution
			);
		}
		outcomes = remaining_count;
	} else if (cards_needed == 2) {
#ifdef _OPENMP
		#pragma omp parallel for private(j) reduction(+:total_utility)
#endif
		for (i = 0; i < remaining_count; i++) {
			for (j = i + 1; j < remaining_count; j++) {
				uint64_t board_full = state->board | remaining_cards[i] | remaining_cards[j];
				int traverser_strength = evaluate(traverser_hand, board_full);
				int opponent_strength = evaluate(opponent_hand, board_full);
				total_utility += showdown_payoff_for_traverser(
					traverser_strength, opponent_strength, state->pot, traverser_contribution
				);
			}
		}
		outcomes = remaining_count * (remaining_count - 1) / 2;
	} else {
		/* The current solver only reaches terminal nodes from flop/turn/river states. */
		return 0.0f;
	}

	if (outcomes <= 0)
		return 0.0f;
	return total_utility / (float)outcomes;
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
	{
		int idx = (int)(gto_rng_uniform() * (float)num_available);
		if (idx >= num_available)
			idx = num_available - 1;
		return available_cards[idx];
	}
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
	
	// Chance node - deal missing board cards for current street.
	int cards_needed = 0;
	int n_board = __builtin_popcountll(state.board);
	if (state.street == STREET_FLOP && n_board < 3)
		cards_needed = 3 - n_board;
	else if (state.street == STREET_TURN && n_board < 4)
		cards_needed = 4 - n_board;
	else if (state.street == STREET_RIVER && n_board < 5)
		cards_needed = 5 - n_board;
	
	if (cards_needed > 0) {
		GameState next_state = state;
		for (int i = 0; i < cards_needed; i++) {
			uint64_t new_card = 0;
			if (next_state.street == STREET_TURN &&
				__builtin_popcountll(next_state.board) == 3 &&
				next_state.preset_turn_card &&
				((next_state.board | next_state.p1_hand | next_state.p2_hand) & next_state.preset_turn_card) == 0) {
				new_card = next_state.preset_turn_card;
			} else if (next_state.street == STREET_RIVER &&
				__builtin_popcountll(next_state.board) == 4 &&
				next_state.preset_river_card &&
				((next_state.board | next_state.p1_hand | next_state.p2_hand) & next_state.preset_river_card) == 0) {
				new_card = next_state.preset_river_card;
			} else {
				new_card = deal_board_card(next_state);
			}
			if (!new_card) {
				next_state.is_terminal = true;
				break;
			}
			next_state.board |= new_card;
		}
		return gto_mccfr(next_state, traverser);
	}
	
	// Player node - get information set
	active_hand = (state.active_player == P1) ? state.p1_hand : state.p2_hand;
	node = gto_get_or_create_node(make_info_set_key(
		state.history, state.board, active_hand, state.pot, state.p1_stack, state.p2_stack
	));
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
