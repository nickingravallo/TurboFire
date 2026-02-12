/**
 * cards.h - Card Representation and Deck Utilities
 * 
 * This module provides fundamental card manipulation for poker.
 * Cards are represented as integers 0-51, where:
 *   - Card value = rank + suit * 13
 *   - Ranks: 0=2, 1=3, ..., 11=K, 12=A
 *   - Suits: 0=clubs, 1=diamonds, 2=hearts, 3=spades
 * 
 * This compact representation allows efficient storage and
 * fast bit manipulation for hand evaluation.
 */

#ifndef CARDS_H
#define CARDS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Number of cards in a standard deck */
#define DECK_SIZE 52
#define NUM_RANKS 13
#define NUM_SUITS 4

/* Card type - just an integer from 0-51 */
typedef uint8_t Card;

/* A hand is represented as a bit mask (52 bits) for fast operations */
typedef uint64_t HandMask;

/* 
 * Card operations
 */

/* Get rank of a card (0-12, where 12=Ace) */
static inline int card_rank(Card c) {
    return c % NUM_RANKS;
}

/* Get suit of a card (0-3) */
static inline int card_suit(Card c) {
    return c / NUM_RANKS;
}

/* Create a card from rank and suit */
static inline Card make_card(int rank, int suit) {
    return (Card)(suit * NUM_RANKS + rank);
}

/* Create a hand mask from a single card */
static inline HandMask card_mask(Card c) {
    return ((HandMask)1) << c;
}

/* Convert card to string representation (e.g., "As", "Td") */
void card_to_string(Card c, char* buf);

/* Parse card from string (e.g., "As" -> 51) */
Card card_from_string(const char* str);

/* 
 * Deck operations
 */

typedef struct {
    Card cards[DECK_SIZE];
    int num_cards;
} Deck;

/* Initialize a full sorted deck */
void deck_init(Deck* deck);

/* Shuffle deck using Fisher-Yates algorithm */
void deck_shuffle(Deck* deck);

/* Deal a card from the deck (returns top card, decreases count) */
Card deck_deal(Deck* deck);

/* Remove specific cards from deck (used for known cards) */
void deck_remove_cards(Deck* deck, const Card* cards, int num_cards);

/* Deal a specific number of cards from the deck */
int deck_deal_n(Deck* deck, Card* cards, int n);

/*
 * Hand mask operations
 */

/* Count number of cards in a hand mask */
int hand_mask_count(HandMask mask);

/* Add card to hand mask */
static inline HandMask hand_mask_add(HandMask mask, Card c) {
    return mask | card_mask(c);
}

/* Check if card is in hand mask */
static inline bool hand_mask_contains(HandMask mask, Card c) {
    return (mask & card_mask(c)) != 0;
}

/* Convert hand mask to array of cards (returns count) */
int hand_mask_to_cards(HandMask mask, Card* cards);

/*
 * String representations
 */

/* Convert hand mask to string (e.g., "As Ks Qs Js Ts") */
void hand_mask_to_string(HandMask mask, char* buf, size_t buf_size);

/* All card rank characters */
extern const char RANK_CHARS[];

/* All card suit characters */
extern const char SUIT_CHARS[];

/* Rank names for display */
extern const char* RANK_NAMES[];

#endif /* CARDS_H */
