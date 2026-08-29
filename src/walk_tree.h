#ifndef WALK_TREE_H
#define WALK_TREE_H

#include "tree.h"

// Emit soft JSONL: HERO_/OPP_ tokens relative to each row's hole-card holder.
void walk_tree(PublicNode* root, GameState initial_state, int num_combos, float* p1_range, float* p2_range);

#endif // WALK_TREE_H
