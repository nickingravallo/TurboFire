#ifndef CFR_H
#define CFR_H

#include "tree.h"
#include "indexer.h"

void do_cfr_iteration(PublicNode* root, GameState initial_state, IsoMap* map, int num_buckets, float* p1_starting_range, float* p2_starting_range);

#endif //CFR_H
