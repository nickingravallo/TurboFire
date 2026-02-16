#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define NUM_CARDS     3 //2 private 1 public
#define MAX_ACTIONS   3 //fold call/check raise/bet
#define MAX_HISTORY   100

#define FOLD_MASK     1
#define CHECK_MASK    2
#define CALL_MASK     2
#define RAISE_MASK    4
#define BET_MASK      4

#define P1            0
#define P2            1

#define GET_RANK(card) (card / 2) //0=J 1=Q 2=K
#define GET_SUIT(card) (card % 2) //0=black 1=red

typedef enum { false, true } bool;

#define TABLE_SIZE    1000003
#define EMPTY_MAGIC   0xBEEFBEEF

typedef struct {
	uint64_t history;
	uint8_t pot;

	uint8_t p1_card;
	uint8_t p2_card;
	int8_t board_card;

	uint8_t street;
	uint8_t active_player;
	uint8_t num_actions_this_street;
	uint8_t num_raises_this_street;
	uint8_t num_actions_total;
	uint8_t last_action;
} GameState;

typedef struct {
	float regret_sum[MAX_ACTIONS];
	float strategy_sum[MAX_ACTIONS];
	uint64_t key;
} InfoSet;

typedef struct {
	uint64_t key;
	InfoSet infoSet;
} HashTable;

HashTable table[TABLE_SIZE];

void init_table() {
	for ( int i = 0; i < TABLE_SIZE; i++)
		table[i].key = EMPTY_MAGIC;
}

//pulled from murmurhash
uint64_t hash(uint64_t key) {
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdLLU;
	key ^= key >> 33;
	return key % TABLE_SIZE;
}

uint64_t make_hash_key(uint64_t history, int board, int private_card) {
	uint64_t key = 0;

	key |= (uint64_t)private_card;
	key |= (uint64_t)board   << 8;
	key |= (uint64_t)history << 16;
	
	return key;
}

InfoSet* get_or_create_node(uint64_t key) {
	uint64_t hash_key;
	int i;

	hash_key = hash(key) % TABLE_SIZE;
	while (table[hash_key].key != EMPTY_MAGIC) {
		if (table[hash_key].key == key)
			return &table[hash_key].infoSet;

		hash_key = (hash_key + 1) % TABLE_SIZE;
	}

	//empty slot
	table[hash_key].key = key;
	for (i = 0; i < MAX_ACTIONS; i++) {
		table[hash_key].infoSet.regret_sum[i] = 0;
		table[hash_key].infoSet.strategy_sum[i] = 0;
	}
	return &table[hash_key].infoSet;
}

bool is_terminal(GameState* state) {
	if (state->last_action == FOLD_MASK)
		return true;

	if (state->street == 1) {
		if (state->last_action == CALL_MASK)
			return true;
		if (state->last_action == CHECK_MASK && state->num_actions_this_street >= 2)
			return true;
	}

	return false;
}

float get_payout(GameState* state, int traverser) {
	int opponent;

	int traverser_card, opponent_card;
	bool traverser_paired, opponent_paired;

	if (state->last_action == FOLD_MASK)
		return (state->active_player == traverser) ? -(state->pot / 2.0f) : (state->pot / 2.0f);

	opponent = 1 - traverser;
	traverser_card = (traverser == P1) ? state->p1_card : state->p2_card;
	opponent_card  = (opponent == P1)  ? state->p1_card : state->p2_card;

	traverser_paired = (GET_RANK(traverser_card) == GET_RANK(state->board_card));
	opponent_paired  = (GET_RANK(opponent_card) == GET_RANK(state->board_card));

	if (traverser_paired && !opponent_paired)
		return (state->pot / 2.0f);
	if (!traverser_paired && opponent_paired)
		return -(state->pot / 2.0f);

	if (GET_RANK(traverser_card) > GET_RANK(opponent_card))
		return (state->pot / 2.0f);
	if (GET_RANK(traverser_card) < GET_RANK(opponent_card)) 
		return -(state->pot / 2.0f);

	return 0.0f; //chop
}

void get_strategy(float* regret, float* out_strategy, uint8_t legal_actions) {
	int i;
	uint8_t num_legal_actions;
	float normalized_sum;

	num_legal_actions = __builtin_popcount(legal_actions);

	normalized_sum = 0;
	for ( i = 0 ; i < MAX_ACTIONS; i++) {
		if (legal_actions & (1 << i)) {
			out_strategy[i]   = (regret[i] <= 0) ? 0 : regret[i];
			normalized_sum += out_strategy[i];
		}
		else
			out_strategy[i] = 0.0;
	}

	for ( i = 0; i < MAX_ACTIONS; i++) {
		if (legal_actions & (1 << i)) {
			if (normalized_sum > 0)
				out_strategy[i] = out_strategy[i] / normalized_sum;
			else
				out_strategy[i] = 1.0 / (float)num_legal_actions;
		}
	}
}

