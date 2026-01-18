/*
 * TurboFire.c - Poker GTO Solver Terminal Application (Heads-Up SB vs BB)
 * 
 * Usage: ./TurboFire [SB_range] [BB_range] [board]
 *   e.g. ./TurboFire "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,82o+,72o+,62o+,52o+,42o+,32o" "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,82o+,72o+,62o+,52o+,42o+,32o"
 *
 * Range format:
 *   Pairs: 22, 33, ..., AA or 22+ (all pairs from 22 to AA)
 *   Suited: A2s, K2s, ..., AAs or A2s+ (all suited hands with A high)
 *   Offsuit: A2o, K2o, ..., AAo or A2o+ (all offsuit hands with A high)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include "HandRanks.h"
#include "MCCFR.h"
#include "RangeParser.h"
#include "GUI.h"

/* --------------------------------------------------------------------------
 * Card parsing
 * -------------------------------------------------------------------------- */

static const char *RANKS = "23456789TJQKA";
static const char *SUITS = "cdhs";

/* Parse board cards */
static int parse_board(const char *s, int *board, int max_cards) {
    if (!s) return 0;
    
    int len = strlen(s);
    if (len < 2) return 0;
    
    int count = 0;
    for (int i = 0; i < len && count < max_cards; i += 2) {
        if (i + 1 >= len) break;
        char card_str[3] = {s[i], s[i+1], '\0'};
        
        int rank = -1, suit = -1;
        char rc = toupper(s[i]);
        char sc = tolower(s[i+1]);
        
        for (int j = 0; j < 13; j++) {
            if (RANKS[j] == rc) { rank = j; break; }
        }
        for (int j = 0; j < 4; j++) {
            if (SUITS[j] == sc) { suit = j; break; }
        }
        
        if (rank >= 0 && suit >= 0) {
            board[count++] = rank * 4 + suit;
        } else {
            return -1;
        }
    }
    
    return count;
}

/* Format card for display */
static void card_str(int card, char *out) {
    if (card < 0 || card >= 52) {
        out[0] = '\0';
        return;
    }
    out[0] = RANKS[card >> 2];
    out[1] = SUITS[card & 3];
    out[2] = '\0';
}

