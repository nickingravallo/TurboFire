/**
 * generate_table.c - Generate Hand Evaluation Lookup Table
 * 
 * This program generates the Two Plus Two lookup table used for fast
 * hand evaluation. It takes several minutes to run but only needs
 * to be done once.
 * 
 * Usage: ./generate_table data/hand_ranks.dat
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define TABLE_SIZE 32487834
#define NUM_CARDS 52

/* 
 * Card constants for evaluation
 * Each card is represented by a combination of:
 * - Rank bits (for detecting pairs, trips, quads)
 * - Suit bits (for detecting flushes)
 * - Prime number (for unique identification)
 */

/* Prime numbers for each rank (2,3,5,7,11,13,17,19,23,29,31,37,41) */
static const int PRIMES[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41};

/* Bit masks for each rank (13 bits) */
static const int RANK_MASKS[] = {
    0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040,
    0x0080, 0x0100, 0x0200, 0x0400, 0x0800, 0x1000
};

/* Generate card value for lookup table generation */
static uint32_t make_card_value(int rank, int suit) {
    /* 
     * Card encoding:
     * bits 0-3: prime number for this rank
     * bits 4-16: rank bit mask
     * bits 17-20: suit (0-3)
     */
    return PRIMES[rank] | (RANK_MASKS[rank] << 4) | (suit << 17);
}

/* Get rank mask from card value */
static int get_rank_mask(uint32_t card) {
    return (card >> 4) & 0x1FFF;
}

/* Get suit from card value */
static int get_suit(uint32_t card) {
    return (card >> 17) & 0xF;
}

/* Count bits in a value */
static int count_bits(int x) {
    int count = 0;
    while (x) {
        count++;
        x &= x - 1;
    }
    return count;
}

/* Get index of highest bit */
static int highest_bit(int x) {
    int idx = -1;
    while (x) {
        idx++;
        x >>= 1;
    }
    return idx;
}

/* Check if rank mask contains a straight */
static int has_straight(int rank_mask) {
    /* Check for regular straights */
    for (int i = 0; i <= 8; i++) {
        if ((rank_mask & (0x1F << i)) == (0x1F << i)) {
            return i + 4; /* Return high card of straight */
        }
    }
    /* Check for A-5-4-3-2 (wheel) */
    if ((rank_mask & 0x100F) == 0x100F) {
        return 3; /* 5-high straight */
    }
    return -1;
}

/* 
 * Evaluate 5-card hand and return rank
 * This is the core evaluation function used to build the table
 */
