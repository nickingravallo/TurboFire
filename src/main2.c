#include "tree.h"
#include "indexer.h"
#include "evaluator.h"
#include "cfr.h"
#include "ex.h"
#include "parse.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- DECLARE THE EXPLOITABILITY FUNCTION ---
float calc_exploitability(PublicNode* root, GameState initial_state, IsoMap* map, int num_buckets, float* p1_starting_range, float* p2_starting_range);

// Helper function to traverse the tree and count the total nodes
size_t count_nodes(PublicNode* node) {
    if (node == NULL) return 0;
    size_t total = 1; 
    if (node->type == NODE_TERMINAL) return total;
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
                out_reach_array[target_bucket] += range->combos[i].weight;
                break;
            }
        }
    }
}

// Aggregates strategy into a 13x13 grid, filtering out folded hands and dynamically coloring actions
void print_root_strategy(PublicNode* root, GameState state, IsoMap* map, int num_buckets, float* current_reach) {
    int legal_actions[8];
    int num_actions = generate_bet_sizes(&state, legal_actions);

    printf("\n--- PLAYER %d STRATEGY GRID (Dominant Action) ---\n", state.active_player + 1);

    const char* colors[] = {
        "\x1b[36m",   // Cyan (Fold)
        "\x1b[32m",   // Green (Check/Call)
        "\x1b[33m",   // Yellow (Bet/Raise 1)
        "\x1b[35m",   // Magenta (Bet/Raise 2)
        "\x1b[34m",   // Blue (Bet/Raise 3)
        "\x1b[31m",   // Red (Bet/Raise 4)
        "\x1b[1;31m", // Bold Red (All-In / Bet 5)
        "\x1b[37m"    // White (Fallback)
    };
    const char* reset = "\x1b[0m";

    printf("Legend: ");
    for (int i = 0; i < num_actions; i++) {
        int color_idx = (i < 8) ? i : 7;
        if (legal_actions[i] == -1) printf("%sFold%s | ", colors[color_idx], reset);
        else if (legal_actions[i] == 0) printf("%sCheck/Call%s | ", colors[color_idx], reset);
        else printf("%sBet %d%s | ", colors[color_idx], legal_actions[i], reset);
    }
    printf("\b\b  \n\n"); 

    float grid_probs[13][13][8] = {0}; 
    int grid_counts[13][13] = {0};

    for (int combo = 0; combo < 1326; combo++) {
        int bucket = map->combo_to_bucket[combo];
        if (bucket == -1) continue; 
        if (current_reach[bucket] < 0.0001f) continue; 

        float sum = 0.0f;
        for (int a = 0; a < num_actions; a++) {
            sum += root->strategy_sum[(a * num_buckets) + bucket];
        }
        if (sum == 0.0f) continue; 

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

        int row = 12 - rank_high; 
        int col = 12 - rank_low;

        int grid_r, grid_c;
        if (s1 == s2) { grid_r = row; grid_c = col; }        
        else if (r1 == r2) { grid_r = row; grid_c = col; }   
        else { grid_r = col; grid_c = row; }                 

        grid_counts[grid_r][grid_c]++;
        for (int a = 0; a < num_actions; a++) {
            float prob = root->strategy_sum[(a * num_buckets) + bucket] / sum;
            grid_probs[grid_r][grid_c][a] += prob;
        }
    }

    const char rank_chars[] = "AKQJT98765432";
    printf("    ");
    for (int i = 0; i < 13; i++) printf("  %c  ", rank_chars[i]);
    printf("\n");

    for (int r = 0; r < 13; r++) {
        printf("%c | ", rank_chars[r]);
        for (int c = 0; c < 13; c++) {
            if (grid_counts[r][c] == 0) {
                printf("  -  ");
            } else {
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
                int color_idx = (dom_a < 8) ? dom_a : 7;
                printf("%s%3.0f%%%s ", colors[color_idx], print_pct, reset);
            }
        }
        printf("\n");
    }
    printf("\n");
}

