#include "tree.h"
#include "indexer.h"
#include "evaluator.h"
#include <stdio.h>

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

int main() {
    printf("Initializing TurboFire Engine...\n\n");

    // 1. Define the Flop (Ace of Spades, 8 of Spades, 2 of Spades)
    // Spades = Offset 0. (Rank: 2=0, 3=1, ..., 8=6, ..., A=12)
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

    // 3. Initialize the 2GB Memory Arena
    printf("Allocating 2GB Memory Arena...\n");
    Arena arena;
    size_t arena_size = 2ULL * 1024 * 1024 * 1024; // 2 Gigabytes
    arena_init(&arena, arena_size);
    printf("-> Arena Initialized.\n\n");

    // 4. Setup the Initial GameState (e.g., Single Raised Pot)
    GameState root_state = {0};
    root_state.board = flop_board;
    root_state.pot = 200;                   // 6bb pot
    root_state.p1_stack = 300;             // 97bb remaining
    root_state.p2_stack = 300;             // 97bb remaining
    root_state.p1_commit = 0;
    root_state.p2_commit = 0;
    root_state.active_player = 0;          // Player 1 (OOP) acts first
    root_state.street = 0;                 // 0 = Flop
    root_state.raises_this_street = 0;
    root_state.num_actions_this_street = 0;
    root_state.last_action_was_fold = 0;

    // 5. Build the Game Tree!
    printf("Building Public State Tree (This might take a second)...\n");
    PublicNode* root = build_public_tree(&arena, root_state, flop_map.padded_buckets);

    // 6. Verify and Count
    printf("Traversing tree to count nodes...\n");
    size_t total_nodes = count_nodes(root);
    
    // Calculate memory used
    double mb_used = (double)arena.offset / (1024.0 * 1024.0);

    printf("\n========================================\n");
    printf("TREE BUILD COMPLETE\n");
    printf("Total Nodes Generated: %zu\n", total_nodes);
    printf("Arena Memory Used: %.2f MB\n", mb_used);
    printf("========================================\n");

    // Free the arena back to the OS
    free(arena.memory);

    // --- OMPEVAL SANITY TEST ---
    init_evaluator();
    printf("Evaluating a test hand...\n");

    // Let's build a Royal Flush board in Spades (Offset 0)
    uint64_t Ts = 1ULL << 8;
    uint64_t Js = 1ULL << 9;
    uint64_t Qs = 1ULL << 10;
    uint64_t Ks = 1ULL << 11;
    uint64_t Aces = 1ULL << 12;
    uint64_t royal_board = Ts | Js | Qs | Ks | Aces;

    // Player 1 has completely dead cards (Pair of 2s in Hearts/Diamonds)
    uint64_t p1_hand = (1ULL << (0 + 16)) | (1ULL << (0 + 32)); // 2h 2d

    int score = evaluate_board(p1_hand, royal_board);
    printf("-> P1 Hand Score: %d\n\n", score);
    // ---------------------------
    return 0;
}