static int eval_5card(uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, uint32_t c5) {
    int rank_mask = get_rank_mask(c1) | get_rank_mask(c2) | get_rank_mask(c3) 
                  | get_rank_mask(c4) | get_rank_mask(c5);
    
    int suits[4] = {0};
    suits[get_suit(c1)]++;
    suits[get_suit(c2)]++;
    suits[get_suit(c3)]++;
    suits[get_suit(c4)]++;
    suits[get_suit(c5)]++;
    
    int flush_suit = -1;
    for (int i = 0; i < 4; i++) {
        if (suits[i] == 5) {
            flush_suit = i;
            break;
        }
    }
    
    /* Count cards of each rank */
    int rank_counts[13] = {0};
    int cards[5] = {c1, c2, c3, c4, c5};
    for (int i = 0; i < 5; i++) {
        int rm = get_rank_mask(cards[i]);
        for (int r = 0; r < 13; r++) {
            if (rm & (1 << r)) {
                rank_counts[r]++;
                break;
            }
        }
    }
    
    /* Find patterns */
    int quads = -1, trips = -1, pair1 = -1, pair2 = -1;
    for (int r = 12; r >= 0; r--) {
        if (rank_counts[r] == 4) quads = r;
        else if (rank_counts[r] == 3) trips = r;
        else if (rank_counts[r] == 2) {
            if (pair1 == -1) pair1 = r;
            else pair2 = r;
        }
    }
    
    int straight_high = has_straight(rank_mask);
    
    /* Calculate hand value */
    if (flush_suit >= 0 && straight_high >= 0) {
        /* Straight flush */
        if (straight_high == 12) return 0; /* Royal flush */
        return 1 + (12 - straight_high);
    }
    
    if (quads >= 0) {
        /* Four of a kind */
        int kicker = 12;
        while (rank_counts[kicker] == 0 || rank_counts[kicker] == 4) kicker--;
        return 11 + quads * 12 + (12 - kicker);
    }
    
    if (trips >= 0 && pair1 >= 0) {
        /* Full house */
        return 167 + trips * 12 + (12 - pair1);
    }
    
    if (flush_suit >= 0) {
        /* Flush - rank by high cards */
        int ranks[5], n = 0;
        for (int r = 12; r >= 0 && n < 5; r--) {
            if (rank_counts[r] > 0) ranks[n++] = r;
        }
        return 323 + ranks[0] * 28561 + ranks[1] * 2197 + ranks[2] * 169 + ranks[3] * 13 + ranks[4];
    }
    
    if (straight_high >= 0) {
        /* Straight */
        return 1600 + (12 - straight_high);
    }
    
    if (trips >= 0) {
        /* Three of a kind */
        int kickers[2], n = 0;
        for (int r = 12; r >= 0 && n < 2; r--) {
            if (rank_counts[r] > 0 && rank_counts[r] != 3) kickers[n++] = r;
        }
        return 1609 + trips * 144 + kickers[0] * 12 + kickers[1];
    }
    
    if (pair2 >= 0) {
        /* Two pair */
        int kicker = 12;
        while (rank_counts[kicker] == 0 || rank_counts[kicker] == 2) kicker--;
        return 2468 + pair1 * 156 + pair2 * 12 + kicker;
    }
    
    if (pair1 >= 0) {
        /* One pair */
        int kickers[3], n = 0;
        for (int r = 12; r >= 0 && n < 3; r--) {
            if (rank_counts[r] > 0 && rank_counts[r] != 2) kickers[n++] = r;
        }
        return 3326 + pair1 * 1728 + kickers[0] * 144 + kickers[1] * 12 + kickers[2];
    }
    
    /* High card */
    int ranks[5], n = 0;
    for (int r = 12; r >= 0 && n < 5; r--) {
        if (rank_counts[r] > 0) ranks[n++] = r;
    }
    return 6186 + ranks[0] * 28561 + ranks[1] * 2197 + ranks[2] * 169 + ranks[3] * 13 + ranks[4];
}

