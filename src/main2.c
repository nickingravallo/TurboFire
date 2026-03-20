#include "tree.h"
#include "indexer.h"
#include "evaluator.h"
#include "cfr.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

// Helper function to traverse the tree and count the total nodes
size_t count_nodes(PublicNode* node) {
    if (node == NULL) return 0;

    size_t total = 1; // Count this node

    // If it's a Terminal node, it has no children to traverse
    if (node->type == NODE_TERMINAL) {
        return total;
    }

    // Traverse all children recursively
    for (int i = 0; i < node->num_children; i++) {
        total += count_nodes(node->children[i]);
    }

    return total;
}

// Translates the strategy_sum array into human-readable percentages
void print_root_strategy(PublicNode* root, IsoMap* map, int num_buckets) {
    printf("\n--- P1 FLOP STRATEGY (First 20 Unique Hands) ---\n");
    
    char ranks[] = "23456789TJQKA";
    char suits[] = "shdc"; // Spades, Hearts, Diamonds, Clubs
    
    int printed_count = 0;
    int seen_buckets[1184] = {0}; // Track which buckets we've already printed

    // Our hardcoded root actions: Check, Bet 33%, Bet 52%, Bet 100%, All-in
    printf("%-10s | %-7s | %-7s | %-7s | %-7s | %-7s\n", "Hand", "Check", "Bet 33", "Bet 52", "Bet 100", "All-In");
    printf("-----------------------------------------------------------------\n");

    // Loop through all 1326 possible starting hands
    for (int combo = 0; combo < 1326; combo++) {
        int bucket = map->combo_to_bucket[combo];
        
        // Skip if the hand is dead (contains board cards) or we already printed this bucket
        if (bucket == -1 || seen_buckets[bucket] == 1) continue;
        
        seen_buckets[bucket] = 1;
        printed_count++;

        // Reconstruct the two specific cards for this combo
        int c1 = 0, c2 = 0, counter = 0;
        for (int i = 0; i < 51; i++) {
            for (int j = i + 1; j < 52; j++) {
                if (counter == combo) { c1 = i; c2 = j; break; }
                counter++;
            }
            if (c1 || c2) break;
        }

        // 1. Calculate the total strategy sum for this specific bucket
        float sum = 0.0f;
        for (int a = 0; a < root->num_children; a++) {
            sum += root->strategy_sum[(a * num_buckets) + bucket];
        }

        // 2. Format the hand string (e.g., "Ah Kh")
        char hand_str[6];
        sprintf(hand_str, "%c%c %c%c", ranks[c1%13], suits[c1/13], ranks[c2%13], suits[c2/13]);
        printf("%-10s", hand_str);

        // 3. Print the normalized probability for each action
        for (int a = 0; a < root->num_children; a++) {
            float prob = 0.0f;
            if (sum > 0.0f) {
                prob = root->strategy_sum[(a * num_buckets) + bucket] / sum;
            } else {
                prob = 1.0f / root->num_children; // Default to random if no sum
            }
            printf(" | %6.1f%%", prob * 100.0f);
        }
        printf("\n");

        if (printed_count >= 20) break; // Stop after 20 hands so we don't flood the terminal
    }
    printf("-----------------------------------------------------------------\n");
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
    // (We removed the sanity test, but we MUST keep this initialization call)
    printf("Initializing OMPEval tables...\n");
    init_evaluator();

    // --- SOLVER EXECUTION ---
    printf("Starting DCFR Solver...\n");
    int num_iterations = 100; 
    
    clock_t start_time = clock();

    // The core execution loop
    for (int i = 0; i < num_iterations; i++) {
        do_cfr_iteration(root, root_state, &flop_map, flop_map.padded_buckets);
        
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
    // NOW we safely free the arena back to the OS
    free(arena.memory);

    return 0;
}
