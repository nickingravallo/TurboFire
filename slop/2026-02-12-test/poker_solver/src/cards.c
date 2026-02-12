/**
 * cards.c - Card Representation Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cards.h"

/* Card rank characters: 2, 3, 4, 5, 6, 7, 8, 9, T, J, Q, K, A */
const char RANK_CHARS[] = "23456789TJQKA";

/* Card suit characters: clubs, diamonds, hearts, spades */
const char SUIT_CHARS[] = "cdhs";

/* Full rank names for display */
const char* RANK_NAMES[] = {
    "2", "3", "4", "5", "6", "7", "8", "9", "10", "Jack", "Queen", "King", "Ace"
};

/* Convert card to 2-character string representation */
void card_to_string(Card c, char* buf) {
    buf[0] = RANK_CHARS[card_rank(c)];
    buf[1] = SUIT_CHARS[card_suit(c)];
    buf[2] = '\0';
}

/* Parse card from 2-character string */
Card card_from_string(const char* str) {
    if (strlen(str) < 2) return 0;
    
    /* Find rank */
    int rank = -1;
    for (int i = 0; i < NUM_RANKS; i++) {
        if (RANK_CHARS[i] == str[0]) {
            rank = i;
            break;
        }
    }
    
    /* Find suit */
    int suit = -1;
    for (int i = 0; i < NUM_SUITS; i++) {
        if (SUIT_CHARS[i] == str[1]) {
            suit = i;
            break;
        }
    }
    
    if (rank == -1 || suit == -1) return 0;
    return make_card(rank, suit);
}

/* Initialize a full sorted deck (clubs first, then diamonds, hearts, spades) */
void deck_init(Deck* deck) {
    deck->num_cards = DECK_SIZE;
    for (int i = 0; i < DECK_SIZE; i++) {
        deck->cards[i] = (Card)i;
    }
}

/* Shuffle deck using Fisher-Yates algorithm with proper randomization */
void deck_shuffle(Deck* deck) {
    /* Initialize random seed if not already done */
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }
    
    /* Fisher-Yates shuffle - O(n) and unbiased */
    for (int i = deck->num_cards - 1; i > 0; i--) {
        /* Pick a random index from 0 to i */
        int j = rand() % (i + 1);
        /* Swap cards[i] and cards[j] */
        Card temp = deck->cards[i];
        deck->cards[i] = deck->cards[j];
        deck->cards[j] = temp;
    }
}

/* Deal top card from deck, reducing card count */
Card deck_deal(Deck* deck) {
    if (deck->num_cards == 0) {
        return 0; /* No cards left */
    }
    return deck->cards[--deck->num_cards];
}

/* Remove specific cards from deck (used when we know certain cards) */
void deck_remove_cards(Deck* deck, const Card* cards, int num_cards) {
    for (int i = 0; i < num_cards; i++) {
        Card target = cards[i];
        /* Find and remove this card */
        for (int j = 0; j < deck->num_cards; j++) {
            if (deck->cards[j] == target) {
                /* Replace with last card to maintain compact array */
                deck->cards[j] = deck->cards[deck->num_cards - 1];
                deck->num_cards--;
                break;
            }
        }
    }
}

/* Deal a specific number of cards from the deck */
int deck_deal_n(Deck* deck, Card* cards, int n) {
    int dealt = 0;
    for (int i = 0; i < n && deck->num_cards > 0; i++) {
        cards[dealt++] = deck_deal(deck);
    }
    return dealt;
}

/* Count number of cards in hand mask using Brian Kernighan's algorithm */
int hand_mask_count(HandMask mask) {
    int count = 0;
    while (mask) {
        mask &= mask - 1; /* Clear lowest set bit */
        count++;
    }
    return count;
}

/* Convert hand mask to array of cards, returns count */
int hand_mask_to_cards(HandMask mask, Card* cards) {
    int count = 0;
    for (int i = 0; i < DECK_SIZE; i++) {
        if (mask & ((HandMask)1 << i)) {
            cards[count++] = (Card)i;
        }
    }
    return count;
}

/* Convert hand mask to human-readable string */
void hand_mask_to_string(HandMask mask, char* buf, size_t buf_size) {
    Card cards[DECK_SIZE];
    int count = hand_mask_to_cards(mask, cards);
    
    buf[0] = '\0';
    size_t pos = 0;
    
    for (int i = count - 1; i >= 0; i--) { /* Print highest cards first */
        char card_str[3];
        card_to_string(cards[i], card_str);
        
        size_t len = strlen(card_str);
        if (pos + len + 1 >= buf_size) break;
        
        if (pos > 0) {
            buf[pos++] = ' ';
        }
        strcpy(buf + pos, card_str);
        pos += len;
    }
    
    buf[pos] = '\0';
}
