#include "solver/abstraction.h"
#include <algorithm>

namespace poker {

std::array<uint32_t, 1326 * 8> HandAbstraction::bucket_lookup_;

uint32_t HandAbstraction::getBucket(uint32_t hand_index, const std::vector<uint8_t>& board_cards) {
    uint8_t texture = getBoardTexture(board_cards);
    
    // For now, return a simplified bucket based on hand type and texture
    // In production, this would use sophisticated bucketing based on hand strength
    
    // Decode hand index to cards
    uint8_t card1, card2;
    if (hand_index < 78) {
        // Pairs
        uint8_t rank = hand_index / 6;
        return rank * 8 + texture; // 13 ranks x 8 textures = 104 buckets
    } else if (hand_index < 390) {
        // Suited
        uint32_t suited_idx = hand_index - 78;
        uint8_t suit = suited_idx % 4;
        uint32_t pair_idx = suited_idx / 4;
        return 104 + pair_idx * 4 * 8 + suit * 8 + texture;
    } else {
        // Offsuit
        uint32_t offsuit_idx = hand_index - 390;
        return 104 + 312 * 8 + offsuit_idx * 8 + texture;
    }
}

HandAbstraction::BucketInfo HandAbstraction::getBucketInfo(uint32_t bucket) {
    BucketInfo info = {};
    // Simplified - extract info from bucket number
    if (bucket < 104) {
        info.pair = true;
        info.rank1 = bucket / 8;
        info.rank2 = info.rank1;
    }
    return info;
}

std::vector<uint32_t> HandAbstraction::getHandsInBucket(uint32_t bucket) {
    std::vector<uint32_t> hands;
    // Return all hands that map to this bucket
    // This is expensive, so in practice we'd cache this
    return hands;
}

uint8_t HandAbstraction::getBoardTexture(const std::vector<uint8_t>& board_cards) {
    if (board_cards.empty()) return 0; // Preflop
    
    // Simple texture classification
    // 0: Dry/Rainbow unpaired
    // 1: Paired
    // 2: Monotone
    // 3: Two-tone
    // 4: Connected
    // 5: Paired + suited
    // 6: Monotone + connected
    // 7: Very wet
    
    bool paired = false;
    uint8_t suits[4] = {0};
    uint8_t ranks[13] = {0};
    
    for (uint8_t card : board_cards) {
        uint8_t rank = card >> 2;
        uint8_t suit = card & 0x3;
        if (ranks[rank] > 0) paired = true;
        ranks[rank]++;
        suits[suit]++;
    }
    
    bool monotone = false;
    bool two_tone = false;
    for (int i = 0; i < 4; ++i) {
        if (suits[i] >= 3) monotone = true;
        if (suits[i] == 2) two_tone = true;
    }
    
    bool connected = false;
    for (int i = 0; i <= 9; ++i) {
        int count = 0;
        for (int j = 0; j < 5 && (i+j) < 13; ++j) {
            if (ranks[i+j]) count++;
        }
        if (count >= 3) connected = true;
    }
    
    if (monotone && paired) return 5;
    if (monotone && connected) return 6;
    if (monotone) return 2;
    if (paired && two_tone) return 5;
    if (paired) return 1;
    if (two_tone && connected) return 7;
    if (two_tone) return 3;
    if (connected) return 4;
    return 0;
}

void HandAbstraction::initLookupTables() {
    // Initialize bucket lookup table
    // This would be precomputed and loaded from file in production
}

} // namespace poker