// Aggregates a player's reach probabilities into a 13x13 grid to visualize their range
void print_live_reach_grid(float* reach, IsoMap* map) {
    printf("\n--- PLAYER RANGE GRID (Hand Frequencies) ---\n");

    float grid_weights[13][13] = {0};
    int grid_counts[13][13] = {0};

    for (int combo = 0; combo < 1326; combo++) {
        int bucket = map->combo_to_bucket[combo];
        if (bucket == -1) continue; 

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

        int row = 12 - rank_high; 
        int col = 12 - rank_low;

        int grid_r, grid_c;
        if (s1 == s2) { grid_r = row; grid_c = col; }        
        else if (r1 == r2) { grid_r = row; grid_c = col; }   
        else { grid_r = col; grid_c = row; }                 

        grid_weights[grid_r][grid_c] += reach[bucket];
        grid_counts[grid_r][grid_c]++;
    }

    const char rank_chars[] = "AKQJT98765432";
    printf("    ");
    for (int i = 0; i < 13; i++) printf("  %c  ", rank_chars[i]);
    printf("\n");

    for (int r = 0; r < 13; r++) {
        printf("%c | ", rank_chars[r]);
        for (int c = 0; c < 13; c++) {
            if (grid_counts[r][c] == 0) {
                printf("  -  ");
            } else {
                float avg_weight = grid_weights[r][c] / (float)grid_counts[r][c];
                if (avg_weight < 0.005f) {
                     printf("  -  ");
                } else {
                     printf("%3.0f%% ", avg_weight * 100.0f);
                }
            }
        }
        printf("\n");
    }
    printf("\n");
}

int get_action_index(GameState state, int target_action) {
    int legal_actions[8];
    int num_actions = generate_bet_sizes(&state, legal_actions); 
    
    for (int i = 0; i < num_actions; i++) {
        if (legal_actions[i] == target_action) {
            return i;
        }
    }
    return -1; 
}

