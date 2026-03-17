#include "parse.h"

static inline int char_to_rank(char c) {
    if (c >= '2' && c <= '9') return c - '2';
    if (c == 'T') return 8;
    if (c == 'J') return 9;
    if (c == 'Q') return 10;
    if (c == 'K') return 11;
    if (c == 'A') return 12;
    return -1;
}

void add_combos_to_range(const char* hand_str, float weight, PlayerRange* range) {
    if (weight <= 0.0f) return;

    int r1 = char_to_rank(hand_str[0]);
    int r2 = char_to_rank(hand_str[1]);
    
    // Determine the format: Pair (length 2), Suited (ends in 's'), Offsuit (ends in 'o')
    int is_pair = (r1 == r2);
    int is_suited = (hand_str[2] == 's');
    int is_offsuit = (hand_str[2] == 'o' || hand_str[2] == '\0');

    // Suit offsets: 0=Spades, 1=Hearts, 2=Diamonds, 3=Clubs
    for (int suit1 = 0; suit1 < 4; suit1++) {
        for (int suit2 = 0; suit2 < 4; suit2++) {
            
            // Filter invalid suit combinations based on the hand type
            if (is_pair && suit1 >= suit2) continue; // Pairs need 2 diff suits, order doesn't matter (6 combos)
            if (is_suited && suit1 != suit2) continue; // Suited needs exactly same suit (4 combos)
            if (is_offsuit && !is_pair && suit1 == suit2) continue; // Offsuit needs different suits (12 combos)

           // Generate the cards using your 16-bit block offset
            uint64_t c1_mask = 1ULL << (r1 + (suit1 * 16));
            uint64_t c2_mask = 1ULL << (r2 + (suit2 * 16));
            uint64_t combined_mask = c1_mask | c2_mask;
            
            // --- NEW: Check for duplicates to sanitize dirty JSON ---
            int is_duplicate = 0;
            for (int i = 0; i < range->num_combos; i++) {
                if (range->combos[i].mask == combined_mask) {
                    is_duplicate = 1;
                    break;
                }
            }

            // Push to range ONLY if it's unique and we haven't hit the 1326 cap
            if (!is_duplicate && range->num_combos < 1326) {
                range->combos[range->num_combos].mask = combined_mask;
                range->combos[range->num_combos].weight = weight;
                range->num_combos++;
            }
        }
    }
}

void parse_json_range(const char* json_string, PlayerRange* out_range) {
    out_range->num_combos = 0;
    
    // Jump straight to the "hands" object to avoid parsing metadata
    const char *ptr = strstr(json_string, "\"hands\"");
    if (!ptr) return;

    ptr += 7;

    // Iterate through all key-value pairs in the dictionary
    while ((ptr = strchr(ptr, '"')) != NULL) {
        ptr++; // Skip opening quote
        
        // If we hit the end of the JSON or the hands object, stop parsing
        if (*ptr == '}' || *ptr == '\0') break;

        // Extract the hand string (e.g. "AKs")
        char hand_str[4] = {0};
        int i = 0;
        while (*ptr != '"' && i < 3) {
            hand_str[i++] = *ptr++;
        }
        
        ptr++; // Skip closing quote
        
        // Find the colon separating the key from the weight
        ptr = strchr(ptr, ':');
        if (ptr) {
            ptr++;
            // Extract the float weight and advance the pointer
            float weight = strtof(ptr, (char**)&ptr);
            
            // Expand into 64-bit combinations
            add_combos_to_range(hand_str, weight, out_range);
        }
    }
}

void print_range_grid(const PlayerRange* range) {
    // 13x13 grid to accumulate the weights
    float grid[13][13] = {0};

    // 1. Decode every combo in the range
    for (int i = 0; i < range->num_combos; i++) {
        uint64_t mask = range->combos[i].mask;
        float weight = range->combos[i].weight;

        int c1_idx = -1, c2_idx = -1;
        
        // Find the two set bits representing the cards
        for (int b = 0; b < 64; b++) {
            if (mask & (1ULL << b)) {
                if (c1_idx == -1) c1_idx = b;
                else c2_idx = b;
            }
        }

        if (c1_idx != -1 && c2_idx != -1) {
            // Extract Rank (0-12) and Suit (0-3) based on the 16-bit offset
            int r1 = c1_idx % 16;
            int s1 = c1_idx / 16;
            int r2 = c2_idx % 16;
            int s2 = c2_idx / 16;

            // Determine high/low ranks to map to the grid
            int rank_high = (r1 > r2) ? r1 : r2;
            int rank_low  = (r1 < r2) ? r1 : r2;

            // Convert rank (0=2, 12=A) to grid index (0=A, 12=2)
            int row_idx = 12 - rank_high;
            int col_idx = 12 - rank_low;

            // Map to the correct quadrant
            if (s1 == s2) {
                grid[row_idx][col_idx] += weight; // Suited (Upper Right)
            } else if (r1 == r2) {
                grid[row_idx][row_idx] += weight; // Pair (Diagonal)
            } else {
                grid[col_idx][row_idx] += weight; // Offsuit (Lower Left)
            }
        }
    }

    // 2. Print the Grid
    const char rank_chars[] = "AKQJT98765432";
    
    printf("\n    ");
    for (int i = 0; i < 13; i++) {
        printf("  %c  ", rank_chars[i]);
    }
    printf("\n");

    for (int r = 0; r < 13; r++) {
        printf("%c | ", rank_chars[r]);
        for (int c = 0; c < 13; c++) {
            
            // Max combinations per hand type
            float max_weight = (r == c) ? 6.0f : (r < c ? 4.0f : 12.0f);
            
            // Calculate percentage
            float pct = (grid[r][c] / max_weight) * 100.0f;

            if (pct > 0.0f) {
                printf("%3.0f%% ", pct);
            } else {
                printf("  -  ");
            }
        }
        printf("\n");
    }
    printf("\n");
}
