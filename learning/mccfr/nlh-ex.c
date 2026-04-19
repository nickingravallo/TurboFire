/*
 * nlh-ex.c — Single-file, non-threaded No-Limit Hold'em GTO solver (MCCFR)
 *
 * Simplified from src/gto_solver.c for learning: no threading, no merge,
 * no table saturation, no legacy flags. Core algorithm only.
 *
 * Build: make mccfr  (produces output/mccfr/nlh-ex), or:
 *   gcc -o nlh-ex mccfr/nlh-ex.c src/ranks.c -I src -O2
 *
 * Requires: src/ranks.c and src/ranks.h (init_flush_map, init_rank_map, evaluate).
 * For a quicker run, reduce 'iterations' in main() (e.g. 5000).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

/* Hand evaluation: init once, then evaluate(hand_bitmask, board_bitmask). */
#include "ranks.h"

/* -------------------------------------------------------------------------
 * Constants (match src/gto_solver.h; reduced table for learning)
 * ------------------------------------------------------------------------- */
#define MAX_ACTIONS       8
#define BITS_PER_ACTION   3
#define MAX_HISTORY        100
#define TABLE_SIZE         2000003
#define EMPTY_MAGIC        0xBEEFBEEF

#define STARTING_FLOP_POT_BB  6.0f
#define STARTING_STACK_BB      97.0f

/* OOP / IP facing check: 0=Check, 1=Bet33, 2=Bet52, 3=Bet100 */
/* IP facing bet: 0=Fold, 1=Call, 2=Raise33, 3=Raise52, 4=Raise100 */
static const int BET_PCT[]   = { 0, 33, 52, 100 };
static const int RAISE_PCT[] = { 33, 52, 100 };
#define MAX_RAISES_PER_STREET  2

#define P1  0
#define P2  1
#define STREET_FLOP  1
#define STREET_TURN  2
#define STREET_RIVER 3

/* -------------------------------------------------------------------------
 * Game state (information visible to the game tree)
 * ------------------------------------------------------------------------- */
typedef struct {
	uint64_t history;
	float pot, to_call, p1_stack, p2_stack, p1_contribution, p2_contribution;
	uint64_t p1_hand, p2_hand, board;
	uint64_t preset_turn_card, preset_river_card;
	uint8_t street, active_player, num_actions_this_street, num_raises_this_street;
	uint8_t num_actions_total, last_action;
	bool facing_bet, is_terminal;
} GameState;

/* -------------------------------------------------------------------------
 * Information set (one per unique infoset: history + board + private hand + pot/stacks)
 * ------------------------------------------------------------------------- */
typedef struct {
	float regret_sum[MAX_ACTIONS];
	float strategy_sum[MAX_ACTIONS];
	uint64_t key;
} InfoSet;

typedef struct {
	uint64_t key;
	InfoSet infoSet;
} HashTable;

static HashTable *table;

/* -------------------------------------------------------------------------
 * RNG (single global state; no threads)
 * ------------------------------------------------------------------------- */
static uint64_t rng_state;

static void rng_seed(unsigned int seed) {
	rng_state = (uint64_t)seed;
	if (rng_state == 0) rng_state = 1;
}

static float rng_uniform(void) {
	uint64_t x = rng_state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rng_state = x;
	return (float)(x >> 11) / (float)(UINT64_C(1) << 53);
}

/* -------------------------------------------------------------------------
 * Hash table
 * ------------------------------------------------------------------------- */
static uint64_t mix64(uint64_t h, uint64_t v) {
	h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
	return h;
}

static uint64_t quantize_cents(float value) {
	if (value <= 0.0f) return 0;
	return (uint64_t)(value * 100.0f + 0.5f);
}

static uint64_t hash_idx(uint64_t key) {
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdLLU;
	key ^= key >> 33;
	return key % TABLE_SIZE;
}

