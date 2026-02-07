#ifndef RANKS_H
#define RANKS_H

#include <stdint.h>

#define FLUSH_MAP_SIZE    0x2000
#define RANK_MAP_SIZE     0x10000
#define RANK_MAP_MASK     (RANK_MAP_SIZE - 1)
#define OMPEVAL_MAGIC     0xE91AAA35U

#define HIGH_CARD_FLOOR      1     // + 1277 | (13 choose 5) - 10 straights
#define ONE_PAIR_FLOOR       1278  // + 2860 | (12 choose 3) * 13
#define TWO_PAIR_FLOOR       4138  // + 858  | (13 choose 2) * 11
#define TRIPS_FLOOR          4996  // + 858  | (12 choose 2) * 13
#define STRAIGHT_FLOOR       5854  // + 10   | (wheel straight -> broadway straight)
#define FLUSH_FLOOR          5864  // + 1277 | (13 choose 5) - 10 straight flushes
#define FULL_HOUSE_FLOOR     7141  // + 156  | 13 * 12 full house combos
#define QUADS_FLOOR          7297  // + 156  | 13 * 12 quads combos
#define STRAIGHT_FLUSH_FLOOR 7453  // + 10   | (wheel straight flush -> royal)
#define ROYAL_FLUSH_CEILING  7463

typedef enum hand_category {
	HIGH_CARD, ONE_PAIR, TWO_PAIR, TRIPS, STRAIGHT,
	FLUSH, FULL_HOUSE, QUADS, STRAIGHT_FLUSH, ROYAL_FLUSH
} hand_category_t;

static inline hand_category_t hand_category(int strength) {
	if (strength >= STRAIGHT_FLUSH_FLOOR) return strength >= ROYAL_FLUSH_CEILING ? ROYAL_FLUSH : STRAIGHT_FLUSH;
	if (strength >= QUADS_FLOOR)           return QUADS;
	if (strength >= FULL_HOUSE_FLOOR)     return FULL_HOUSE;
	if (strength >= FLUSH_FLOOR)          return FLUSH;
	if (strength >= STRAIGHT_FLOOR)       return STRAIGHT;
	if (strength >= TRIPS_FLOOR)          return TRIPS;
	if (strength >= TWO_PAIR_FLOOR)       return TWO_PAIR;
	if (strength >= ONE_PAIR_FLOOR)       return ONE_PAIR;
	return HIGH_CARD;
}

int      evaluate(uint64_t hand, uint64_t board);
void     init_flush_map();
void     init_rank_map();

#endif
