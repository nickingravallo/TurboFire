/*
 * RangeParser.c - Poker Hand Range Parser
 * 
 * Parses poker hand ranges in standard notation:
 *   Pairs: 22, 33, ..., AA
 *   Suited: A2s, K2s, ..., AAs
 *   Offsuit: A2o, K2o, ..., AAo
 *   Plus notation: 22+ means 22,33,44,...,AA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "RangeParser.h"

static const char *RANKS = "23456789TJQKA";

// Convert rank char to index (0-12)
static int rank_to_idx(char rank) {
    for (int i = 0; i < 13; i++) {
        if (RANKS[i] == toupper(rank)) return i;
    }
    return -1;
}

// Generate all hands for a pair (e.g., "22")
static void add_pair_hands(HandRange *range, int rank, double pct) {
    for (int s1 = 0; s1 < 4; s1++) {
        for (int s2 = s1 + 1; s2 < 4; s2++) {
            if (range->count >= 1326) {
                fprintf(stderr, "Warning: Hand range exceeds maximum capacity (1326)\n");
                return;
            }
            range->hands[range->count][0] = rank * 4 + s1;
            range->hands[range->count][1] = rank * 4 + s2;
            range->hand_percentages[range->count] = pct;
            range->count++;
        }
    }
}

// Generate all hands for suited (e.g., "A2s")
static void add_suited_hands(HandRange *range, int r1, int r2, double pct) {
    if (r1 == r2) return;  // Pairs handled separately
    for (int suit = 0; suit < 4; suit++) {
        if (range->count >= 1326) {
            fprintf(stderr, "Warning: Hand range exceeds maximum capacity (1326)\n");
            return;
        }
        range->hands[range->count][0] = r1 * 4 + suit;
        range->hands[range->count][1] = r2 * 4 + suit;
        range->hand_percentages[range->count] = pct;
        range->count++;
    }
}

// Generate all hands for offsuit (e.g., "A2o")
static void add_offsuit_hands(HandRange *range, int r1, int r2, double pct) {
    if (r1 == r2) return;  // Pairs handled separately
    for (int s1 = 0; s1 < 4; s1++) {
        for (int s2 = 0; s2 < 4; s2++) {
            if (s1 != s2) {
                if (range->count >= 1326) {
                    fprintf(stderr, "Warning: Hand range exceeds maximum capacity (1326)\n");
                    return;
                }
                range->hands[range->count][0] = r1 * 4 + s1;
                range->hands[range->count][1] = r2 * 4 + s2;
                range->hand_percentages[range->count] = pct;
                range->count++;
            }
        }
    }
}

// Parse a single hand specifier (e.g., "22", "A2s", "K2o", "22+", "A2s+", "A8s@50%")
static void parse_hand_spec(HandRange *range, const char *spec) {
    int len = strlen(spec);
    if (len < 2) return;
    
    // Extract percentage if present (e.g., "A8s@50%" or "22@50%")
    double pct = 1.0;
    char *spec_copy = strdup(spec);
    char *at_sign = strchr(spec_copy, '@');
    if (at_sign) {
        *at_sign = '\0';  // Terminate spec at @
        at_sign++;  // Move past @
        
        // Parse percentage (supports both "50%" and "0.5" formats)
        double pct_val = atof(at_sign);
        if (pct_val > 1.0 && pct_val <= 100.0) {
            pct = pct_val / 100.0;
        } else if (pct_val > 0.0 && pct_val <= 1.0) {
            pct = pct_val;
        }
    }
    
    int r1 = rank_to_idx(spec_copy[0]);
    int r2 = rank_to_idx(spec_copy[1]);
    
    if (r1 < 0 || r2 < 0) {
        free(spec_copy);
        return;
    }
    
    int spec_len = strlen(spec_copy);
    int is_plus = (spec_len > 2 && spec_copy[spec_len-1] == '+');
    int is_suited = (spec_len > 2 && tolower(spec_copy[spec_len-1]) == 's');
    int is_offsuit = (spec_len > 2 && tolower(spec_copy[spec_len-1]) == 'o');
    
    if (r1 == r2) {
        // Pair
        if (is_plus) {
            // Add all pairs from r1 to AA
            for (int r = r1; r < 13; r++) {
                add_pair_hands(range, r, pct);
            }
        } else {
            add_pair_hands(range, r1, pct);
        }
    } else {
        // Non-pair
        int high = (r1 > r2) ? r1 : r2;
        int low = (r1 < r2) ? r1 : r2;
        
        if (is_suited) {
            if (is_plus) {
                // Add all suited hands with this high card
                for (int r = 0; r < high; r++) {
                    add_suited_hands(range, high, r, pct);
                }
            } else {
                add_suited_hands(range, high, low, pct);
            }
        } else if (is_offsuit) {
            if (is_plus) {
                // Add all offsuit hands with this high card
                for (int r = 0; r < high; r++) {
                    add_offsuit_hands(range, high, r, pct);
                }
            } else {
                add_offsuit_hands(range, high, low, pct);
            }
        } else {
            // Default: add both suited and offsuit
            if (is_plus) {
                for (int r = 0; r < high; r++) {
                    add_suited_hands(range, high, r, pct);
                    add_offsuit_hands(range, high, r, pct);
                }
            } else {
                add_suited_hands(range, high, low, pct);
                add_offsuit_hands(range, high, low, pct);
            }
        }
    }
    
    free(spec_copy);
}

// Parse a range string (e.g., "22+,A2s+,K2o+" or "22+,A2s+@70%" or "A8s@50%,KJo@50%")
HandRange* parse_range(const char *range_str) {
    HandRange *range = calloc(1, sizeof(HandRange));
    range->percentage = 1.0;  // Default to 100%
    
    // Initialize all hand percentages to 1.0
    for (int i = 0; i < 1326; i++) {
        range->hand_percentages[i] = 1.0;
    }
    
    if (!range_str || strlen(range_str) == 0) {
        return range;
    }
    
    // Check for overall percentage at the end (e.g., "@70%" or "@0.7")
    // Only apply if there's no @ in individual hand specs (before last comma)
    char *str = strdup(range_str);
    char *last_at = strrchr(str, '@');
    
    // Only treat as overall percentage if it's at the very end and not part of a hand spec
    if (last_at) {
        // Check if there's a comma before this @ - if so, it's likely per-hand
        char *comma_before = strrchr(str, ',');
        if (!comma_before || comma_before > last_at) {
            // No comma before @ or @ is before last comma, could be overall
            // Check if there's a comma after @ - if so, it's definitely per-hand
            char *comma_after = strchr(last_at, ',');
            if (!comma_after) {
                // No comma after @, treat as overall percentage
                *last_at = '\0';  // Terminate range string at @
                last_at++;  // Move past @
                
                // Parse percentage (supports both "70%" and "0.7" formats)
                double pct = atof(last_at);
                if (pct > 1.0 && pct <= 100.0) {
                    // Percentage format (70% -> 0.7)
                    range->percentage = pct / 100.0;
                } else if (pct > 0.0 && pct <= 1.0) {
                    // Decimal format (0.7 -> 0.7)
                    range->percentage = pct;
                } else {
                    fprintf(stderr, "Warning: Invalid percentage '%s', using 100%%\n", last_at);
                    range->percentage = 1.0;
                }
            }
        }
    }
    
    // Split by comma and parse each spec
    char *token = strtok(str, ",");
    
    while (token) {
        // Trim whitespace
        while (*token == ' ') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') *end-- = '\0';
        
        parse_hand_spec(range, token);
        token = strtok(NULL, ",");
    }
    
    free(str);
    return range;
}

void free_range(HandRange *range) {
    free(range);
}

// Get hand category for grouping (exported)
const char* hand_category(int c0, int c1) {
    int r0 = c0 >> 2;
    int r1 = c1 >> 2;
    int s0 = c0 & 3;
    int s1 = c1 & 3;
    
    static char buf[16];
    
    if (r0 == r1) {
        // Pair
        snprintf(buf, sizeof(buf), "%c%c", RANKS[r0], RANKS[r0]);
        return buf;
    }
    
    int high = (r0 > r1) ? r0 : r1;
    int low = (r0 < r1) ? r0 : r1;
    
    if (s0 == s1) {
        snprintf(buf, sizeof(buf), "%c%cs", RANKS[high], RANKS[low]);
    } else {
        snprintf(buf, sizeof(buf), "%c%co", RANKS[high], RANKS[low]);
    }
    
    return buf;
}

// Print range summary
void print_range_summary(const HandRange *range) {
    printf("Range contains %d hands\n", range->count);
}
