/*
 * HandRankGen.c - Poker Hand Rank Table Generator (Refined)
 *
 * Generates lookup tables with correct hand ordering.
 * Each of the 7462 distinct hand types gets a unique rank.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

static const int PRIMES[13] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41};

#define BITMASK_SIZE 8192

/* --------------------------------------------------------------------------
 * Tables
 * -------------------------------------------------------------------------- */

static int16_t flush_table[BITMASK_SIZE];
static int16_t unique5_table[BITMASK_SIZE];

typedef struct {
    int32_t product;
    int16_t rank;
} ProductEntry;

static ProductEntry *product_table = NULL;
static int num_products = 0;

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static int is_straight(int bits) {
    for (int high = 12; high >= 4; high--) {
        int mask = 0x1F << (high - 4);
        if ((bits & mask) == mask) return 1;
    }
    if ((bits & 0x100F) == 0x100F) return 1;  /* Wheel A-5-4-3-2 */
    return 0;
}

static int straight_high(int bits) {
    for (int high = 12; high >= 4; high--) {
        int mask = 0x1F << (high - 4);
        if ((bits & mask) == mask) return high;
    }
    if ((bits & 0x100F) == 0x100F) return 3;  /* Wheel = 5-high, rank index 3 */
    return -1;
}

static int32_t prime_prod(int r0, int r1, int r2, int r3, int r4) {
    return (int32_t)PRIMES[r0] * PRIMES[r1] * PRIMES[r2] * PRIMES[r3] * PRIMES[r4];
}

/* --------------------------------------------------------------------------
 * Hand Storage
 * -------------------------------------------------------------------------- */

typedef struct {
    int32_t product;
    int rank_bits;
    int is_flush;  /* 1 if flush/straight-flush, 0 otherwise */
} Hand;

static Hand *hands = NULL;
static int hand_count = 0;
static int hand_cap = 0;

static void add_hand(int32_t prod, int bits, int is_flush) {
    if (hand_count >= hand_cap) {
        hand_cap = hand_cap ? hand_cap * 2 : 8192;
        hands = realloc(hands, hand_cap * sizeof(Hand));
    }
    hands[hand_count].product = prod;
    hands[hand_count].rank_bits = bits;
    hands[hand_count].is_flush = is_flush;
    hand_count++;
}

/* --------------------------------------------------------------------------
 * Generate Hands in Exact Rank Order (Best to Worst)
 * -------------------------------------------------------------------------- */

