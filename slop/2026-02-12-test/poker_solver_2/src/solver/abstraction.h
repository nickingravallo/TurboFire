#pragma once

#include <cstdint>
#include <vector>
#include <array>

namespace poker {

// Isomorphic hand abstraction
// Reduces 1326 hands to ~1500 canonical buckets
class HandAbstraction {
public:
    static constexpr uint32_t NUM_BUCKETS = 1500;
    
    struct BucketInfo {
        uint16_t rank1;
        uint16_t rank2;
        bool suited;
        bool pair;
        uint8_t board_texture; // 0-7 for different textures
    };
    
    // Get canonical bucket for a hand given board
    static uint32_t getBucket(uint32_t hand_index, const std::vector<uint8_t>& board_cards);
    
    // Get bucket info
    static BucketInfo getBucketInfo(uint32_t bucket);
    
    // Get all hands in a bucket
    static std::vector<uint32_t> getHandsInBucket(uint32_t bucket);
    
private:
    // Precomputed lookup tables
    static std::array<uint32_t, 1326 * 8> bucket_lookup_; // 1326 hands x 8 board textures
    
    static void initLookupTables();
    static uint8_t getBoardTexture(const std::vector<uint8_t>& board_cards);
};

} // namespace poker
