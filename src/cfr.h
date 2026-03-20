#ifndef CFR_H
#define CFR_H

#include "tree.h"
#include "indexer.h"

void do_cfr_iteration(PublicNode* root, GameState initial_state, IsoMap* map, int num_buckets);

#endif //CFR_H
