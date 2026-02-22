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

static uint64_t gto_rng_state;
HashTable* gto_table = NULL;

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


