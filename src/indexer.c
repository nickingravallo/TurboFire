#include "indexer.h"

// Your highly optimized signature generator
static unsigned __int128 get_canonical_hand(uint64_t private_hand, uint64_t board) {
    unsigned __int128 packed_suits = 0;
    
    packed_suits |= (unsigned __int128)((board >> 0) & 0x1FFF) << 0;
    packed_suits |= (unsigned __int128)((private_hand >> 0) & 0x1FFF) << 13;
    packed_suits |= (unsigned __int128)((board >> 16) & 0x1FFF) << 26;
    packed_suits |= (unsigned __int128)((private_hand >> 16) & 0x1FFF) << 39;
    packed_suits |= (unsigned __int128)((board >> 32) & 0x1FFF) << 52;
    packed_suits |= (unsigned __int128)((private_hand >> 32) & 0x1FFF) << 65;
    packed_suits |= (unsigned __int128)((board >> 48) & 0x1FFF) << 78;
    packed_suits |= (unsigned __int128)((private_hand >> 48) & 0x1FFF) << 91;

    uint32_t s[4];
    s[0] = (packed_suits >> 0)  & 0x3FFFFFF;
    s[1] = (packed_suits >> 26) & 0x3FFFFFF;
    s[2] = (packed_suits >> 52) & 0x3FFFFFF;
    s[3] = (packed_suits >> 78) & 0x3FFFFFF;

    #define SWAP(a, b) do { \
        uint32_t t = s[a] ^ s[b]; \
        uint32_t mask = (s[a] < s[b]) ? ~0U : 0U; \
        s[a] ^= t & mask; \
        s[b] ^= t & mask; \
    } while(0)

    SWAP(0, 1); SWAP(2, 3); 
    SWAP(0, 2); SWAP(1, 3); 
    SWAP(1, 2);
    #undef SWAP

    unsigned __int128 canonical = 0;
    canonical |= ((unsigned __int128)s[0]) << 0;
    canonical |= ((unsigned __int128)s[1]) << 26;
    canonical |= ((unsigned __int128)s[2]) << 52;
    canonical |= ((unsigned __int128)s[3]) << 78;

    return canonical;
}

void build_isomorphism_map(uint64_t board_mask, IsoMap* out_map) {
    out_map->num_unique_buckets = 0;
    unsigned __int128 seen_signatures[MAX_BUCKETS];

    int combo_idx = 0;
    for (int c1 = 0; c1 < 51; c1++) {
        for (int c2 = c1 + 1; c2 < 52; c2++) {
            int r1 = c1 % 13; int s1 = c1 / 13;
            int r2 = c2 % 13; int s2 = c2 / 13;

            uint64_t combo_mask = (1ULL << (r1 + (s1 * 16))) | (1ULL << (r2 + (s2 * 16)));

            if ((combo_mask & board_mask) != 0) {
                out_map->combo_to_bucket[combo_idx] = -1; // Dead hand
            } else {
                unsigned __int128 sig = get_canonical_hand(combo_mask, board_mask);

                int bucket_id = -1;
                for (int b = 0; b < out_map->num_unique_buckets; b++) {
                    if (seen_signatures[b] == sig) {
                        bucket_id = b;
                        break;
                    }
                }

                if (bucket_id == -1) {
                    bucket_id = out_map->num_unique_buckets;
                    seen_signatures[bucket_id] = sig;
                    out_map->num_unique_buckets++;
                }

                out_map->combo_to_bucket[combo_idx] = bucket_id;
            }
            combo_idx++;
        }
    }
    // Pad for ARM NEON / AVX2
    out_map->padded_buckets = (out_map->num_unique_buckets + 7) & ~7;
}
