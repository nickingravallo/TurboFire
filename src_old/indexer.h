#ifndef INDEXER_H
#define INDEXER_H

#include <stdint.h>

// 1176 max unique hands on flop, padded to 1184 for AVX2/NEON alignment
#define MAX_BUCKETS 1184

typedef struct {
    int combo_to_bucket[1326]; 
    int num_unique_buckets;
    int padded_buckets;        
} IsoMap;

void build_isomorphism_map(uint64_t board_mask, IsoMap* out_map);

#endif // INDEXER_H