int main(int argc, char** argv) {
    printf("Initializing TurboFire Engine...\n\n");

    if (argc < 5) {
        printf("ERROR: Missing arguments.\n");
        printf("Usage:   ./turbofire \"<board>\" <pot> <p1_stack> <p2_stack>\n");
        printf("Example: ./turbofire \"As 8s 2s\" 200 300 300\n\n");
        return 1;
    }

    const char* board_str = argv[1];
    int pot_size = atoi(argv[2]);
    int p1_stack = atoi(argv[3]);
    int p2_stack = atoi(argv[4]);

    printf("--- GAME STATE CONFIGURATION ---\n");
    printf("Board:    %s\n", board_str);
    printf("Pot:      %d\n", pot_size);
    printf("P1 Stack: %d\n", p1_stack);
    printf("P2 Stack: %d\n", p2_stack);
    printf("--------------------------------\n\n");

    uint64_t flop_board = parse_board_string(board_str);

    printf("Building Flop Isomorphism Map...\n");
    IsoMap flop_map;
    build_isomorphism_map(flop_board, &flop_map);
    printf("-> Unique Buckets: %d\n", flop_map.num_unique_buckets);
    printf("-> SIMD Padded Buckets: %d\n\n", flop_map.padded_buckets);

    printf("Allocating Memory Arena...\n");
    Arena arena;
    size_t arena_size = 8ULL * 1024 * 1024 * 1024; // 8 Gigabytes
    arena_init(&arena, arena_size);
    printf("-> Arena Initialized.\n\n");

    GameState root_state = {0};
    root_state.board = flop_board;
    root_state.pot = pot_size;                  
    root_state.p1_stack = p1_stack;             
    root_state.p2_stack = p2_stack;             
    root_state.p1_commit = 0;
    root_state.p2_commit = 0;
    root_state.active_player = 0;          
    root_state.street = 0;                 
    root_state.raises_this_street = 0;
    root_state.num_actions_this_street = 0;
    root_state.last_action_was_fold = 0;

    printf("Building Public State Tree (This might take a second)...\n");
    PublicNode* root = build_public_tree(&arena, root_state, flop_map.padded_buckets);

    printf("Traversing tree to count nodes...\n");
    size_t total_nodes = count_nodes(root);
    
    double mb_used = (double)arena.offset / (1024.0 * 1024.0);

    printf("\n========================================\n");
    printf("TREE BUILD COMPLETE\n");
    printf("Total Nodes Generated: %zu\n", total_nodes);
    printf("Arena Memory Used: %.2f MB\n", mb_used);
    printf("========================================\n\n");

    printf("Initializing OMPEval tables...\n");
    init_evaluator();

    printf("Loading Preflop Ranges...\n");
    
    PlayerRange p1_raw_range = {0};
    parse_json_range(sb, &p1_raw_range); 

    PlayerRange p2_raw_range = {0};
    parse_json_range(btn, &p2_raw_range);   

    float* p1_starting_reach = (float*)malloc(flop_map.padded_buckets * sizeof(float));
    float* p2_starting_reach = (float*)malloc(flop_map.padded_buckets * sizeof(float));
    
    convert_range_to_buckets(&p1_raw_range, &flop_map, p1_starting_reach);
    convert_range_to_buckets(&p2_raw_range, &flop_map, p2_starting_reach);

    // --- SOLVER EXECUTION (FLOP) ---
    printf("Starting DCFR Solver...\n");
    int num_iterations = 50; 
    
    clock_t total_start_time = clock();
    clock_t chunk_start_time = clock();

    for (int i = 0; i < num_iterations; i++) {
        do_cfr_iteration(root, root_state, &flop_map, flop_map.padded_buckets, p1_starting_reach, p2_starting_reach);
        
        if ((i + 1) % 10 == 0) {
            clock_t chunk_end_time = clock();
            double chunk_time = (double)(chunk_end_time - chunk_start_time) / CLOCKS_PER_SEC;
            
            // --- NEW: CALC EXPLOITABILITY FOR FLOP ---
            float exp_chips = calc_exploitability(root, root_state, &flop_map, flop_map.padded_buckets, p1_starting_reach, p2_starting_reach);
            
            printf("Completed %d / %d iterations... [%.2f seconds] | Exploitability: %.4f chips\n", i + 1, num_iterations, chunk_time, exp_chips);
            
            chunk_start_time = clock(); 
        }
    }

    clock_t total_end_time = clock();
    double total_time_spent = (double)(total_end_time - total_start_time) / CLOCKS_PER_SEC;

    printf("\n========================================\n");
    printf("SOLVER FINISHED\n");
    printf("Total Time: %.2f seconds\n", total_time_spent);
    printf("Speed: %.4f seconds per iteration\n", total_time_spent / num_iterations);
    printf("========================================\n");

    // --- INTERACTIVE EXPLORER ---
    PublicNode* current_node = root;
    GameState current_state = root_state;
    
    float* live_p1_reach = (float*)malloc(flop_map.padded_buckets * sizeof(float));
    float* live_p2_reach = (float*)malloc(flop_map.padded_buckets * sizeof(float));
    
    for (int i = 0; i < flop_map.padded_buckets; i++) {
        live_p1_reach[i] = p1_starting_reach[i];
        live_p2_reach[i] = p2_starting_reach[i];
    }
    
    char input[64];
    while (1) {
        if (current_node->type == NODE_TERMINAL) {
            printf("\n========================================\n");
            printf("[ TERMINAL NODE REACHED. HAND OVER. ]\n");
            printf("Final Pot: %d | P1 Commit: %d | P2 Commit: %d\n", 
                   current_state.pot, current_state.p1_commit, current_state.p2_commit);
            printf("========================================\n\n");
            break;
        }

        // --- THE UNIFIED SUBGAME BRIDGE (TURN & RIVER) ---
        if (current_node->type == NODE_CHANCE) {
            int next_street = current_state.street + 1;
            
            printf("\n========================================\n");
            if (current_state.street == 0) printf("[ FLOP ACTION COMPLETE. ]\n");
            if (current_state.street == 1) printf("[ TURN ACTION COMPLETE. ]\n");
            printf("Pot: %d\n", current_state.pot);
            printf("========================================\n\n");

            char next_card_input[8];
            if (current_state.street == 0) printf("Enter the Turn card (e.g., '4h'): ");
            if (current_state.street == 1) printf("Enter the River card (e.g., 'Kd'): ");
            
            if (fgets(next_card_input, sizeof(next_card_input), stdin) == NULL) break;
            next_card_input[strcspn(next_card_input, "\n")] = 0; 
            
            uint64_t next_card_mask = parse_board_string(next_card_input);
            current_state.board |= next_card_mask;
            
            current_state.street = next_street; 
            current_state.p1_commit = 0;
            current_state.p2_commit = 0;
            current_state.raises_this_street = 0;
            current_state.num_actions_this_street = 0;
            current_state.active_player = 0; 

            float raw_p1_combos[1326] = {0};
            float raw_p2_combos[1326] = {0};
            for (int i = 0; i < 1326; i++) {
                int old_b = flop_map.combo_to_bucket[i];
                if (old_b != -1) {
                    raw_p1_combos[i] = live_p1_reach[old_b];
                    raw_p2_combos[i] = live_p2_reach[old_b];
                }
            }

            printf("\nBuilding %s Isomorphism Map...\n", (next_street == 1) ? "Turn" : "River");
            memset(flop_map.combo_to_bucket, -1, 1326 * sizeof(int));
            build_isomorphism_map(current_state.board, &flop_map); 
            printf("-> %s Buckets: %d\n", (next_street == 1) ? "Turn" : "River", flop_map.padded_buckets);

            float* new_p1_reach = (float*)calloc(flop_map.padded_buckets, sizeof(float));
            float* new_p2_reach = (float*)calloc(flop_map.padded_buckets, sizeof(float));
            int* bucket_counts = (int*)calloc(flop_map.padded_buckets, sizeof(int));

            for (int i = 0; i < 1326; i++) {
                int new_b = flop_map.combo_to_bucket[i];
                if (new_b != -1) {
                    new_p1_reach[new_b] += raw_p1_combos[i];
                    new_p2_reach[new_b] += raw_p2_combos[i];
                    bucket_counts[new_b]++;
                }
            }

            for (int b = 0; b < flop_map.padded_buckets; b++) {
                if (bucket_counts[b] > 0) {
                    new_p1_reach[b] /= (float)bucket_counts[b];
                    new_p2_reach[b] /= (float)bucket_counts[b];
                }
            }
            free(bucket_counts);

            free(live_p1_reach);
            free(live_p2_reach);
            live_p1_reach = new_p1_reach;
            live_p2_reach = new_p2_reach;

            printf("Resetting Memory Arena...\n");
            arena_reset(&arena);

            printf("Building %s Tree...\n", (next_street == 1) ? "Turn" : "River");
            current_node = build_public_tree(&arena, current_state, flop_map.padded_buckets);

            // --- SUBGAME EXECUTION & EXPLOITABILITY GRADING ---
            printf("Starting %s CFR Solver...\n", (next_street == 1) ? "Turn" : "River");
            int subgame_iterations = (next_street == 1) ? 600 : 200; 
            
            for (int i = 0; i < subgame_iterations; i++) {
                do_cfr_iteration(current_node, current_state, &flop_map, flop_map.padded_buckets, live_p1_reach, live_p2_reach);
                
                // NEW: Grade the subgame every 100 iterations
                if ((i + 1) % 100 == 0) {
                    float subgame_exp = calc_exploitability(current_node, current_state, &flop_map, flop_map.padded_buckets, live_p1_reach, live_p2_reach);
                    printf("Completed %d / %d iterations... | Exploitability: %.4f chips\n", i + 1, subgame_iterations, subgame_exp);
                }
            }
            
            printf("\n[ %s SOLVE COMPLETE ]\n", (next_street == 1) ? "TURN" : "RIVER");
            continue; 
        }

        printf("\n--- CURRENT NODE STRATEGY (Player %d) ---\n", current_state.active_player + 1);
        printf("Pot: %d | P1 Stack: %d | P2 Stack: %d\n", current_state.pot, current_state.p1_stack, current_state.p2_stack);

        float* active_reach = (current_state.active_player == 0) ? live_p1_reach : live_p2_reach;
        print_root_strategy(current_node, current_state, &flop_map, flop_map.padded_buckets, active_reach);

        int legal_actions[8];
        int num_actions = generate_bet_sizes(&current_state, legal_actions);
        
        printf("Legal Actions: ");
        for (int i = 0; i < num_actions; i++) {
            if (legal_actions[i] == -1) printf("[-1: Fold] ");
            else if (legal_actions[i] == 0) printf("[0: Check/Call] ");
            else printf("[%d: Bet/Raise] ", legal_actions[i]);
        }
        printf("\n");

        printf("Enter action amount to step forward ('r1' for P1 Range, 'r2' for P2 Range, 'q' to quit): ");
        
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        if (input[0] == 'q' || input[0] == 'Q') break;

        if (input[0] == 'r' && input[1] == '1') {
            print_live_reach_grid(live_p1_reach, &flop_map);
            continue; 
        }
        if (input[0] == 'r' && input[1] == '2') {
            print_live_reach_grid(live_p2_reach, &flop_map);
            continue; 
        }

        int chosen_action = atoi(input);
        int child_idx = get_action_index(current_state, chosen_action);
        
        if (child_idx == -1) {
            printf("\n[!] INVALID ACTION. Please type one of the exact numbers listed above.\n");
            continue;
        }

        if (current_state.active_player == 0) {
            extract_action_range(current_node, flop_map.padded_buckets, child_idx, live_p1_reach, live_p1_reach);
        } else {
            extract_action_range(current_node, flop_map.padded_buckets, child_idx, live_p2_reach, live_p2_reach);
        }

        current_node = current_node->children[child_idx];
        current_state = apply_bet(current_state, chosen_action);
    }

    free(p1_starting_reach);
    free(p2_starting_reach);
    free(live_p1_reach);
    free(live_p2_reach);
    free(arena.memory);

    return 0;
}
