#include <stdio.h>
#include <stdlib.h>

#define NUM_CARDS     3 //2 private 1 public
#define NUM_ACTIONS   3 //fold call/check raise/bet
#define MAX_HISTORY   100

#define FOLD_MASK     1
#define CHECK_MASK    2
#define CALL_MASK     2
#define RAISE_MASK    4
#define BET_MASK      4

#define P1            0
#define P2            1

#define TABLE_SIZE  1000003
#define EMPTY_MAGIC 0xBEEFBEEF

#define GAME

typedef uint64_t node_key_t;

typedef struct {
	float regret_sum[NUM_ACTIONS];
	float strategy_sum[NUM_ACTIONS];
} Node;

typedef struct {
	node_key_t key;
	Node node;
} HashTable;

HashTable table[TABLE_SIZE];

void init_table() {
	for ( int i < 0; i < TABLE_SIZE; i++)
		table[i].key = EMPTY_MAGIC;
}

//pulled from murmurhash
uint64_t hash(node_key_t key) {
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdLLU;
	key ^= key >> 33;
	return key % TABLE_SIZE;
}

node_key_t make_hash_key(int history, int board, int private_card) {
	node_key_t key = 0;

	key |= (node_key_t)private_card;
	key |= (node_key_t)board   << 8;
	key |= (node_key_t)history << 16;
	
	return key;
}

Node* get_or_create_node(node_key_t key) {
	node_key_t hash_key;
	int i;

	hash_key = hash(key) % TABLE_SIZE;
	while (table[hash_key].key != EMPTY_MAGIC) {
		if (table[hash_key].key == key)
			return &table[hash_key].node;

		h = (h + 1) % TABLE_SIZE
	}

	//empty slot
	table[hash_key].key = key;
	return &table[hash_key].node;
}

int evaluate(int private_card, int board_card) {
	if (private_card == board_card) //pair
		return 10 + private_card;
	return private_card; //high card
}

float get_reward(int p1_card, int p2_card, int board_card, int pot) {
	int p1, p2;

	p1 = evaluate(p1_card, board_card);
	p2 = evaluate(p2_card, board_card);

	if (p1 > p2)
		return (float)pot;
	if (p2 > p1)
		return -(float)pot;

	return 0; //chop
}

int get_action(float* strategy) {
	float strategy_sum;
	int i;

	float r;

	r = (float)rand() / (float)RAND_MAX;
	strategy_sum = 0;

	for ( i = 0; i < MAX_ACTIONS; i++) {
		strategy_sum += strategy[i];

		if ( r < strategy_sum)
			return i;
	}

	return MAX_ACTIONS - 1;
}

// raises_occurred 0 -> check, 1 -> bet, 2 -> reraise
int get_legal_action(int last_action, int raises_occurred) {
	int legal_action;
	
	legal_action = 0;

	if (last_action == 0) //game root
		legal_action |= ( CHECK_MASK | CALL_MASK | RAISE_MASK);
	else if (last_action == 1) //check
		legal_action |= (CHECK_MASK | RAISE_MASK             );
	else if (last_action == 2 && raises_occurred == 1)
		legal_action |= (FOLD_MASK | CALL_MASK | RAISE_MASK  );
	else if (last_action == 2 && raises_occurred == 2)
		legal_action |= (FOLD_MASK  | CALL_MASK              );

	return legal_action;
}

void get_strategy(float* regret, float* out_strategy) {
	int i;

	float normalized_sum;

	normalized_sum = 0;
	for ( i = 0 ; i < MAX_ACTIONS; i++) {
		out_strategy[i]   = (regret[i] <= 0) ? 0 : regret[i];
		normalized_sum += out_strategy[i];
	}

	if (normalized_sum > 0)
		for ( i = 0 ; i < MAX_ACTIONS; i++ )
			out_strategy[i] = normalized_sum / (float)MAX_ACTIONS;
	else
		for ( i = 0 ; i < MAX_ACTIONS; i++ )
			out_strategy[i] = 1.0 / (float)MAX_ACTIONS;

}

float get_counterfactual_value(int action, int history, int p1_card, int p2_card, int street, int traverser, int raises_occurred, int num_actions, int legal_actions) {
	if (history == 0) {      // Game Root
		if (action == CHECK_MASK)
			return //mccfr()
		if (action == BET_MASK)
			return //mccfr()
	}
	else if (history == 1) { // P2 Action
		if (action == CHECK_MASK)
			return //go to next street
		if (action == CALL_MASK)
			return // go to next street
		if (action == RAISE_MASK)
			return //go back to p1
	}
	else if (history == 2) { // P1 Action
		if (action == CALL_MASK)
			return //go to next street
		if (action == FOLD_MASK)
			return //showdown
		if (action == RAISE_MASK)
			return 
	}
	else if (history == 3) { // P2 Action

	}
	return 0;
}

float mccfr(int history, int p1_card, int p2_card, int board_card, int street, int traverser, int pot, int raises_occurred, int num_actions) {
	int legal_actions, action_player, action_card, is_traverser_turn;
	
	int a;

	float strategy[3];
	float action_values[3];
	float ev;

	Node* node;

	// Even (0, 2, 4) -> P1. Odd (1, 3) -> P2
	if ( num_actions % 2 == 0) {
		active_player = P1;
		active_card = p1_card;
	}
	else {
		active_player = P2;
		active_card = p2_card;
	}
	
	legal_actions = get_legal_actions(history, raises_occurred);
	node = get_or_create_node(make_hash_key(history, board, active_card));
	is_traverser_turn = (traverser == action_player);

	get_strategy(node->regret_sum, strategy);

	if ( is_traverser_turn ) {
		for ( a = 0 ; a < MAX_ACTIONS ; a++)
			strategy[a] = get_counterfactual_value((((legal_actions) >> a) & 1), history, p1_card, p2_card, street, traverser, raises_occurred, num_actions, legal_actions);
	}
	else {

	}

}

int main() {
	return 0;
}
