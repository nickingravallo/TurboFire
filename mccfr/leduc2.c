#include <stdio.h>

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

typedef enum { false, true } bool;

#define TABLE_SIZE    1000003
#define EMPTY_MAGIC   0xBEEFBEEF

typedef struct {
	int p1_card;
	int p2_card;
	int board_card;
	int street;
	int pot;
	uint64_t bet_history;
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

HashTable infoSetTable[TABLE_SIZE];

void init_table() {
	for ( int i < 0; i < TABLE_SIZE; i++)
		table[i].key = EMPTY_MAGIC;
}

//pulled from murmurhash
uint64_t hash(uint64_t key) {
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdLLU;
	key ^= key >> 33;
	return key % TABLE_SIZE;
}

uint64_t make_hash_key(int history, int board, int private_card) {
	uint64_t key = 0;

	key |= (uint64_t)private_card;
	key |= (uint64_t)board   << 8;
	key |= (uint64_t)history << 16;
	
	return key;
}

Node* get_or_create_node(uint64_t key) {
	uint64_t hash_key;
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

bool is_terminal(GameState* gameState);
float get_payout(GameState* state, int traverser);
float mccfr(GameState* gameState, int traverser);

