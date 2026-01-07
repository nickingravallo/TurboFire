/*
 * Simulator.c - Monte Carlo poker hand simulator
 * 
 * Usage: ./Simulator [hand1] [hand2]
 *   e.g. ./Simulator AcAd KhKs
 *        ./Simulator 9h9d AcKs
 *
 * Card format: [rank][suit] where:
 *   rank: 2,3,4,5,6,7,8,9,T,J,Q,K,A
 *   suit: c,d,h,s (clubs, diamonds, hearts, spades)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* --------------------------------------------------------------------------
 * Tables
 * -------------------------------------------------------------------------- */

static const int PRIMES[13] = {2,3,5,7,11,13,17,19,23,29,31,37,41};

typedef struct { int32_t product; int16_t rank; } HRProd;

static int16_t *flush_tbl;
static int16_t *unique5_tbl;
static HRProd *prod_tbl;
static int num_prods;

static int load_tables(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int32_t hdr[4];
    if (fread(hdr, 4, 4, f) != 4 || hdr[0] != 0x48524E4B) { fclose(f); return 0; }
    flush_tbl = malloc(hdr[2] * 2);
    unique5_tbl = malloc(hdr[2] * 2);
    prod_tbl = malloc(hdr[3] * sizeof(HRProd));
    num_prods = hdr[3];
    fread(flush_tbl, 2, hdr[2], f);
    fread(unique5_tbl, 2, hdr[2], f);
    fread(prod_tbl, sizeof(HRProd), hdr[3], f);
    fclose(f);
    return 1;
}

static inline int bsearch_prod(int32_t prod) {
    int lo = 0, hi = num_prods - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int32_t mp = prod_tbl[mid].product;
        if (mp == prod) return prod_tbl[mid].rank;
        if (mp < prod) lo = mid + 1; else hi = mid - 1;
    }
    return 7462;
}

static inline int eval5(int c0, int c1, int c2, int c3, int c4) {
    int r0 = c0 >> 2, r1 = c1 >> 2, r2 = c2 >> 2, r3 = c3 >> 2, r4 = c4 >> 2;
    int s0 = c0 & 3, s1 = c1 & 3, s2 = c2 & 3, s3 = c3 & 3, s4 = c4 & 3;
    int bits = (1 << r0) | (1 << r1) | (1 << r2) | (1 << r3) | (1 << r4);

    if (s0 == s1 && s1 == s2 && s2 == s3 && s3 == s4) {
        return flush_tbl[bits];
    }

    int b = bits - ((bits >> 1) & 0x5555);
    b = (b & 0x3333) + ((b >> 2) & 0x3333);
    int unique = ((b + (b >> 4)) & 0xF0F) * 0x101 >> 8;

    if (unique == 5) return unique5_tbl[bits];

    return bsearch_prod((int32_t)PRIMES[r0] * PRIMES[r1] * PRIMES[r2] * PRIMES[r3] * PRIMES[r4]);
}

static inline int eval7(int c0, int c1, int c2, int c3, int c4, int c5, int c6) {
    int best = 9999, r;
    #define E(a,b,c,d,e) r = eval5(a,b,c,d,e); if (r < best) best = r
    E(c2,c3,c4,c5,c6); E(c1,c3,c4,c5,c6); E(c1,c2,c4,c5,c6);
    E(c1,c2,c3,c5,c6); E(c1,c2,c3,c4,c6); E(c1,c2,c3,c4,c5);
    E(c0,c3,c4,c5,c6); E(c0,c2,c4,c5,c6); E(c0,c2,c3,c5,c6);
    E(c0,c2,c3,c4,c6); E(c0,c2,c3,c4,c5); E(c0,c1,c4,c5,c6);
    E(c0,c1,c3,c5,c6); E(c0,c1,c3,c4,c6); E(c0,c1,c3,c4,c5);
    E(c0,c1,c2,c5,c6); E(c0,c1,c2,c4,c6); E(c0,c1,c2,c4,c5);
    E(c0,c1,c2,c3,c6); E(c0,c1,c2,c3,c5); E(c0,c1,c2,c3,c4);
    #undef E
    return best;
}

/* --------------------------------------------------------------------------
 * RNG (xorshift128+)
 * -------------------------------------------------------------------------- */

static uint64_t s0, s1;

static inline uint64_t rng(void) {
    uint64_t x = s0, y = s1;
    s0 = y;
    x ^= x << 23;
    s1 = x ^ y ^ (x >> 17) ^ (y >> 26);
    return s1 + y;
}

/* --------------------------------------------------------------------------
 * Card parsing
 * -------------------------------------------------------------------------- */

static const char *RANKS = "23456789TJQKA";
static const char *SUITS = "cdhs";

/* Parse a single card like "Ac" or "9h". Returns card (0-51) or -1 on error */
static int parse_card(const char *s) {
    if (!s || strlen(s) < 2) return -1;

    char rc = toupper(s[0]);
    char sc = tolower(s[1]);

    int rank = -1, suit = -1;

    for (int i = 0; i < 13; i++) {
        if (RANKS[i] == rc || (rc == '1' && s[1] == '0')) {  /* Handle "10" */
            rank = i;
            break;
        }
    }

    for (int i = 0; i < 4; i++) {
        if (SUITS[i] == sc) {
            suit = i;
            break;
        }
    }

    if (rank < 0 || suit < 0) return -1;
    return rank * 4 + suit;
}

