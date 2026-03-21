#include "tree.h"
#include "indexer.h"
#include "evaluator.h"
#include "cfr.h"
#include "ex.h"
#include "parse.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

// Helper function to traverse the tree and count the total nodes
size_t count_nodes(PublicNode* node) {
    if (node == NULL) return 0;

    size_t total = 1; // Count this node

    if (node->type == NODE_TERMINAL) {
        return total;
    }

    for (int i = 0; i < node->num_children; i++) {
        total += count_nodes(node->children[i]);
    }

    return total;
}

// Translates a raw PlayerRange (masks) into a bucketed reach probability array for the solver
void convert_range_to_buckets(PlayerRange* range, IsoMap* map, float* out_reach_array) {
    for (int i = 0; i < map->padded_buckets; i++) {
        out_reach_array[i] = 0.0f;
    }

    for (int combo = 0; combo < 1326; combo++) {
        int target_bucket = map->combo_to_bucket[combo];
        if (target_bucket == -1) continue;

        int c1 = 0, c2 = 0, counter = 0;
        for (int i = 0; i < 51; i++) {
            for (int j = i + 1; j < 52; j++) {
                if (counter == combo) { c1 = i; c2 = j; break; }
                counter++;
            }
            if (c1 || c2) break;
        }
        int r1 = c1 % 13; int s1 = c1 / 13;
        int r2 = c2 % 13; int s2 = c2 / 13;
        uint64_t combo_mask = (1ULL << (r1 + (s1 * 16))) | (1ULL << (r2 + (s2 * 16)));

        for (int i = 0; i < range->num_combos; i++) {
            if (range->combos[i].mask == combo_mask) {
                out_reach_array[target_bucket] = range->combos[i].weight;
                break;
            }
        }
    }
}

// Aggregates strategy into a 13x13 grid, colored by the dominant action
void print_root_strategy(PublicNode* root, IsoMap* map, int num_buckets) {
    printf("\n--- P1 FLOP STRATEGY GRID (Dominant Action) ---\n");

    int num_actions = root->num_children;
    float grid_probs[13][13][8] = {0}; // Up to 8 actions
    int grid_counts[13][13] = {0};

    // 1. Accumulate probabilities for every valid combo
    for (int combo = 0; combo < 1326; combo++) {
        int bucket = map->combo_to_bucket[combo];
        if (bucket == -1) continue;

        float sum = 0.0f;
        for (int a = 0; a < num_actions; a++) {
            sum += root->strategy_sum[(a * num_buckets) + bucket];
        }
        
        // Skip dead hands (this filters out the 20/20/20 splits)
        if (sum == 0.0f) continue; 

        // Reconstruct the cards
        int c1 = 0, c2 = 0, counter = 0;
        for (int i = 0; i < 51; i++) {
            for (int j = i + 1; j < 52; j++) {
                if (counter == combo) { c1 = i; c2 = j; break; }
                counter++;
            }
            if (c1 || c2) break;
        }

        int r1 = (c1 % 13); int s1 = (c1 / 13);
        int r2 = (c2 % 13); int s2 = (c2 / 13);
        int rank_high = (r1 > r2) ? r1 : r2;
        int rank_low  = (r1 < r2) ? r1 : r2;

        // Map to 13x13 coordinates (A=0, K=1, ..., 2=12)
        int row = 12 - rank_high; 
        int col = 12 - rank_low;

        int grid_r, grid_c;
        if (s1 == s2) { grid_r = row; grid_c = col; }        // Suited: Top-Right
        else if (r1 == r2) { grid_r = row; grid_c = col; }   // Pair: Diagonal
        else { grid_r = col; grid_c = row; }                 // Offsuit: Bottom-Left

        // Add this combo's normalized strategy to the cell's running total
        grid_counts[grid_r][grid_c]++;
        for (int a = 0; a < num_actions; a++) {
            float prob = root->strategy_sum[(a * num_buckets) + bucket] / sum;
            grid_probs[grid_r][grid_c][a] += prob;
        }
    }

    // 2. Setup ANSI Colors for the terminal
    const char rank_chars[] = "AKQJT98765432";
    const char* colors[] = {
        "\x1b[32m",   // 0: Check (Green)
        "\x1b[33m",   // 1: Bet 33 (Yellow)
        "\x1b[35m",   // 2: Bet 52 (Magenta)
        "\x1b[31m",   // 3: Bet 100 (Red)
        "\x1b[1;31m", // 4: All-In (Bold Red)
    };
    const char* reset = "\x1b[0m";

    printf("Legend: %sCheck%s | %sBet 33%%%s | %sBet 52%%%s | %sBet 100%%%s | %sAll-In%s\n\n",
        colors[0], reset, colors[1], reset, colors[2], reset, colors[3], reset, colors[4], reset);

    // 3. Print the Grid
    printf("    ");
    for (int i = 0; i < 13; i++) printf("  %c  ", rank_chars[i]);
    printf("\n");

    for (int r = 0; r < 13; r++) {
        printf("%c | ", rank_chars[r]);
        for (int c = 0; c < 13; c++) {
            
            if (grid_counts[r][c] == 0) {
                // No valid combos exist for this hand class
                printf("  -  ");
            } else {
                // Find the dominant action for this hand class
                int dom_a = 0;
                float max_p = -1.0f;
                for (int a = 0; a < num_actions; a++) {
                    float avg_p = grid_probs[r][c][a] / (float)grid_counts[r][c];
                    if (avg_p > max_p) {
                        max_p = avg_p;
                        dom_a = a;
                    }
                }
                
                float print_pct = max_p * 100.0f;
                int color_idx = dom_a % 5;
                
                // Print the percentage wrapped in the correct ANSI color code
                printf("%s%3.0f%%%s ", colors[color_idx], print_pct, reset);
            }
        }
        printf("\n");
    }
    printf("\n");
}