static void generate_all_hands(void) {
    printf("Generating 7462 hand types in exact rank order...\n");

    /* ===== STRAIGHT FLUSHES (10): Royal down to Wheel ===== */
    /* A-high (royal) down to 6-high, then wheel */
    for (int high = 12; high >= 4; high--) {
        int bits = 0x1F << (high - 4);
        int32_t prod = 1;
        for (int r = 0; r < 13; r++) if (bits & (1 << r)) prod *= PRIMES[r];
        add_hand(prod, bits, 1);
    }
    /* Wheel (5-high): A-5-4-3-2 */
    {
        int bits = (1 << 12) | 0xF;
        add_hand(prime_prod(12, 3, 2, 1, 0), bits, 1);
    }
    printf("  Straight flushes: %d (ranks 1-10)\n", hand_count);

    /* ===== FOUR OF A KIND (156): Quad rank high to low, kicker high to low ===== */
    for (int q = 12; q >= 0; q--) {
        for (int k = 12; k >= 0; k--) {
            if (k == q) continue;
            int32_t prod = (int32_t)PRIMES[q]*PRIMES[q]*PRIMES[q]*PRIMES[q]*PRIMES[k];
            add_hand(prod, (1 << q) | (1 << k), 0);
        }
    }
    printf("  Four of a kind: %d (ranks 11-166)\n", hand_count - 10);

    /* ===== FULL HOUSE (156): Trips high to low, pair high to low ===== */
    for (int t = 12; t >= 0; t--) {
        for (int p = 12; p >= 0; p--) {
            if (p == t) continue;
            int32_t prod = (int32_t)PRIMES[t]*PRIMES[t]*PRIMES[t]*PRIMES[p]*PRIMES[p];
            add_hand(prod, (1 << t) | (1 << p), 0);
        }
    }
    printf("  Full houses: %d (ranks 167-322)\n", hand_count - 166);

    /* ===== FLUSH (1277): All non-straight 5-card combos, ordered by ranks ===== */
    /* Iterate from highest (A-K-Q-J-9) down to lowest (7-5-4-3-2) */
    for (int r0 = 12; r0 >= 4; r0--) {
        for (int r1 = r0 - 1; r1 >= 3; r1--) {
            for (int r2 = r1 - 1; r2 >= 2; r2--) {
                for (int r3 = r2 - 1; r3 >= 1; r3--) {
                    for (int r4 = r3 - 1; r4 >= 0; r4--) {
                        int bits = (1<<r0)|(1<<r1)|(1<<r2)|(1<<r3)|(1<<r4);
                        if (is_straight(bits)) continue;
                        add_hand(prime_prod(r0, r1, r2, r3, r4), bits, 1);
                    }
                }
            }
        }
    }
    printf("  Flushes: %d (ranks 323-1599)\n", hand_count - 322);

    /* ===== STRAIGHT (10): A-high down to wheel ===== */
    for (int high = 12; high >= 4; high--) {
        int bits = 0x1F << (high - 4);
        int32_t prod = 1;
        for (int r = 0; r < 13; r++) if (bits & (1 << r)) prod *= PRIMES[r];
        add_hand(prod, bits, 0);
    }
    /* Wheel */
    {
        int bits = (1 << 12) | 0xF;
        add_hand(prime_prod(12, 3, 2, 1, 0), bits, 0);
    }
    printf("  Straights: %d (ranks 1600-1609)\n", hand_count - 1599);

    /* ===== THREE OF A KIND (858): Trips high to low, kickers high to low ===== */
    for (int t = 12; t >= 0; t--) {
        for (int k1 = 12; k1 >= 0; k1--) {
            if (k1 == t) continue;
            for (int k2 = k1 - 1; k2 >= 0; k2--) {
                if (k2 == t) continue;
                int32_t prod = (int32_t)PRIMES[t]*PRIMES[t]*PRIMES[t]*PRIMES[k1]*PRIMES[k2];
                add_hand(prod, (1<<t)|(1<<k1)|(1<<k2), 0);
            }
        }
    }
    printf("  Three of a kind: %d (ranks 1610-2467)\n", hand_count - 1609);

    /* ===== TWO PAIR (858): High pair, low pair, kicker - all high to low ===== */
    for (int p1 = 12; p1 >= 1; p1--) {
        for (int p2 = p1 - 1; p2 >= 0; p2--) {
            for (int k = 12; k >= 0; k--) {
                if (k == p1 || k == p2) continue;
                int32_t prod = (int32_t)PRIMES[p1]*PRIMES[p1]*PRIMES[p2]*PRIMES[p2]*PRIMES[k];
                add_hand(prod, (1<<p1)|(1<<p2)|(1<<k), 0);
            }
        }
    }
    printf("  Two pair: %d (ranks 2468-3325)\n", hand_count - 2467);

    /* ===== ONE PAIR (2860): Pair rank, then kickers high to low ===== */
    for (int p = 12; p >= 0; p--) {
        for (int k1 = 12; k1 >= 0; k1--) {
            if (k1 == p) continue;
            for (int k2 = k1 - 1; k2 >= 0; k2--) {
                if (k2 == p) continue;
                for (int k3 = k2 - 1; k3 >= 0; k3--) {
                    if (k3 == p) continue;
                    int32_t prod = (int32_t)PRIMES[p]*PRIMES[p]*PRIMES[k1]*PRIMES[k2]*PRIMES[k3];
                    add_hand(prod, (1<<p)|(1<<k1)|(1<<k2)|(1<<k3), 0);
                }
            }
        }
    }
    printf("  One pair: %d (ranks 3326-6185)\n", hand_count - 3325);

    /* ===== HIGH CARD (1277): Same as flush but non-flush ===== */
    for (int r0 = 12; r0 >= 4; r0--) {
        for (int r1 = r0 - 1; r1 >= 3; r1--) {
            for (int r2 = r1 - 1; r2 >= 2; r2--) {
                for (int r3 = r2 - 1; r3 >= 1; r3--) {
                    for (int r4 = r3 - 1; r4 >= 0; r4--) {
                        int bits = (1<<r0)|(1<<r1)|(1<<r2)|(1<<r3)|(1<<r4);
                        if (is_straight(bits)) continue;
                        add_hand(prime_prod(r0, r1, r2, r3, r4), bits, 0);
                    }
                }
            }
        }
    }
    printf("  High card: %d (ranks 6186-7462)\n", hand_count - 6185);

    printf("  TOTAL: %d\n", hand_count);
}