int get_action(float* strategy, uint8_t legal_actions) {
	float r;
	float cumulative_strat;
	int a, last_legal_action;

	last_legal_action = 0;
	cumulative_strat = 0;
	r = (float)rand() / (float)RAND_MAX;

	for ( a = 0; a < MAX_ACTIONS; a++) {
		//check mask
		if (legal_actions & (1 << a)) {
			last_legal_action = a;
			cumulative_strat += strategy[a];

			if ( r < cumulative_strat )
				return a;
		}
	}

	return last_legal_action;
}

GameState apply_action(GameState state, int action_id) {
	int previous_action;

	previous_action = state.last_action;
	state.last_action = (1 << action_id);
	state.history |= (state.last_action << (state.num_actions_total * 3));
	state.num_actions_this_street++;
	state.num_actions_total++;

	if (action_id == 0) //fold
		return state;
	else if (action_id == 1) { //call or check
		bool is_facing_bet = (previous_action == RAISE_MASK);

		if (is_facing_bet) { //call the bet
			int call_amount;

			call_amount = (state.street == 0) ? 2 : 4;
			state.pot += call_amount;
			
			state.street++;
			state.num_actions_this_street = 0;
			state.num_raises_this_street  = 0;
			state.active_player = P1;
			state.last_action = 0;
		}
		else { //check
			if (state.num_actions_this_street == 2) {
				state.street++;
				state.num_actions_this_street = 0;
				state.num_raises_this_street  = 0;
				state.active_player = P1;
				state.last_action = 0;
			}
			else { //check->check
				state.active_player = 1 - state.active_player;
			}
		}
	}
	else if (action_id == 2) {
		int bet_limit;
		bool is_facing_bet;

		bet_limit = (state.street == 0) ? 2 : 4;
		is_facing_bet = (previous_action == RAISE_MASK);
		
		if (is_facing_bet)
			state.pot += (bet_limit * 2);
		else
			state.pot += bet_limit;

		state.num_raises_this_street++;
		state.active_player = 1 - state.active_player;
	}

	return state;
}

uint8_t get_legal_actions(GameState state) {
	uint8_t legal_mask;
	bool is_facing_bet;

	legal_mask = 0;
	legal_mask |= CHECK_MASK; //we can always call or check

	is_facing_bet = (state.last_action == RAISE_MASK);
	if (is_facing_bet)
		legal_mask |= FOLD_MASK;

	if (state.num_raises_this_street < 2)
		legal_mask |= RAISE_MASK;

	return legal_mask;
}

int deal_random_board_card(GameState state) {
	int rcard;

	do
		rcard = rand() % 6;	
	while (state.p1_card == rcard || state.p2_card == rcard);

	return rcard;
}

float mccfr(GameState state, int traverser) {
	float strategy[MAX_ACTIONS];
	float action_values[MAX_ACTIONS];
	float node_value;

	InfoSet* node;

	uint8_t legal_actions;
	int active_card;

	if (is_terminal(&state))
		return get_payout(&state, traverser);

	//chance node, going to flop
	if (state.street == 1 && state.board_card == -1) {
		GameState next_state = state;
		next_state.board_card = deal_random_board_card(state);
		next_state.num_actions_this_street = 0;
		next_state.num_raises_this_street = 0;
		return mccfr(next_state, traverser);
	}

	active_card = (state.active_player == P1) ? state.p1_card : state.p2_card;

	node = get_or_create_node(make_hash_key(state.history, state.board_card, active_card));
	legal_actions = get_legal_actions(state);
	get_strategy(node->regret_sum, strategy, legal_actions);
	node_value = 0;

	if (state.active_player == traverser) {
		//walk legal actions
		for ( int a = 0 ; a < MAX_ACTIONS; a++ ) {
			if (legal_actions & (1 << a)) {
				GameState next_state = apply_action(state, a);
				action_values[a] = mccfr(next_state, traverser);
				node_value += strategy[a] * action_values[a];
			}
		}

		//calcualte and update node regret
		for (int a = 0 ; a < MAX_ACTIONS; a++) {
			if (legal_actions & (1 << a)) {
				float regret = action_values[a] - node_value;
				node->regret_sum[a] += regret;
			}
		}
	}
	else {
		int sampled_action = get_action(strategy, legal_actions);
		GameState next_state = apply_action(state, sampled_action);

		node_value = mccfr(next_state, traverser);

		//update strategy
		for (int a = 0; a < MAX_ACTIONS; a++)
			if (legal_actions & (1 << a)) 
				node->strategy_sum[a] += strategy[a];
	}

	return node_value;
}

int main() {
    init_table();
    for (int i = 0; i < 1000000; i++) {
        GameState state = {0};
        // Initial Deal
        state.p1_card = rand() % 6;
        do { state.p2_card = rand() % 6; } while (state.p2_card == state.p1_card);
        
        state.board_card = -1;
        state.pot = 2; // Antes
        state.active_player = P1;

        // Alternate traversers so both players learn
        mccfr(state, P1);
        mccfr(state, P2);

        if (i % 100000 == 0) printf("Iteration %d...\n", i);
    }
    // Now you can print the strategy_sum from the hash table
    return 0;
}
