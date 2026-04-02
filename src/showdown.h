#include "indexer.h"
#include "tree.h"
#include "evaluator.h"

#include <string.h>

uint64_t get_mask_for_bucket(IsoMap* map, int target_bucket);
void evaluate_showdown(GameState state, IsoMap* map, int num_buckets, float* p1_reach, float* p2_reach, float* out_util, uint64_t* precomputed_masks);
