/**
 * evaluator.h - Fast Hand Evaluation (Two Plus Two Algorithm)
 * 
 * This implements a fast 5-7 card poker hand evaluator using the
 * Two Plus Two algorithm with perfect hash lookup tables.
 * 
 * The algorithm works by:
 * 1. Starting with a base value for 0 cards
 * 2. For each card, look up: value = table[value + card]
 * 3. Final value is the hand rank (0-7462, where 0 is strongest)
 * 
 * Hand ranks are from the Cactus Kev evaluation system:
 * - 0: Royal Flush
 * - 1-10: Straight Flush
 * - 11-166: Four of a Kind
 * - 167-322: Full House
 * - 323-1599: Flush
 * - 1600-1608: Straight
 * - 1609-2467: Three of a Kind
 * - 2468-3325: Two Pair
 * - 3326-6185: One Pair
 * - 6186-7462: High Card
 * 
 * The lookup table is pre-computed and loaded from disk (~125MB).
 */

#ifndef EVALUATOR_H
#define EVALUATOR_H

#include <stdint.h>
#include "cards.h"

/* Maximum number of cards in a Texas Hold'em hand (2 hole + 5 board) */
#define MAX_HAND_CARDS 7

/* Number of distinct hand ranks (0-7462) */
#define NUM_HAND_RANKS 7463

/* 
 * Hand types for classification
 */
typedef enum {
    HAND_HIGH_CARD = 0,
    HAND_ONE_PAIR,
    HAND_TWO_PAIR,
    HAND_THREE_KIND,
    HAND_STRAIGHT,
    HAND_FLUSH,
    HAND_FULL_HOUSE,
    HAND_FOUR_KIND,
    HAND_STRAIGHT_FLUSH,
    HAND_ROYAL_FLUSH
} HandType;

/* 
 * Evaluation result
 */
typedef struct {
    int rank;           /* 0-7462, lower is better */
    HandType type;      /* Classification */
    const char* name;   /* Human-readable name */
} EvalResult;

/*
 * Global lookup table for hand evaluation
 * Size: 32487834 * 4 bytes = ~124MB
 * This is loaded from disk at startup
 */
extern int32_t* hand_rank_table;

/* Initialize evaluator - loads lookup table from disk */
int evaluator_init(const char* table_path);

/* Cleanup evaluator - frees lookup table */
void evaluator_cleanup(void);

/* 
 * Evaluate a hand given cards array
 * Returns rank 0-7462 (0 is best)
 */
int evaluate_hand(const Card* cards, int num_cards);

/* 
 * Evaluate hand from bit mask
 * More efficient when working with masks
 */
int evaluate_hand_mask(HandMask mask);

/* 
 * Get detailed evaluation result with classification
 */
EvalResult evaluate_hand_detailed(const Card* cards, int num_cards);

/* 
 * Compare two hands
 * Returns: >0 if hand1 wins, <0 if hand2 wins, 0 if tie
 */
int compare_hands(const Card* hand1, int num1, const Card* hand2, int num2);

/* Get hand type from rank */
HandType get_hand_type(int rank);

/* Get hand name from rank */
const char* get_hand_name(int rank);

/* 
 * Utility: Generate lookup table (call this once to create data file)
 * This takes several minutes but only needs to be done once
 */
int generate_lookup_table(const char* output_path);

#endif /* EVALUATOR_H */
