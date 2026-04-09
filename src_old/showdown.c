#include "showdown.h"

// A helper to find a valid 64-bit mask for a given bucket
uint64_t get_mask_for_bucket(IsoMap* map, int target_bucket) {
    for (int combo = 0; combo < 1326; combo++) {
        if (map->combo_to_bucket[combo] == target_bucket) {
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
            return (1ULL << (r1 + (s1 * 16))) | (1ULL << (r2 + (s2 * 16)));
        }
    }
    return 0; // Should never reach here if bucket is valid
}
// Update the signature
void evaluate_showdown(GameState state, IsoMap* map, int num_buckets, float* p1_reach, float* p2_reach, float* out_util, uint64_t* precomputed_masks) {
    memset(out_util, 0, num_buckets * sizeof(float));

    if (state.last_action_was_fold == 1) {
        for (int b = 0; b < num_buckets; b++) {
            if (state.active_player == 0) {
                out_util[b] = -(float)state.p1_commit; 
            } else {
                out_util[b] = -(float)state.p2_commit; 
            }
        }
        return; 
    }

    int bucket_scores[1184] = {0};
    for (int b = 0; b < map->num_unique_buckets; b++) {
        // FAST LOOKUP: No more looping 1,326 times here
        uint64_t hand_mask = precomputed_masks[b]; 
        bucket_scores[b] = evaluate_board(hand_mask, state.board);
    }

    for (int p1_b = 0; p1_b < map->num_unique_buckets; p1_b++) {
        if (p1_reach[p1_b] == 0.0f) continue;
        float expected_value = 0.0f;

        for (int p2_b = 0; p2_b < map->num_unique_buckets; p2_b++) {
            if (p2_reach[p2_b] == 0.0f) continue;

            int p1_score = bucket_scores[p1_b];
            int p2_score = bucket_scores[p2_b];

            if (p1_score > p2_score) {
                expected_value += p2_reach[p2_b] * (float)state.p2_commit; 
            } else if (p2_score > p1_score) {
                expected_value -= p2_reach[p2_b] * (float)state.p1_commit;
            } else {
                expected_value += 0.0f; 
            }
        }
        
        if (state.active_player == 0) {
            out_util[p1_b] = expected_value;
        } else {
            out_util[p1_b] = -expected_value; 
        }
    }
}
