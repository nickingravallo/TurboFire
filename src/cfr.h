#ifndef CFR_H
#define CFR_H

#include "tree.h"
#include "indexer.h"

void do_cfr_iteration(PublicNode* root, GameState initial_state, IsoMap* map, int num_buckets, float* p1_starting_range, float* p2_starting_range);

void discount_tree(PublicNode* node, int num_buckets, int t, float alpha, float beta, float gamma);

void extract_action_range(PublicNode* node, int num_buckets, int action_idx, float* current_reach, float* out_new_reach);
#endif //CFR_H
