/**
 * simple_evaluator.c - Simple Hand Evaluation without Lookup Table
 * 
 * This is a simpler, slower hand evaluator that doesn't require a 125MB table.
 * It's suitable for learning and smaller-scale training.
 * 
 * Algorithm:
 * 1. Categorize hand type (straight flush, quads, etc.)
 * 2. Rank within category by high cards
 * 
 * Returns rank 0-7462 (0 is best)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "evaluator.h"
#include "cards.h"

/* Rank values for comparison */
#define RANK_2 0
#define RANK_3 1
#define RANK_4 2
#define RANK_5 3
#define RANK_6 4
#define RANK_7 5
#define RANK_8 6
#define RANK_9 7
#define RANK_T 8
#define RANK_J 9
#define RANK_Q 10
#define RANK_K 11
#define RANK_A 12

/* Hand type boundaries */
#define BOUNDARY_STRAIGHT_FLUSH 10
#define BOUNDARY_QUADS 166
#define BOUNDARY_FULL_HOUSE 322
#define BOUNDARY_FLUSH 1599
#define BOUNDARY_STRAIGHT 1609
#define BOUNDARY_TRIPS 2467
#define BOUNDARY_TWO_PAIR 3325
#define BOUNDARY_PAIR 6185
#define BOUNDARY_HIGH_CARD 7462

/* Count bits in mask */
static int count_bits(int x) {
    int count = 0;
    while (x) {
        count++;
        x &= x - 1;
    }
    return count;
}

/* Check if rank mask has a straight, return high card or -1 */
static int has_straight(int rank_mask) {
    /* Check for wheel (A-5-4-3-2) */
    if ((rank_mask & 0x100F) == 0x100F) {
        return 3; /* 5-high straight */
    }
    
    /* Check for other straights */
    for (int i = 12; i >= 4; i--) {
        if ((rank_mask & (0x1F << (i - 4))) == (0x1F << (i - 4))) {
            return i;
        }
    }
    
    return -1;
}

/* Sort ranks descending */
static void sort_ranks_desc(int* ranks, int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if (ranks[i] < ranks[j]) {
                int tmp = ranks[i];
                ranks[i] = ranks[j];
                ranks[j] = tmp;
            }
        }
    }
}

/* Evaluate a 5-7 card hand */
int evaluate_hand(const Card* cards, int num_cards) {
    if (num_cards < 5 || num_cards > 7) return BOUNDARY_HIGH_CARD;
    
    int rank_counts[13] = {0};
    int suit_counts[4] = {0};
    int card_ranks[7];
    
    /* Count ranks and suits */
    for (int i = 0; i < num_cards; i++) {
        int r = card_rank(cards[i]);
        int s = card_suit(cards[i]);
        rank_counts[r]++;
        suit_counts[s]++;
        card_ranks[i] = r;
    }
    
    /* Find flush suit */
    int flush_suit = -1;
    for (int s = 0; s < 4; s++) {
        if (suit_counts[s] >= 5) {
            flush_suit = s;
            break;
        }
    }
    
    /* Build rank masks */
    int rank_mask = 0;
    int flush_rank_mask = 0;
    
    for (int i = 0; i < num_cards; i++) {
        int r = card_rank(cards[i]);
        rank_mask |= (1 << r);
        
        if (flush_suit >= 0 && card_suit(cards[i]) == flush_suit) {
            flush_rank_mask |= (1 << r);
        }
    }
    
    /* Check for straight flush */
    if (flush_suit >= 0) {
        int straight_high = has_straight(flush_rank_mask);
        if (straight_high >= 0) {
            if (straight_high == 12) {
                return 0; /* Royal flush */
            }
            return 1 + (12 - straight_high); /* Straight flush */
        }
    }
    
    /* Find quads, trips, pairs */
    int quads = -1, trips = -1;
    int pairs[2] = {-1, -1};
    int num_pairs = 0;
    
    for (int r = 12; r >= 0; r--) {
        if (rank_counts[r] == 4) {
            quads = r;
        } else if (rank_counts[r] == 3) {
            trips = r;
        } else if (rank_counts[r] == 2) {
            if (num_pairs < 2) {
                pairs[num_pairs++] = r;
            }
        }
    }
    
    /* Quads */
    if (quads >= 0) {
        /* Find best kicker */
        int kicker = 12;
        while (kicker == quads || rank_counts[kicker] == 0) kicker--;
        return 11 + quads * 12 + (12 - kicker);
    }
    
    /* Full house */
    if (trips >= 0 && num_pairs > 0) {
        return 167 + trips * 12 + (12 - pairs[0]);
    }
    
    /* Flush */
    if (flush_suit >= 0) {
        /* Get flush cards sorted */
        int flush_ranks[7], num_flush = 0;
        for (int i = 0; i < num_cards && num_flush < 5; i++) {
            if (card_suit(cards[i]) == flush_suit) {
                flush_ranks[num_flush++] = card_rank(cards[i]);
            }
        }
        sort_ranks_desc(flush_ranks, num_flush);
        
        /* Calculate flush rank */
        return 323 + flush_ranks[0] * 28561 + flush_ranks[1] * 2197 + 
               flush_ranks[2] * 169 + flush_ranks[3] * 13 + flush_ranks[4];
    }
    
    /* Straight */
    int straight_high = has_straight(rank_mask);
    if (straight_high >= 0) {
        return 1600 + (12 - straight_high);
    }
    
    /* Three of a kind */
    if (trips >= 0) {
        int kickers[2], num_kickers = 0;
        for (int r = 12; r >= 0 && num_kickers < 2; r--) {
            if (r != trips && rank_counts[r] > 0) {
                kickers[num_kickers++] = r;
            }
        }
        return 1609 + trips * 144 + kickers[0] * 12 + kickers[1];
    }
    
    /* Two pair */
    if (num_pairs >= 2) {
        /* Find kicker */
        int kicker = 12;
        while (rank_counts[kicker] == 0 || rank_counts[kicker] == 2) kicker--;
        return 2468 + pairs[0] * 156 + pairs[1] * 12 + kicker;
    }
    
    /* One pair */
    if (num_pairs == 1) {
        int kickers[3], num_kickers = 0;
        for (int r = 12; r >= 0 && num_kickers < 3; r--) {
            if (r != pairs[0] && rank_counts[r] > 0) {
                kickers[num_kickers++] = r;
            }
        }
        return 3326 + pairs[0] * 1728 + kickers[0] * 144 + 
               kickers[1] * 12 + kickers[2];
    }
    
    /* High card */
    sort_ranks_desc(card_ranks, num_cards);
    return 6186 + card_ranks[0] * 28561 + card_ranks[1] * 2197 + 
           card_ranks[2] * 169 + card_ranks[3] * 13 + card_ranks[4];
}

