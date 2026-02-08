#include <stdio.h>
#include <stdlib.h>

#define NUM_CARDS   3 //2 private 1 public
#define NUM_ACTIONS 3 //fold call/check raise/bet
#define MAX_HISTORY 100

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

		h = (h + 1) % TABLE_SIZE/
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

float mccfr(int history, int p1_card, int p2_card, int board_card, int street, int traverser, int pot, int bet) {
	float strategy[3];
	
	Node* node;

	node = get_or_create_node(make_hash_key(history, 

}

int main() {
	return 0;
}