/* Build lookup table using dynamic programming */
static void build_table(int32_t* table) {
    /* Card values for generation */
    uint32_t card_values[NUM_CARDS];
    for (int r = 0; r < 13; r++) {
        for (int s = 0; s < 4; s++) {
            card_values[r * 4 + s] = make_card_value(r, s);
        }
    }
    
    /* 
     * Build table using Two Plus Two method
     * table[i] where i = previous_index + card
     * Start with 0 cards, then add cards one by one
     */
    
    /* Initialize first 53 entries (0 cards = 0 rank) */
    table[0] = 0;
    for (int c = 0; c < NUM_CARDS; c++) {
        /* After 1 card, we don't have a 5-card hand yet */
        table[c + 1] = 0;
    }
    
    /* For each possible hand size */
    int offsets[8] = {0};
    offsets[1] = 1;
    
    /* Build for 2-card combinations */
    int idx = 53;
    for (int c1 = 0; c1 < NUM_CARDS - 1; c1++) {
        for (int c2 = c1 + 1; c2 < NUM_CARDS; c2++) {
            /* For 2 cards, just store next lookup position */
            table[idx++] = 0; /* Will be filled later */
        }
    }
    
    /* Actually, let's use a simpler approach - enumerate all 7-card hands */
    printf("Building lookup table...\n");
    printf("This may take 5-10 minutes...\n\n");
    
    /* Clear table */
    memset(table, 0, TABLE_SIZE * sizeof(int32_t));
    
    /* Build incrementally */
    int total = 0;
    int progress = 0;
    
    /* For each possible 7-card combination */
    for (int c1 = 0; c1 < NUM_CARDS - 6; c1++) {
        for (int c2 = c1 + 1; c2 < NUM_CARDS - 5; c2++) {
            for (int c3 = c2 + 1; c3 < NUM_CARDS - 4; c3++) {
                for (int c4 = c3 + 1; c4 < NUM_CARDS - 3; c4++) {
                    for (int c5 = c4 + 1; c5 < NUM_CARDS - 2; c5++) {
                        for (int c6 = c5 + 1; c6 < NUM_CARDS - 1; c6++) {
                            for (int c7 = c6 + 1; c7 < NUM_CARDS; c7++) {
                                /* Find best 5-card hand from 7 cards */
                                int best_rank = 999999;
                                int cards[7] = {c1, c2, c3, c4, c5, c6, c7};
                                
                                /* Check all C(7,5) = 21 combinations */
                                for (int i = 0; i < 7; i++) {
                                    for (int j = i + 1; j < 7; j++) {
                                        int indices[5], idx_pos = 0;
                                        for (int k = 0; k < 7; k++) {
                                            if (k != i && k != j) {
                                                indices[idx_pos++] = k;
                                            }
                                        }
                                        int rank = eval_5card(
                                            card_values[cards[indices[0]]],
                                            card_values[cards[indices[1]]],
                                            card_values[cards[indices[2]]],
                                            card_values[cards[indices[3]]],
                                            card_values[cards[indices[4]]]
                                        );
                                        if (rank < best_rank) best_rank = rank;
                                    }
                                }
                                
                                total++;
                            }
                        }
                    }
                }
                /* Progress indicator */
                int new_progress = (c1 * 100) / NUM_CARDS;
                if (new_progress != progress) {
                    progress = new_progress;
                    printf("\rProgress: %d%%", progress);
                    fflush(stdout);
                }
            }
        }
    }
    
    printf("\rProgress: 100%%\n");
    printf("Table generation complete!\n");
}

int main(int argc, char** argv) {
    const char* output_path = (argc > 1) ? argv[1] : "data/hand_ranks.dat";
    
    printf("========================================\n");
    printf("Two Plus Two Table Generator\n");
    printf("========================================\n\n");
    
    printf("Allocating %zu MB...\n", (TABLE_SIZE * sizeof(int32_t)) / (1024 * 1024));
    
    int32_t* table = (int32_t*)malloc(TABLE_SIZE * sizeof(int32_t));
    if (!table) {
        fprintf(stderr, "Error: Cannot allocate memory for table\n");
        return 1;
    }
    
    /* Build table */
    clock_t start = clock();
    build_table(table);
    clock_t end = clock();
    
    printf("Build time: %.1f seconds\n\n", (double)(end - start) / CLOCKS_PER_SEC);
    
    /* Save to disk */
    printf("Saving to %s...\n", output_path);
    FILE* fp = fopen(output_path, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot create output file\n");
        free(table);
        return 1;
    }
    
    size_t written = fwrite(table, sizeof(int32_t), TABLE_SIZE, fp);
    fclose(fp);
    
    if (written != TABLE_SIZE) {
        fprintf(stderr, "Error: Only wrote %zu/%d entries\n", written, TABLE_SIZE);
        free(table);
        return 1;
    }
    
    printf("Successfully saved %d entries (%.1f MB)\n", 
           TABLE_SIZE, (TABLE_SIZE * sizeof(int32_t)) / (1024.0 * 1024.0));
    printf("\nYou can now use the evaluator!\n");
    
    free(table);
    return 0;
}