/* --------------------------------------------------------------------------
 * Build Tables
 * -------------------------------------------------------------------------- */

static int cmp_products(const void *a, const void *b) {
    const ProductEntry *pa = a, *pb = b;
    return (pa->product > pb->product) - (pa->product < pb->product);
}

static void build_tables(void) {
    printf("\nBuilding lookup tables...\n");

    memset(flush_table, 0, sizeof(flush_table));
    memset(unique5_table, 0, sizeof(unique5_table));

    /* Count product entries (non-flush, non-unique5) */
    int prod_count = 0;
    for (int i = 0; i < hand_count; i++) {
        int bits = hands[i].rank_bits;
        int pop = 0;
        for (int b = bits; b; b >>= 1) pop += b & 1;
        if (!hands[i].is_flush && pop != 5) {
            prod_count++;
        }
    }

    product_table = malloc(prod_count * sizeof(ProductEntry));
    num_products = 0;

    for (int i = 0; i < hand_count; i++) {
        int16_t rank = i + 1;  /* Rank 1 = best */
        int bits = hands[i].rank_bits;
        int32_t prod = hands[i].product;

        /* Count unique ranks */
        int pop = 0;
        for (int b = bits; b; b >>= 1) pop += b & 1;

        if (hands[i].is_flush) {
            /* Flush or straight flush: use flush table */
            flush_table[bits] = rank;
        } else if (pop == 5) {
            /* 5 unique ranks (straight or high card): use unique5 table */
            unique5_table[bits] = rank;
        } else {
            /* Pairs/trips/quads: use product table */
            product_table[num_products].product = prod;
            product_table[num_products].rank = rank;
            num_products++;
        }
    }

    /* Sort product table for binary search */
    qsort(product_table, num_products, sizeof(ProductEntry), cmp_products);

    printf("  Flush table: %zu bytes\n", sizeof(flush_table));
    printf("  Unique5 table: %zu bytes\n", sizeof(unique5_table));
    printf("  Product table: %d entries (%zu bytes)\n", num_products, 
           num_products * sizeof(ProductEntry));
}

/* --------------------------------------------------------------------------
 * Verification
 * -------------------------------------------------------------------------- */

