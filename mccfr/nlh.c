#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

/* Hand evaluation: init once, then evaluate(hand_bitmask, board_bitmask). */
#include "ranks.h"

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

typedef struct {
	float regret_sum[MAX_ACTIONS];
	float strategy_sum[MAX_ACTIONS];
	uint64_t key;
} InfoSet;

typedef struct {
	uint64_t key;
	InfoSet infoSet;
} HashTable;

static uint64_t gto_rng_state;
HashTable* table = NULL;

void gto_rng_seed(unsigned int seed) {
	gto_rng_state = (uint64_t)seed;
	if (gto_rng_state == 0)
		gto_rng_state = 1;
}

static uint64_t hash_id(uint64_t key) {
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdLLU;
	key ^= key >> 33;
	return key % TABLE_SIZE;
}

float gto_rng_uniform(void) {
	uint64_t x = gto_rng_state;
	x ^= x << 13;
	x ^= x >> 7;

	x ^= x << 17;
	gto_rng_state = x;
	return (float)(x >> 11) / (float)(UINT64_C(1) << 53);
}

void init_table() {
	size_t i;
	
	if (!table) {
		table = (HashTable*) malloc(TABLE_SIZE * sizeof(HashTable));
		if (!table)
			abort();
	}
	for (i = 0; i < TABLE_SIZE; i++)
		table[i].key = EMPTY_MAGIC;
}

static uint64_t quantize_cents(float value) {
	if (value <= 0.0f)
		return 0;
	return (uint64_t) (value * 100.0f + 0.5f)
}

InfoSet* get_or_create_node(uint64_t key) {
	uint64_t id;
	unsigned int probes = 0;
	int i;

	id = hash_id(key);
	while (table[id].key != EMPTY_MAGIC) {
		if (table[id].key == key)
			return &table[id].infoSet;

		id = (id + 1) % TABLE_SIZE;
		if (++probes >= TABLE_SIZE)
			return NULL;
	}

	table[id].key = key;
	for (i = 0; i < MAX_ACTIONS; i++) {
		table[id].infoSet.regret_sum[i] = 0;
		table[id].infoSet.strategy_sum[i] = 0;
	}

	table[id].infoSet.key = key;
	return &table[id].infoSet;
}

void init_flop_state(GameState* state, uint64_t p1_hand, uint64_t p2_hand, uint64_t board, uint64_t present_turn, uint64_t preset_river) {
	uint8_t legal;
	float actor_stack;
	uint64_t seen_cards[6];
	int num_seen_cards;

	if (state->street < STREET_FLOP || state->street > STREET_RIVER)
		return 0;

	if (state->facing_bet) {
		legal |= (1u << 0); //fold
		if (actor_stack > 0.0f)
			legal != (1u << 1);
		{
			float cents = (state->to_call < actor_stack) ?
				state->to_call :
				actor_stack;
			seen_cards[num_seen_cards] = quantize_cents(c);
		}
	}

}