static uint64_t make_info_set_key(
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

static void init_table(void) {
	size_t i;
	if (!table) {
		table = (HashTable *)malloc((size_t)TABLE_SIZE * sizeof(HashTable));
		if (!table) abort();
	}
	for (i = 0; i < TABLE_SIZE; i++)
		table[i].key = EMPTY_MAGIC;
}

static InfoSet *get_or_create_node(uint64_t key) {
	uint64_t idx = hash_idx(key);
	unsigned int probes = 0;
	while (table[idx].key != EMPTY_MAGIC) {
		if (table[idx].key == key)
			return &table[idx].infoSet;
		idx = (idx + 1) % TABLE_SIZE;
		if (++probes >= TABLE_SIZE) return NULL;
	}
	table[idx].key = key;
	for (int i = 0; i < MAX_ACTIONS; i++) {
		table[idx].infoSet.regret_sum[i] = 0;
		table[idx].infoSet.strategy_sum[i] = 0;
	}
	table[idx].infoSet.key = key;
	return &table[idx].infoSet;
}

/* -------------------------------------------------------------------------
 * Game state init and advance
 * ------------------------------------------------------------------------- */
static void init_flop_state(
	GameState *state, uint64_t p1_hand, uint64_t p2_hand, uint64_t board,
	uint64_t preset_turn, uint64_t preset_river
) {
	memset(state, 0, sizeof(*state));
	state->p1_hand = p1_hand;
	state->p2_hand = p2_hand;
	state->board = board;
	state->preset_turn_card = preset_turn;
	state->preset_river_card = preset_river;
	state->pot = STARTING_FLOP_POT_BB;
	state->p1_stack = state->p2_stack = STARTING_STACK_BB;
	state->p1_contribution = state->p2_contribution = STARTING_FLOP_POT_BB * 0.5f;
	state->street = STREET_FLOP;
	state->active_player = P1;
}

static int seen_commit(const uint64_t seen[], int n, uint64_t cents) {
	for (int i = 0; i < n; i++)
		if (seen[i] == cents) return 1;
	return 0;
}

static uint8_t get_legal_actions(const GameState *state) {
	uint8_t legal = 0;
	float actor_stack = (state->active_player == P1) ? state->p1_stack : state->p2_stack;
	uint64_t seen[6];
	int n_seen = 0;

	if (state->street < STREET_FLOP || state->street > STREET_RIVER) return 0;

	if (state->facing_bet) {
		legal |= (1u << 0); /* Fold */
		if (actor_stack > 0.0f) legal |= (1u << 1); /* Call */
		{ float c = (state->to_call < actor_stack) ? state->to_call : actor_stack; seen[n_seen++] = quantize_cents(c);
		}
		if (state->num_raises_this_street < MAX_RAISES_PER_STREET) {
			for (int i = 2; i <= 4; i++) {
				float raise = state->pot * (float)RAISE_PCT[i - 2] / 100.0f;
				float total = state->to_call + raise;
				if (total > actor_stack) total = actor_stack;
				if (total <= state->to_call) continue;
				uint64_t cents = quantize_cents(total);
				if (seen_commit(seen, n_seen, cents)) continue;
				if (n_seen < 6) seen[n_seen++] = cents;
				legal |= (1u << i);
			}
		}
		return legal;
	}
	legal |= (1u << 0); /* Check */
	for (int i = 1; i <= 3; i++) {
		float bet = state->pot * (float)BET_PCT[i] / 100.0f;
		float commit = (bet < actor_stack) ? bet : actor_stack;
		if (commit <= 0.0f) continue;
		uint64_t cents = quantize_cents(commit);
		if (seen_commit(seen, n_seen, cents)) continue;
		if (n_seen < 6) seen[n_seen++] = cents;
		legal |= (1u << i);
	}
	return legal;
}

static void advance_street(GameState *state) {
	state->to_call = 0.0f;
	if (state->street < STREET_RIVER) {
		state->street++;
		state->num_actions_this_street = 0;
		state->num_raises_this_street = 0;
		state->active_player = P1;
		state->last_action = 0;
		state->facing_bet = false;
		if (state->street == STREET_TURN && state->preset_turn_card &&
		    __builtin_popcountll(state->board) == 3 &&
		    !((state->board | state->p1_hand | state->p2_hand) & state->preset_turn_card))
			state->board |= state->preset_turn_card;
		else if (state->street == STREET_RIVER && state->preset_river_card &&
		         __builtin_popcountll(state->board) == 4 &&
		         !((state->board | state->p1_hand | state->p2_hand) & state->preset_river_card))
			state->board |= state->preset_river_card;
	} else {
		state->is_terminal = true;
	}
}

static GameState apply_action(GameState state, int action_id) {
	float *actor_stack = (state.active_player == P1) ? &state.p1_stack : &state.p2_stack;
	float *actor_contrib = (state.active_player == P1) ? &state.p1_contribution : &state.p2_contribution;

	if (state.num_actions_total >= MAX_HISTORY) {
		state.is_terminal = true;
		return state;
	}
	state.history |= ((uint64_t)(action_id & 7) << (state.num_actions_total * BITS_PER_ACTION));
	state.last_action = (uint8_t)(action_id & 0xFF);
	state.num_actions_this_street++;
	state.num_actions_total++;

	if (state.facing_bet) {
		if (action_id == 0) {
			state.is_terminal = true;
			return state;
		}
		if (action_id == 1) {
			float call = (state.to_call < *actor_stack) ? state.to_call : *actor_stack;
			*actor_stack -= call;
			if (*actor_stack < 0.0f) *actor_stack = 0.0f;
			*actor_contrib += call;
			state.pot += call;
			state.to_call = 0.0f;
			advance_street(&state);
			return state;
		}
		if (action_id >= 2 && action_id <= 4) {
			float prev_call = state.to_call;
			float raise = state.pot * (float)RAISE_PCT[action_id - 2] / 100.0f;
			float commit = prev_call + raise;
			if (commit > *actor_stack) commit = *actor_stack;
			if (commit <= prev_call) {
				float call = (prev_call < *actor_stack) ? prev_call : *actor_stack;
				*actor_stack -= call;
				if (*actor_stack < 0.0f) *actor_stack = 0.0f;
				*actor_contrib += call;
				state.pot += call;
				state.to_call = 0.0f;
				advance_street(&state);
				return state;
			}
			*actor_stack -= commit;
			if (*actor_stack < 0.0f) *actor_stack = 0.0f;
			*actor_contrib += commit;
			state.pot += commit;
			state.to_call = commit - prev_call;
			state.num_raises_this_street++;
			state.active_player = (uint8_t)(1 - state.active_player);
			state.facing_bet = true;
			return state;
		}
		return state;
	}
	if (action_id == 0) {
		if (state.num_actions_this_street >= 2)
			advance_street(&state);
		else {
			state.active_player = 1 - state.active_player;
			state.facing_bet = false;
		}
		return state;
	}
	if (action_id >= 1 && action_id <= 3) {
		float bet = state.pot * (float)BET_PCT[action_id] / 100.0f;
		float commit = (bet < *actor_stack) ? bet : *actor_stack;
		if (commit <= 0.0f) return state;
		*actor_stack -= commit;
		if (*actor_stack < 0.0f) *actor_stack = 0.0f;
		*actor_contrib += commit;
		state.pot += commit;
		state.to_call = commit;
		state.num_raises_this_street++;
		state.active_player = 1 - state.active_player;
		state.facing_bet = true;
	}
	return state;
}

/* -------------------------------------------------------------------------
 * Terminal payoff and showdown (uses evaluate from ranks.c)
 * ------------------------------------------------------------------------- */
static float payoff_showdown(int my_strength, int opp_strength, float pot, float my_contrib) {
	if (my_strength > opp_strength) return pot - my_contrib;
	if (my_strength < opp_strength) return -my_contrib;
	return (pot * 0.5f) - my_contrib;
}

static uint64_t make_card(int rank, int suit) {
	return 1ULL << (rank + suit * 16);
}

static int build_remaining_cards(uint64_t used, uint64_t out[52]) {
	int n = 0;
	for (int r = 0; r < 13; r++)
		for (int s = 0; s < 4; s++) {
			uint64_t c = make_card(r, s);
			if (!(used & c)) out[n++] = c;
		}
	return n;
}

static float estimate_showdown_utility(const GameState *state, int traverser) {
	int n_board = __builtin_popcountll(state->board);
	int need = 5 - n_board;
	uint64_t my_hand = (traverser == P1) ? state->p1_hand : state->p2_hand;
	uint64_t opp_hand = (traverser == P1) ? state->p2_hand : state->p1_hand;
	float my_contrib = (traverser == P1) ? state->p1_contribution : state->p2_contribution;

	if (need < 0) return 0.0f;
	if (need == 0) {
		int my_s = evaluate(my_hand, state->board);
		int opp_s = evaluate(opp_hand, state->board);
		return payoff_showdown(my_s, opp_s, state->pot, my_contrib);
	}

	uint64_t used = state->board | state->p1_hand | state->p2_hand;
	uint64_t rem[52];
	int n_rem = build_remaining_cards(used, rem);
	if (n_rem <= 0) return 0.0f;

	float total = 0.0f;
	int outcomes = 0;
	if (need == 1) {
		for (int i = 0; i < n_rem; i++) {
			uint64_t board5 = state->board | rem[i];
			int my_s = evaluate(my_hand, board5);
			int opp_s = evaluate(opp_hand, board5);
			total += payoff_showdown(my_s, opp_s, state->pot, my_contrib);
		}
		outcomes = n_rem;
	} else if (need == 2) {
		for (int i = 0; i < n_rem; i++)
			for (int j = i + 1; j < n_rem; j++) {
				uint64_t board5 = state->board | rem[i] | rem[j];
				int my_s = evaluate(my_hand, board5);
				int opp_s = evaluate(opp_hand, board5);
				total += payoff_showdown(my_s, opp_s, state->pot, my_contrib);
			}
		outcomes = n_rem * (n_rem - 1) / 2;
	} else {
		return 0.0f;
	}
	return outcomes > 0 ? total / (float)outcomes : 0.0f;
}

static bool is_terminal(const GameState *state) {
	return state->is_terminal;
}

static float get_payout(const GameState *state, int traverser) {
	float contrib = (traverser == P1) ? state->p1_contribution : state->p2_contribution;
	if (state->is_terminal && state->last_action == 0 && state->facing_bet) {
		if (state->active_player == traverser)
			return -contrib;
		return state->pot - contrib;
	}
	return estimate_showdown_utility(state, traverser);
}

/* -------------------------------------------------------------------------
 * Chance node: deal random board cards for current street
 * ------------------------------------------------------------------------- */
static uint64_t deal_board_card(GameState state) {
	uint64_t used = state.p1_hand | state.p2_hand | state.board;
	uint64_t deck[52];
	int n = build_remaining_cards(used, deck);
	if (n <= 0) return 0;
	int i = (int)(rng_uniform() * (float)n);
	if (i >= n) i = n - 1;
	return deck[i];
}

/* -------------------------------------------------------------------------
 * Regret matching: strategy from regret_sum (only legal actions)
 * ------------------------------------------------------------------------- */
static void get_strategy(const float *regret, float *out, uint8_t legal) {
	int n_legal = __builtin_popcount(legal);
	float sum = 0.0f;
	for (int i = 0; i < MAX_ACTIONS; i++) {
		if (legal & (1 << i)) {
			out[i] = (regret[i] <= 0.0f) ? 0.0f : regret[i];
			sum += out[i];
		} else {
			out[i] = 0.0f;
		}
	}
	if (n_legal == 0) {
		for (int i = 0; i < MAX_ACTIONS; i++) out[i] = 1.0f / (float)MAX_ACTIONS;
		return;
	}
	for (int i = 0; i < MAX_ACTIONS; i++) {
		if (legal & (1 << i)) {
			out[i] = (sum > 0.0f) ? (out[i] / sum) : (1.0f / (float)n_legal);
		}
	}
}

static int sample_action(const float *strategy, uint8_t legal) {
	float r = rng_uniform();
	float cum = 0.0f;
	int last = 0;
	for (int a = 0; a < MAX_ACTIONS; a++) {
		if (legal & (1 << a)) {
			last = a;
			cum += strategy[a];
			if (r < cum) return a;
		}
	}
	return last;
}

/* -------------------------------------------------------------------------
 * MCCFR: single traversal. Traverser = player we are updating regrets for.
 * Opponent nodes: sample one action. Traverser nodes: traverse all, update regrets.
 * ------------------------------------------------------------------------- */
static float mccfr(GameState state, int traverser) {
	float strategy[MAX_ACTIONS], action_values[MAX_ACTIONS];
	uint8_t legal;
	InfoSet *node;
	uint64_t active_hand;
	int a;

	for (a = 0; a < MAX_ACTIONS; a++) action_values[a] = 0.0f;

	if (is_terminal(&state))
		return get_payout(&state, traverser);

	/* Chance node: deal missing board cards for current street */
	{
		int n_board = __builtin_popcountll(state.board);
		int need = 0;
		if (state.street == STREET_FLOP && n_board < 3) need = 3 - n_board;
		else if (state.street == STREET_TURN && n_board < 4) need = 4 - n_board;
		else if (state.street == STREET_RIVER && n_board < 5) need = 5 - n_board;

		if (need > 0) {
			GameState next = state;
			for (int i = 0; i < need; i++) {
				uint64_t card = 0;
				if (next.street == STREET_TURN && __builtin_popcountll(next.board) == 3 &&
				    next.preset_turn_card &&
				    !((next.board | next.p1_hand | next.p2_hand) & next.preset_turn_card))
					card = next.preset_turn_card;
				else if (next.street == STREET_RIVER && __builtin_popcountll(next.board) == 4 &&
				         next.preset_river_card &&
				         !((next.board | next.p1_hand | next.p2_hand) & next.preset_river_card))
					card = next.preset_river_card;
				else
					card = deal_board_card(next);
				if (!card) { next.is_terminal = true; break; }
				next.board |= card;
			}
			return mccfr(next, traverser);
		}
	}

	active_hand = (state.active_player == P1) ? state.p1_hand : state.p2_hand;
	node = get_or_create_node(make_info_set_key(
		state.history, state.board, active_hand, state.pot, state.p1_stack, state.p2_stack));
	if (!node) return 0.0f;

	legal = get_legal_actions(&state);
	get_strategy(node->regret_sum, strategy, legal);
	float node_value = 0.0f;

	if (state.active_player == traverser) {
		/* Traverser: evaluate every legal action, then update regrets */
		for (a = 0; a < MAX_ACTIONS; a++) {
			if (legal & (1 << a)) {
				GameState next = apply_action(state, a);
				action_values[a] = mccfr(next, traverser);
				node_value += strategy[a] * action_values[a];
			}
		}
		for (a = 0; a < MAX_ACTIONS; a++) {
			if (legal & (1 << a)) {
				float regret = action_values[a] - node_value;
				node->regret_sum[a] += regret;
			}
		}
	} else {
		/* Opponent: sample one action, accumulate strategy sum */
		int act = sample_action(strategy, legal);
		GameState next = apply_action(state, act);
		node_value = mccfr(next, traverser);
		for (a = 0; a < MAX_ACTIONS; a++)
			if (legal & (1 << a))
				node->strategy_sum[a] += strategy[a];
	}
	return node_value;
}

/* -------------------------------------------------------------------------
 * Average strategy (for reporting)
 * ------------------------------------------------------------------------- */
static void get_average_strategy(const InfoSet *node, float *out) {
	float sum = 0.0f;
	for (int a = 0; a < MAX_ACTIONS; a++) {
		out[a] = (node->strategy_sum[a] > 0.0f) ? node->strategy_sum[a] : 0.0f;
		sum += out[a];
	}
	if (sum > 0.0f)
		for (int a = 0; a < MAX_ACTIONS; a++) out[a] /= sum;
	else
		for (int a = 0; a < MAX_ACTIONS; a++) out[a] = 1.0f / (float)MAX_ACTIONS;
}

/* -------------------------------------------------------------------------
 * Deck: 52 cards as bitmasks (rank 0..12, suit 0..3)
 * ------------------------------------------------------------------------- */
static void build_deck(uint64_t deck[52]) {
	int n = 0;
	for (int r = 0; r < 13; r++)
		for (int s = 0; s < 4; s++)
			deck[n++] = make_card(r, s);
}

/* Fisher–Yates shuffle indices, then pick first 7 as p1(2), p2(2), board(3) */
static void deal_hands(uint64_t deck[52], uint64_t *p1, uint64_t *p2, uint64_t *board) {
	int idx[52];
	for (int i = 0; i < 52; i++) idx[i] = i;
	for (int i = 51; i >= 1; i--) {
		int j = (int)(rng_uniform() * (float)(i + 1));
		if (j > i) j = i;
		int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
	}
	*p1 = deck[idx[0]] | deck[idx[1]];
	*p2 = deck[idx[2]] | deck[idx[3]];
	*board = deck[idx[4]] | deck[idx[5]] | deck[idx[6]];
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */
int main(void) {
	uint64_t deck[52];
	const int iterations = 50000;

	init_flush_map();
	init_rank_map();
	init_table();
	rng_seed((unsigned)time(NULL));
	build_deck(deck);

	printf("NLH GTO solver (MCCFR, single-threaded, flop-only example)\n");
	printf("Running %d iterations...\n", iterations);

	for (int i = 0; i < iterations; i++) {
		uint64_t p1, p2, board;
		deal_hands(deck, &p1, &p2, &board);
		if (p1 & p2) continue;

		GameState state;
		init_flop_state(&state, p1, p2, board, 0, 0);
		mccfr(state, P1);
		init_flop_state(&state, p1, p2, board, 0, 0);
		mccfr(state, P2);

		if ((i + 1) % 10000 == 0)
			printf("  %d iterations done\n", i + 1);
	}

	/* Count and print a few sample infosets */
	int count = 0;
	for (size_t i = 0; i < TABLE_SIZE && count < 5; i++) {
		if (table[i].key == EMPTY_MAGIC) continue;
		float probs[MAX_ACTIONS];
		get_average_strategy(&table[i].infoSet, probs);
		float total = 0.0f;
		for (int a = 0; a < MAX_ACTIONS; a++) total += probs[a];
		if (total < 0.001f) continue;
		count++;
		printf("\nInfoset sample %d (key %llu): ", count, (unsigned long long)table[i].key);
		printf("Check %.2f Bet33 %.2f Bet52 %.2f Bet100 %.2f | Fold %.2f Call %.2f R33 %.2f R52 %.2f R100 %.2f\n",
		       (double)probs[0], (double)probs[1], (double)probs[2], (double)probs[3],
		       (double)probs[0], (double)probs[1], (double)probs[2], (double)probs[3], (double)probs[4]);
	}
	printf("\nDone. Table has entries; sample strategy printed above.\n");
	return 0;
}