/* Evaluate from mask */
int evaluate_hand_mask(HandMask mask) {
    Card cards[MAX_HAND_CARDS];
    int num_cards = hand_mask_to_cards(mask, cards);
    return evaluate_hand(cards, num_cards);
}

/* Get hand type from rank */
HandType get_hand_type(int rank) {
    if (rank < 0) return HAND_HIGH_CARD;
    if (rank <= 0) return HAND_ROYAL_FLUSH;
    if (rank < BOUNDARY_STRAIGHT_FLUSH) return HAND_STRAIGHT_FLUSH;
    if (rank < BOUNDARY_QUADS) return HAND_FOUR_KIND;
    if (rank < BOUNDARY_FULL_HOUSE) return HAND_FULL_HOUSE;
    if (rank < BOUNDARY_FLUSH) return HAND_FLUSH;
    if (rank < BOUNDARY_STRAIGHT) return HAND_STRAIGHT;
    if (rank < BOUNDARY_TRIPS) return HAND_THREE_KIND;
    if (rank < BOUNDARY_TWO_PAIR) return HAND_TWO_PAIR;
    if (rank < BOUNDARY_PAIR) return HAND_ONE_PAIR;
    return HAND_HIGH_CARD;
}

/* Get hand name */
const char* get_hand_name(int rank) {
    static const char* names[] = {
        "Royal Flush",
        "Straight Flush",
        "Four of a Kind",
        "Full House",
        "Flush",
        "Straight",
        "Three of a Kind",
        "Two Pair",
        "One Pair",
        "High Card"
    };
    
    HandType type = get_hand_type(rank);
    if (type >= 0 && type <= 9) {
        return names[type];
    }
    return "Unknown";
}

/* Detailed evaluation */
EvalResult evaluate_hand_detailed(const Card* cards, int num_cards) {
    EvalResult result;
    result.rank = evaluate_hand(cards, num_cards);
    result.type = get_hand_type(result.rank);
    result.name = get_hand_name(result.rank);
    return result;
}

/* Compare two hands */
int compare_hands(const Card* hand1, int num1, const Card* hand2, int num2) {
    int rank1 = evaluate_hand(hand1, num1);
    int rank2 = evaluate_hand(hand2, num2);
    
    /* Lower rank is better */
    return rank2 - rank1;
}

/* Dummy functions for compatibility */
int evaluator_init(const char* table_path) {
    (void)table_path;
    /* No initialization needed for simple evaluator */
    return 0;
}

void evaluator_cleanup(void) {
    /* Nothing to clean up */
}