int main() {
    printf("Initializing TurboFire Engine...\n\n");

    // 1. Define the Flop (Ace of Spades, 8 of Spades, 2 of Spades)
    uint64_t As = 1ULL << 12;
    uint64_t Eights = 1ULL << 6;
    uint64_t Twos = 1ULL << 0;
    uint64_t flop_board = As | Eights | Twos;

    // 2. Build the Isomorphism Map
    printf("Building Flop Isomorphism Map...\n");
    IsoMap flop_map;
    build_isomorphism_map(flop_board, &flop_map);
    printf("-> Unique Buckets: %d\n", flop_map.num_unique_buckets);
    printf("-> SIMD Padded Buckets: %d\n\n", flop_map.padded_buckets);

    // 3. Initialize the Memory Arena
    printf("Allocating Memory Arena...\n");
    Arena arena;
    size_t arena_size = 2ULL * 1024 * 1024 * 1024; // 2 Gigabytes
    arena_init(&arena, arena_size);
    printf("-> Arena Initialized.\n\n");

    // 4. Setup the Initial GameState (3-Bet Pot)
    GameState root_state = {0};
    root_state.board = flop_board;
    root_state.pot = 200;                  
    root_state.p1_stack = 300;             
    root_state.p2_stack = 300;             
    root_state.p1_commit = 0;
    root_state.p2_commit = 0;
    root_state.active_player = 0;          
    root_state.street = 0;                 
    root_state.raises_this_street = 0;
    root_state.num_actions_this_street = 0;
    root_state.last_action_was_fold = 0;

    // 5. Build the Game Tree!
    printf("Building Public State Tree (This might take a second)...\n");
    PublicNode* root = build_public_tree(&arena, root_state, flop_map.padded_buckets);

    // 6. Verify and Count
    printf("Traversing tree to count nodes...\n");
    size_t total_nodes = count_nodes(root);
    
    double mb_used = (double)arena.offset / (1024.0 * 1024.0);

    printf("\n========================================\n");
    printf("TREE BUILD COMPLETE\n");
    printf("Total Nodes Generated: %zu\n", total_nodes);
    printf("Arena Memory Used: %.2f MB\n", mb_used);
    printf("========================================\n\n");

    // --- OMPEVAL INITIALIZATION ---
    printf("Initializing OMPEval tables...\n");
    init_evaluator();

    // --- LOAD PREFLOP RANGES ---
    printf("Loading Preflop Ranges...\n");
    
    PlayerRange p1_raw_range = {0};
    parse_json_range(sb, &p1_raw_range);

    PlayerRange p2_raw_range = {0};
    parse_json_range(btn, &p2_raw_range);

    // Convert raw parsed masks into bucketed probability arrays
    float* p1_starting_reach = (float*)malloc(flop_map.padded_buckets * sizeof(float));
    float* p2_starting_reach = (float*)malloc(flop_map.padded_buckets * sizeof(float));
    
    convert_range_to_buckets(&p1_raw_range, &flop_map, p1_starting_reach);
    convert_range_to_buckets(&p2_raw_range, &flop_map, p2_starting_reach);

    // --- SOLVER EXECUTION ---
    printf("Starting DCFR Solver...\n");
    int num_iterations = 100; 
    
    clock_t start_time = clock();

    // The core execution loop
    for (int i = 0; i < num_iterations; i++) {
        // Pass the new reach arrays into the iteration
        do_cfr_iteration(root, root_state, &flop_map, flop_map.padded_buckets, p1_starting_reach, p2_starting_reach);
        
        // Print a progress update every 10 iterations
        if ((i + 1) % 10 == 0) {
            printf("Completed %d / %d iterations...\n", i + 1, num_iterations);
        }
    }

    clock_t end_time = clock();
    double time_spent = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    printf("\n========================================\n");
    printf("SOLVER FINISHED\n");
    printf("Total Time: %.2f seconds\n", time_spent);
    printf("Speed: %.4f seconds per iteration\n", time_spent / num_iterations);
    printf("========================================\n");

    print_root_strategy(root, &flop_map, flop_map.padded_buckets);
    
    // Free the reach arrays
    free(p1_starting_reach);
    free(p2_starting_reach);

    // NOW we safely free the arena back to the OS
    free(arena.memory);

    return 0;
}