/* Parse a hand like "AcAd" into two cards. Returns 0 on success. */
static int parse_hand(const char *s, int *c0, int *c1) {
    if (!s || strlen(s) < 4) return -1;

    char card1[3] = {s[0], s[1], '\0'};
    char card2[3] = {s[2], s[3], '\0'};

    *c0 = parse_card(card1);
    *c1 = parse_card(card2);

    if (*c0 < 0 || *c1 < 0 || *c0 == *c1) return -1;
    return 0;
}

/* Format card for display */
static void card_str(int card, char *out) {
    out[0] = RANKS[card >> 2];
    out[1] = SUITS[card & 3];
    out[2] = '\0';
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */

static void print_usage(const char *prog) {
    printf("Usage: %s [hand1] [hand2]\n", prog);
    printf("\nExamples:\n");
    printf("  %s AcAd KhKs    # Pocket Aces vs Pocket Kings\n", prog);
    printf("  %s AhKh QsQc    # AK suited vs Pocket Queens\n", prog);
    printf("  %s 9h9d AcKs    # Pocket Nines vs AK offsuit\n", prog);
    printf("\nCard format: [rank][suit]\n");
    printf("  Ranks: 2,3,4,5,6,7,8,9,T,J,Q,K,A\n");
    printf("  Suits: c,d,h,s (clubs, diamonds, hearts, spades)\n");
}

int main(int argc, char *argv[]) {
    /* Default hands: AA vs KK */
    int p0_c0 = 48, p0_c1 = 49;  /* Ac, Ad */
    int p1_c0 = 46, p1_c1 = 47;  /* Kh, Ks */

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    if (argc >= 3) {
        if (parse_hand(argv[1], &p0_c0, &p0_c1) != 0) {
            fprintf(stderr, "Error: Invalid hand1 '%s'\n", argv[1]);
            print_usage(argv[0]);
            return 1;
        }
        if (parse_hand(argv[2], &p1_c0, &p1_c1) != 0) {
            fprintf(stderr, "Error: Invalid hand2 '%s'\n", argv[2]);
            print_usage(argv[0]);
            return 1;
        }

        /* Check for duplicate cards */
        if (p0_c0 == p1_c0 || p0_c0 == p1_c1 || p0_c1 == p1_c0 || p0_c1 == p1_c1) {
            fprintf(stderr, "Error: Duplicate card in hands\n");
            return 1;
        }
    }

    printf("=== Monte Carlo Poker Simulator ===\n\n");

    if (!load_tables("output/handranks.dat") && !load_tables("handranks.dat")) {
        fprintf(stderr, "Error: Cannot load handranks.dat\n");
        fprintf(stderr, "Run 'make generate-handranks' first.\n");
        return 1;
    }

    /* RNG seed */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    s0 = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    s1 = s0 ^ 0xCAFEBABEDEADBEEFULL;

    /* Display matchup */
    char c0[3], c1[3], c2[3], c3[3];
    card_str(p0_c0, c0); card_str(p0_c1, c1);
    card_str(p1_c0, c2); card_str(p1_c1, c3);

    printf("Matchup:\n");
    printf("  Player 1: %s %s\n", c0, c1);
    printf("  Player 2: %s %s\n", c2, c3);
    printf("\nRunning for 5 seconds...\n\n");

    /* Build deck without dead cards */
    int deck[48], dn = 0;
    for (int c = 0; c < 52; c++) {
        if (c != p0_c0 && c != p0_c1 && c != p1_c0 && c != p1_c1) {
            deck[dn++] = c;
        }
    }

    long long wins = 0, losses = 0, ties = 0, total = 0;

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int d[48];
    for (int i = 0; i < 48; i++) d[i] = deck[i];

    while (1) {
        for (int iter = 0; iter < 50000; iter++) {
            /* Partial Fisher-Yates for 5 cards */
            for (int i = 0; i < 5; i++) {
                int j = i + (rng() % (48 - i));
                int tmp = d[i]; d[i] = d[j]; d[j] = tmp;
            }

            int r0 = eval7(p0_c0, p0_c1, d[0], d[1], d[2], d[3], d[4]);
            int r1 = eval7(p1_c0, p1_c1, d[0], d[1], d[2], d[3], d[4]);

            if (r0 < r1) wins++;
            else if (r0 > r1) losses++;
            else ties++;
        }
        total += 50000;

        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;

        if (elapsed >= 5.0) {
            printf("=== Results ===\n\n");
            printf("Simulations: %lld\n", total);
            printf("Time:        %.2f seconds\n", elapsed);
            printf("Speed:       %.2f million hands/sec\n\n", total / elapsed / 1e6);

            printf("%s%s wins: %.4f%%\n", c0, c1, 100.0 * wins / total);
            printf("%s%s wins: %.4f%%\n", c2, c3, 100.0 * losses / total);
            printf("Ties:        %.4f%%\n", 100.0 * ties / total);
            break;
        }
    }

    free(flush_tbl); free(unique5_tbl); free(prod_tbl);
    return 0;
}