static int find_product_rank(int32_t prod) {
    int lo = 0, hi = num_products - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (product_table[mid].product == prod) return product_table[mid].rank;
        if (product_table[mid].product < prod) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static void verify(void) {
    printf("\nVerifying...\n");
    int errors = 0;

    /* Royal flush */
    int royal_bits = (1<<12)|(1<<11)|(1<<10)|(1<<9)|(1<<8);
    if (flush_table[royal_bits] != 1) {
        printf("  ERROR: Royal flush = %d, expected 1\n", flush_table[royal_bits]);
        errors++;
    } else {
        printf("  Royal flush: rank %d ✓\n", flush_table[royal_bits]);
    }

    /* Steel wheel (5-high straight flush) */
    int wheel_bits = (1<<12)|(1<<3)|(1<<2)|(1<<1)|(1<<0);
    if (flush_table[wheel_bits] != 10) {
        printf("  ERROR: Steel wheel = %d, expected 10\n", flush_table[wheel_bits]);
        errors++;
    } else {
        printf("  Steel wheel: rank %d ✓\n", flush_table[wheel_bits]);
    }

    /* Quad aces with king kicker: AAAA-K */
    int32_t qak_prod = (int32_t)41*41*41*41*37;
    int qak_rank = find_product_rank(qak_prod);
    if (qak_rank != 11) {
        printf("  ERROR: AAAA-K = %d, expected 11\n", qak_rank);
        errors++;
    } else {
        printf("  Quad Aces + K: rank %d ✓\n", qak_rank);
    }

    /* Quad aces with queen kicker: AAAA-Q */
    int32_t qaq_prod = (int32_t)41*41*41*41*31;
    int qaq_rank = find_product_rank(qaq_prod);
    if (qaq_rank != 12) {
        printf("  ERROR: AAAA-Q = %d, expected 12\n", qaq_rank);
        errors++;
    } else {
        printf("  Quad Aces + Q: rank %d ✓\n", qaq_rank);
    }

    /* Ace-high straight (broadway) */
    if (unique5_table[royal_bits] != 1600) {
        printf("  ERROR: Broadway straight = %d, expected 1600\n", unique5_table[royal_bits]);
        errors++;
    } else {
        printf("  Broadway straight: rank %d ✓\n", unique5_table[royal_bits]);
    }

    /* Wheel straight (not flush) */
    if (unique5_table[wheel_bits] != 1609) {
        printf("  ERROR: Wheel straight = %d, expected 1609\n", unique5_table[wheel_bits]);
        errors++;
    } else {
        printf("  Wheel straight: rank %d ✓\n", unique5_table[wheel_bits]);
    }

    /* Worst high card: 7-5-4-3-2 = ranks 5,3,2,1,0 */
    int worst_bits = (1<<5)|(1<<3)|(1<<2)|(1<<1)|(1<<0);
    if (unique5_table[worst_bits] != 7462) {
        printf("  ERROR: 75432 high = %d, expected 7462\n", unique5_table[worst_bits]);
        errors++;
    } else {
        printf("  75432 (worst): rank %d ✓\n", unique5_table[worst_bits]);
    }

    /* Pair of aces with K-Q-J kickers: AA-K-Q-J */
    int32_t aakqj_prod = (int32_t)41*41*37*31*29;
    int aakqj_rank = find_product_rank(aakqj_prod);
    if (aakqj_rank != 3326) {
        printf("  ERROR: AA-KQJ = %d, expected 3326\n", aakqj_rank);
        errors++;
    } else {
        printf("  AA-KQJ (best pair): rank %d ✓\n", aakqj_rank);
    }

    /* Full house: AAA-KK */
    int32_t aaakk_prod = (int32_t)41*41*41*37*37;
    int aaakk_rank = find_product_rank(aaakk_prod);
    if (aaakk_rank != 167) {
        printf("  ERROR: AAA-KK = %d, expected 167\n", aaakk_rank);
        errors++;
    } else {
        printf("  AAA-KK (best FH): rank %d ✓\n", aaakk_rank);
    }

    if (errors) {
        printf("\n  %d errors found!\n", errors);
    } else {
        printf("\n  All checks passed!\n");
    }
}

/* --------------------------------------------------------------------------
 * Output
 * -------------------------------------------------------------------------- */

static void write_binary(const char *path) {
    printf("\nWriting %s...\n", path);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot create %s\n", path); return; }

    int32_t hdr[4] = {0x48524E4B, 3, BITMASK_SIZE, num_products};
    fwrite(hdr, 4, 4, f);
    fwrite(flush_table, sizeof(int16_t), BITMASK_SIZE, f);
    fwrite(unique5_table, sizeof(int16_t), BITMASK_SIZE, f);
    fwrite(product_table, sizeof(ProductEntry), num_products, f);
    fclose(f);

    long bytes = 16 + 2*BITMASK_SIZE*2 + num_products*sizeof(ProductEntry);
    printf("  Wrote %.2f KB\n", bytes / 1024.0);
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */

int main(void) {
    printf("HandRankGen - Refined Poker Hand Evaluator\n\n");

    generate_all_hands();
    build_tables();
    verify();
    write_binary("handranks.dat");

    printf("\nDone!\n");

    free(hands);
    free(product_table);
    return 0;
}