/* Check if handranks.dat exists */
static int check_handranks(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

/* Generate handranks if needed */
static int ensure_handranks(void) {
    if (check_handranks("output/handranks.dat") || check_handranks("handranks.dat")) {
        return 1;
    }
    
    printf("HandRanks not found. Generating...\n");
    printf("Running HandRankGen...\n");
    
    int ret = system("output/HandRankGen > /dev/null 2>&1");
    if (ret != 0) {
        system("make handrank-gen > /dev/null 2>&1");
        ret = system("output/HandRankGen > /dev/null 2>&1");
    }
    
    if (check_handranks("handranks.dat")) {
        system("mv handranks.dat output/ 2>/dev/null");
    }
    
    return check_handranks("output/handranks.dat") || check_handranks("handranks.dat");
}

/* Strategy aggregation by hand category */
typedef struct {
    char category[16];
    double strategy_sum[3];  // Sum of strategies for each action
    int count;  // Number of hands in this category
} CategoryStrategy;

static CategoryStrategy categories[200];
static int num_categories = 0;

static CategoryStrategy* get_or_create_category(const char *cat) {
    for (int i = 0; i < num_categories; i++) {
        if (strcmp(categories[i].category, cat) == 0) {
            return &categories[i];
        }
    }
    
    if (num_categories < 200) {
        strncpy(categories[num_categories].category, cat, 15);
        categories[num_categories].category[15] = '\0';
        memset(categories[num_categories].strategy_sum, 0, sizeof(categories[num_categories].strategy_sum));
        categories[num_categories].count = 0;
        return &categories[num_categories++];
    }
    
    return NULL;
}

/* Check if cards overlap */
static int cards_overlap(int c0, int c1, int c2, int c3, int *board, int board_size) {
    int cards[9] = {c0, c1, c2, c3};
    for (int i = 0; i < board_size && i < 5; i++) {
        cards[4 + i] = board[i];
    }
    
    int total_cards = 4 + board_size;
    for (int i = 0; i < total_cards; i++) {
        for (int j = i + 1; j < total_cards; j++) {
            if (cards[i] == cards[j] && cards[i] >= 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* Generate random board cards that don't overlap with player hands */
static void generate_random_board(int sb_c0, int sb_c1, int bb_c0, int bb_c1, int *board, Street street) {
    // Build deck excluding player hands
    int deck[48], deck_size = 0;
    int used_cards[4] = {sb_c0, sb_c1, bb_c0, bb_c1};
    
    for (int c = 0; c < 52; c++) {
        int is_used = 0;
        for (int i = 0; i < 4; i++) {
            if (c == used_cards[i]) {
                is_used = 1;
                break;
            }
        }
        if (!is_used) {
            deck[deck_size++] = c;
        }
    }
    
    // Shuffle deck (simple Fisher-Yates)
    static unsigned int seed = 0;
    if (seed == 0) {
        seed = (unsigned int)time(NULL);
        srand(seed);
    }
    
    for (int i = deck_size - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }
    
    // Deal board cards based on street
    int cards_needed = (street == STREET_FLOP) ? 3 : (street == STREET_TURN) ? 4 : 5;
    for (int i = 0; i < 5; i++) {
        if (i < cards_needed) {
            board[i] = deck[i];
        } else {
            board[i] = -1;
        }
    }
}

static void print_usage(const char *prog) {
    printf("Usage: %s [SB_range] [BB_range] [board] [--gui]\n", prog);
    printf("\nHeads-Up Poker Ranges:\n");
    printf("  SB (Button): Very wide opening range (~80%%+)\n");
    printf("  BB: Wider defending range (~50-60%%, not premium-heavy)\n");
    printf("\nExamples:\n");
    printf("  %s \"22+,A2s+,K2o+\" \"22+,A2s+,K2s+\"     # Realistic HU ranges\n", prog);
    printf("  %s \"22+,A2s+,K2o+\" \"22+,A2s+,K2s+\" AcKdQh    # With flop\n", prog);
    printf("  %s \"22+,A2s+\" \"22+,A2s+\" --gui          # Launch GUI\n", prog);
    printf("\nRange format:\n");
    printf("  Pairs: 22, 33, ..., AA or 22+ (all pairs from 22 to AA)\n");
    printf("  Suited: A2s, K2s, ..., AAs or A2s+ (all suited with high card)\n");
    printf("  Offsuit: A2o, K2o, ..., AAo or A2o+ (all offsuit with high card)\n");
    printf("  Combine with commas: \"22+,A2s+,K2o+\"\n");
    printf("  Add percentage: \"22+,A2s+@70%%\" (opens 70%% of the time)\n");
    printf("  Percentage can be 0-100%% or 0.0-1.0 (e.g., @70%% or @0.7)\n");
    printf("\nGUI:\n");
    printf("  Use --gui or -g flag to launch graphical interface\n");
    printf("  GUI shows color-coded strategy: Blue=Check/Call, Green=Bet/Raise, Red=Fold\n");
}

int main(int argc, char *argv[]) {
    printf("=== TurboFire Poker GTO Solver (Heads-Up SB vs BB) ===\n\n");
    
    // Check for GUI flag
    int use_gui = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gui") == 0 || strcmp(argv[i], "-g") == 0) {
            use_gui = 1;
            // Remove flag from argv by shifting
            for (int j = i; j < argc - 1; j++) {
                argv[j] = argv[j + 1];
            }
            argc--;
            break;
        }
    }
    
    // Initialize GUI if requested
    if (use_gui) {
        if (!gui_init()) {
            fprintf(stderr, "Warning: GUI initialization failed. Continuing with terminal output only.\n");
            use_gui = 0;
        }
    }
    
    // Default ranges: Realistic HU poker ranges
    // SB (Button): Very wide opening range (~80%+)
    const char *sb_range_str = "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,82o+,72o+,62o+,52o+,42o+,32o";
    // BB: Wider defending range but not premium-heavy (~50-60%)
    const char *bb_range_str = "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,62s+,52s+,42s+,32s,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+,82o+,72o+,62o+,52o+,42o+,32o";
    
    int board[5] = {-1, -1, -1, -1, -1};
    int board_size = 0;
    
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }
    
    // Parse ranges
    if (argc >= 3) {
        sb_range_str = argv[1];
        bb_range_str = argv[2];
        
        // Parse board if provided
        if (argc >= 4) {
            board_size = parse_board(argv[3], board, 5);
            if (board_size < 0) {
                fprintf(stderr, "Error: Invalid board '%s'\n", argv[3]);
                return 1;
            }
        }
    }
    
    // Ensure handranks exist
    if (!ensure_handranks()) {
        fprintf(stderr, "Error: Cannot generate or find handranks.dat\n");
        return 1;
    }
    
    // Load hand rank tables
    HandRankTables *hr = hr_load("output/handranks.dat");
    if (!hr) {
        hr = hr_load("handranks.dat");
    }
    if (!hr) {
        fprintf(stderr, "Error: Cannot load handranks.dat\n");
        return 1;
    }
    
    // Parse ranges
    HandRange *sb_range = parse_range(sb_range_str);
    HandRange *bb_range = parse_range(bb_range_str);
    
    // Set ranges in GUI if enabled
    if (use_gui) {
        gui_set_ranges(sb_range_str, bb_range_str);
    }
    
    printf("SB Range: %s\n", sb_range_str);
    printf("  Hands: %d", sb_range->count);
    if (sb_range->percentage < 1.0) {
        printf(" (Opened %.1f%% of the time)", sb_range->percentage * 100.0);
    }
    printf("\n");
    printf("BB Range: %s\n", bb_range_str);
    printf("  Hands: %d", bb_range->count);
    if (bb_range->percentage < 1.0) {
        printf(" (Defended %.1f%% of the time)", bb_range->percentage * 100.0);
    }
    printf("\n");
    
    if (board_size > 0) {
        printf("Board: ");
        for (int i = 0; i < board_size; i++) {
            char bc[3];
            card_str(board[i], bc);
            printf("%s ", bc);
        }
        printf("\n");
    }
    printf("\n");
    
    // Determine which streets to analyze
    Street streets_to_analyze[3];
    int num_streets = 0;
    const char *street_names[] = {"Flop", "Turn", "River"};
    
    if (board_size == 0) {
        // Preflop - analyze flop, turn, river
        streets_to_analyze[0] = STREET_FLOP;
        streets_to_analyze[1] = STREET_TURN;
        streets_to_analyze[2] = STREET_RIVER;
        num_streets = 3;
    } else if (board_size == 3) {
        // Flop - analyze turn and river
        streets_to_analyze[0] = STREET_TURN;
        streets_to_analyze[1] = STREET_RIVER;
        num_streets = 2;
    } else if (board_size == 4) {
        // Turn - analyze river
        streets_to_analyze[0] = STREET_RIVER;
        num_streets = 1;
    } else if (board_size == 5) {
        printf("River is terminal - no strategy needed.\n");
        hr_free(hr);
        free_range(sb_range);
        free_range(bb_range);
        return 0;
    }
    
    // First, collect all unique hand categories from SB range
    printf("Collecting unique hand types from SB range...\n");
    CategoryStrategy all_categories[200];
    int num_all_categories = 0;
    
    for (int sb_idx = 0; sb_idx < sb_range->count; sb_idx++) {
        int sb_c0 = sb_range->hands[sb_idx][0];
        int sb_c1 = sb_range->hands[sb_idx][1];
        const char *cat = hand_category(sb_c0, sb_c1);
        
        // Check if category already exists
        int found = 0;
        for (int i = 0; i < num_all_categories; i++) {
            if (strcmp(all_categories[i].category, cat) == 0) {
                found = 1;
                break;
            }
        }
        
        if (!found && num_all_categories < 200) {
            strncpy(all_categories[num_all_categories].category, cat, 15);
            all_categories[num_all_categories].category[15] = '\0';
            memset(all_categories[num_all_categories].strategy_sum, 0, sizeof(all_categories[num_all_categories].strategy_sum));
            all_categories[num_all_categories].count = 0;
            num_all_categories++;
        }
    }
    
    printf("Found %d unique hand types in SB range.\n\n", num_all_categories);
    
    // Analyze each street
    for (int street_idx = 0; street_idx < num_streets; street_idx++) {
        Street street = streets_to_analyze[street_idx];
        
        printf("\n=== Analyzing %s ===\n\n", street_names[street_idx]);
        
        // Reset categories for this street (but keep structure from all_categories)
        for (int i = 0; i < num_all_categories; i++) {
            strncpy(categories[i].category, all_categories[i].category, 15);
            categories[i].category[15] = '\0';
            memset(categories[i].strategy_sum, 0, sizeof(categories[i].strategy_sum));
            categories[i].count = 0;
        }
        num_categories = num_all_categories;
        
        int combinations = 0;
        // Reduced logging
        
        // For each unique hand category, find a representative SB hand and test it
        for (int cat_idx = 0; cat_idx < num_all_categories; cat_idx++) {
            const char *target_cat = all_categories[cat_idx].category;
            
            // Apply range percentage - skip this hand category if random check fails
            if (sb_range->percentage < 1.0) {
                double rand_val = (double)rand() / RAND_MAX;
                if (rand_val > sb_range->percentage) {
                    // Skip this hand (not opened this percentage of the time)
                    continue;
                }
            }
            
            // Find first SB hand matching this category, respecting per-hand percentages
            int sb_c0 = -1, sb_c1 = -1;
            for (int sb_idx = 0; sb_idx < sb_range->count; sb_idx++) {
                int c0 = sb_range->hands[sb_idx][0];
                int c1 = sb_range->hands[sb_idx][1];
                const char *cat = hand_category(c0, c1);
                if (strcmp(cat, target_cat) == 0) {
                    // Check per-hand percentage
                    double hand_pct = sb_range->hand_percentages[sb_idx];
                    double rand_val = (double)rand() / RAND_MAX;
                    if (rand_val <= hand_pct) {
                        sb_c0 = c0;
                        sb_c1 = c1;
                        break;
                    }
                }
            }
            
            if (sb_c0 < 0 || sb_c1 < 0) continue;
            
            // Test with a few representative BB hands - keep searching until we find valid ones
            int bb_tested = 0;
            int bb_attempts = 0;
            int max_bb_attempts = bb_range->count * 2;  // Allow more attempts
            
            // Declare actual_board outside loop so it's accessible
            int actual_board[5];
            int actual_board_size = board_size;
            
            for (int bb_idx = 0; bb_idx < bb_range->count && bb_tested < 5 && bb_attempts < max_bb_attempts; bb_idx++) {
                int bb_c0 = bb_range->hands[bb_idx][0];
                int bb_c1 = bb_range->hands[bb_idx][1];
                bb_attempts++;
                
                // Apply BB per-hand percentage - skip if not defending this percentage
                double hand_pct = bb_range->hand_percentages[bb_idx];
                double rand_val = (double)rand() / RAND_MAX;
                if (rand_val > hand_pct) {
                    // Skip this BB hand (not defending this percentage of the time)
                    if (bb_idx == bb_range->count - 1) {
                        bb_idx = -1;
                    }
                    continue;
                }
                
                // Skip if cards overlap
                if (cards_overlap(sb_c0, sb_c1, bb_c0, bb_c1, board, board_size)) {
                    // Try next BB hand, but wrap around if needed
                    if (bb_idx == bb_range->count - 1) {
                        bb_idx = -1;  // Will become 0 on next iteration
                    }
                    continue;
                }
                
                bb_tested++;
                combinations++;
                
                // Generate board cards if not provided
                if (board_size == 0) {
                    // Generate random board for this street
                    generate_random_board(sb_c0, sb_c1, bb_c0, bb_c1, actual_board, street);
                    actual_board_size = (street == STREET_FLOP) ? 3 : (street == STREET_TURN) ? 4 : 5;
                } else {
                    // Use provided board
                    for (int i = 0; i < 5; i++) {
                        actual_board[i] = board[i];
                    }
                    actual_board_size = board_size;
                }
                
                // Create solver
                MCCFRSolver *solver = mccfr_create(sb_c0, sb_c1, bb_c0, bb_c1, hr);
                mccfr_set_board(solver, actual_board, street);
                
                // Run solver with more iterations for better convergence
                // Increase iterations to get better GTO strategies
                int iterations = (sb_range->count > 500) ? 200 : 500;
                mccfr_solve(solver, iterations);
                
                // Get strategy for SB (player 0) at root
                InfoSet root_iset;
                memset(&root_iset, 0, sizeof(InfoSet));
                root_iset.street = street;
                root_iset.player = 0;
                root_iset.num_actions = 0;
                for (int i = 0; i < 5; i++) {
                    root_iset.board_cards[i] = actual_board[i];
                }
                
                // Get strategy data - use get_or_create which will find existing entry
                InfoSetData strategy_data = {0};
                InfoSetData *data = mccfr_get_or_create(solver, &root_iset);
                if (data) {
                    // Copy the normalized strategy (already computed in mccfr_solve)
                    memcpy(&strategy_data, data, sizeof(InfoSetData));
                    
                    // Debug: verify we got actual data
                    if (data->visits == 0) {
                        // This shouldn't happen - entry should have been visited during CFR
                        fprintf(stderr, "Warning: Root info set has 0 visits!\n");
                    }
                } else {
                    fprintf(stderr, "Error: Could not get strategy data!\n");
                }
                
                // Free solver
                mccfr_free(solver);
                
                // Aggregate by hand category
                CategoryStrategy *cat_strat = &categories[cat_idx];
                if (cat_strat) {
                    // Normalize strategy
                    double sum = 0.0;
                    double normalized_strategy[3];
                    for (int a = 0; a < 3; a++) {
                        sum += strategy_data.strategy[a];
                    }
                    if (sum > 0) {
                        for (int a = 0; a < 3; a++) {
                            normalized_strategy[a] = strategy_data.strategy[a] / sum;
                            cat_strat->strategy_sum[a] += normalized_strategy[a];
                        }
                    } else {
                        for (int a = 0; a < 3; a++) {
                            normalized_strategy[a] = 1.0 / 3.0;
                            cat_strat->strategy_sum[a] += normalized_strategy[a];
                        }
                    }
                    cat_strat->count++;
                    
                    // Add to GUI if enabled
                    if (use_gui) {
                        const char *cat = hand_category(sb_c0, sb_c1);
                        gui_add_strategy(cat, normalized_strategy, actual_board, actual_board_size, street);
                    }
                }
            }
            
            // Reduced progress logging
        }
        
        printf("\n=== %s Strategy Breakdown (Range Grid) ===\n\n", street_names[street_idx]);
        
        // Create lookup maps: separate for suited and offsuit
        // Diagonal = pairs, upper triangle (row < col) = suited, lower triangle (row > col) = offsuit
        typedef struct {
            double strategy[3];
            int count;
            int has_data;
        } HandData;
        
        HandData hand_map_suited[13][13];   // For suited hands (upper triangle)
        HandData hand_map_offsuit[13][13];  // For offsuit hands (lower triangle)
        HandData hand_map_pairs[13][13];    // For pairs (diagonal)
        memset(hand_map_suited, 0, sizeof(hand_map_suited));
        memset(hand_map_offsuit, 0, sizeof(hand_map_offsuit));
        memset(hand_map_pairs, 0, sizeof(hand_map_pairs));
        
        // Fill the map from categories
        for (int i = 0; i < num_categories; i++) {
            const char *cat = categories[i].category;
            int r1 = -1, r2 = -1, is_pair = 0, is_suited = 0;
            
            // Parse category
            if (strlen(cat) == 2) {
                // Pair
                for (int r = 0; r < 13; r++) {
                    if (RANKS[r] == cat[0]) {
                        r1 = r2 = r;
                        is_pair = 1;
                        break;
                    }
                }
            } else if (strlen(cat) >= 3) {
                // Suited or offsuit
                is_suited = (cat[2] == 's');
                for (int r = 0; r < 13; r++) {
                    if (RANKS[r] == cat[0]) r1 = r;
                    if (RANKS[r] == cat[1]) r2 = r;
                }
            }
            
            if (r1 < 0 || r2 < 0) continue;
            
            if (categories[i].count > 0) {
                if (is_pair) {
                    hand_map_pairs[r1][r1].has_data = 1;
                    hand_map_pairs[r1][r1].count = categories[i].count;
                    for (int a = 0; a < 3; a++) {
                        hand_map_pairs[r1][r1].strategy[a] = categories[i].strategy_sum[a] / categories[i].count;
                    }
                } else {
                    // Parse the category: first char is high card, second is low card
                    int high_rank = -1, low_rank = -1;
                    for (int r = 0; r < 13; r++) {
                        if (RANKS[r] == cat[0]) high_rank = r;
                        if (RANKS[r] == cat[1]) low_rank = r;
                    }
                    
                    if (high_rank < 0 || low_rank < 0) continue;
                    
                    if (is_suited) {
                        // Suited: store at [high][low] for upper triangle (row > col visually)
                        // A2s: row=A(12), col=2(0), row > col = upper triangle
                        hand_map_suited[high_rank][low_rank].has_data = 1;
                        hand_map_suited[high_rank][low_rank].count = categories[i].count;
                        for (int a = 0; a < 3; a++) {
                            hand_map_suited[high_rank][low_rank].strategy[a] = categories[i].strategy_sum[a] / categories[i].count;
                        }
                    } else {
                        // Offsuit: store at [high][low] for lower triangle (row > col visually, but different from suited)
                        // Actually, for offsuit in lower triangle, we need row < col visually
                        // So store at [low][high] to get row < col when displaying
                        hand_map_offsuit[low_rank][high_rank].has_data = 1;
                        hand_map_offsuit[low_rank][high_rank].count = categories[i].count;
                        for (int a = 0; a < 3; a++) {
                            hand_map_offsuit[low_rank][high_rank].strategy[a] = categories[i].strategy_sum[a] / categories[i].count;
                        }
                    }
                }
            }
        }
        
        // Print grid format (standard poker range grid)
        // Rows and columns both go A, K, Q, J, T, 9, 8, 7, 6, 5, 4, 3, 2
        printf("      ");
        for (int col = 12; col >= 0; col--) {
            printf("%3c  ", RANKS[col]);
        }
        printf("\n");
        
        for (int row = 12; row >= 0; row--) {
            printf("%3c   ", RANKS[row]);
            
            for (int col = 12; col >= 0; col--) {
                if (row == col) {
                    // Pair (diagonal)
                    if (hand_map_pairs[row][col].has_data) {
                        double bet_pct = hand_map_pairs[row][col].strategy[1] * 100.0;
                        printf("%4.0f%% ", bet_pct);
                    } else {
                        printf("  --  ");
                    }
                } else if (row > col) {
                    // Upper triangle (row > col visually): Suited hands
                    // e.g., row=12(A), col=0(2) -> check suited map at [row][col] = [12][0] for A2s
                    if (hand_map_suited[row][col].has_data) {
                        double bet_pct = hand_map_suited[row][col].strategy[1] * 100.0;
                        printf("%4.0f%% ", bet_pct);
                    } else {
                        printf("  --  ");
                    }
                } else {
                    // Lower triangle (row < col visually): Offsuit hands
                    // e.g., row=0(2), col=12(A) -> check offsuit map at [row][col] = [0][12] for A2o
                    if (hand_map_offsuit[row][col].has_data) {
                        double bet_pct = hand_map_offsuit[row][col].strategy[1] * 100.0;
                        printf("%4.0f%% ", bet_pct);
                    } else {
                        printf("  --  ");
                    }
                }
            }
            printf("\n");
        }
        
        printf("\nLegend: Numbers show Bet/Raise percentage.\n");
        printf("        Pairs on diagonal | Upper triangle = Suited | Lower triangle = Offsuit\n");
        printf("Full details:\n");
        printf("%-8s %12s %12s %12s %8s\n", "Hand", "Check/Call", "Bet/Raise", "Fold", "Tests");
        printf("%-8s %12s %12s %12s %8s\n", "----", "----------", "----------", "----", "-----");
        
        // Also print detailed list
        for (int i = 0; i < num_categories; i++) {
            if (categories[i].count > 0) {
                printf("%-8s ", categories[i].category);
                for (int a = 0; a < 3; a++) {
                    double avg = categories[i].strategy_sum[a] / categories[i].count;
                    printf("%11.1f%% ", avg * 100.0);
                }
                printf("%8d\n", categories[i].count);
            }
        }
        
        printf("\nProcessed %d hand combinations for %s.\n\n", combinations, street_names[street_idx]);
    }
    
    hr_free(hr);
    free_range(sb_range);
    free_range(bb_range);
    
    printf("\n=== Analysis Complete ===\n");
    
    // Run GUI if enabled
    if (use_gui) {
        printf("\n=== Launching GUI (close window to exit) ===\n");
        gui_run();
        gui_cleanup();
    }
    
    return 0;
}
