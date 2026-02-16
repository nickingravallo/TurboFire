#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace gto {

// Hand representation for ranges: 0-168 (13*13 matrix)
// Row 0 = AA, AKs, AQs, ..., A2s
// Row 1 = AKo, KK, KQs, ..., K2s
// etc.
// Pocket pairs on diagonal, suited above, offsuit below

[[nodiscard]] constexpr uint16_t hand_index(uint8_t rank1, uint8_t rank2, bool suited) noexcept {
    if (rank1 == rank2) {
        return rank1 * 13 + rank1;  // Pocket pair
    }
    if (suited) {
        return rank1 * 13 + rank2;
    }
    return rank2 * 13 + rank1;
}

[[nodiscard]] inline std::string_view hand_name(uint16_t idx) noexcept {
    constexpr std::array<const char*, 13> ranks = {
        "2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K", "A"
    };
    
    uint8_t r1 = idx / 13;
    uint8_t r2 = idx % 13;
    
    static thread_local char buffer[5];
    if (r1 == r2) {
        // Pair
        buffer[0] = ranks[r1][0];
        buffer[1] = ranks[r2][0];
        buffer[2] = '\0';
    } else if (r1 > r2) {
        // Suited
        buffer[0] = ranks[r1][0];
        buffer[1] = ranks[r2][0];
        buffer[2] = 's';
        buffer[3] = '\0';
    } else {
        // Offsuit
        buffer[0] = ranks[r2][0];
        buffer[1] = ranks[r1][0];
        buffer[2] = 'o';
        buffer[3] = '\0';
    }
    
    return std::string_view(buffer);
}

// Range weights: 0.0 = never play, 1.0 = always play
using RangeWeights = std::array<float, 169>;

class PreflopRanges {
public:
    // SB (BTN) Opening Range (~85%)
    // Acts last postflop, opens very wide
    [[nodiscard]] static RangeWeights get_sb_opening_range() noexcept {
        RangeWeights range = {};
        
        // All suited hands: A2s+, K2s+, Q2s+, J2s+, T2s+, 92s+, 82s+, 72s+, 62s+, 52s+, 42s+, 32s
        for (uint8_t r1 = 12; r1 > 0; --r1) {  // A down to 3
            for (uint8_t r2 = 0; r2 < r1; ++r2) {
                uint16_t idx = r1 * 13 + r2;  // Suited
                range[idx] = 1.0f;
            }
        }
        // 32s is marginal but included
        range[1 * 13 + 0] = 1.0f;  // 32s
        
        // All pairs: 22+
        for (uint8_t r = 0; r < 13; ++r) {
            range[r * 13 + r] = 1.0f;
        }
        
        // Offsuit hands
        // A2o+, K2o+, Q2o+
        for (uint8_t r = 0; r < 11; ++r) {  // 2 to Q
            range[r * 13 + 12] = 1.0f;  // Axo
            if (r <= 1) range[r * 13 + 11] = 1.0f;  // K2o+, K3o+
        }
        range[0 * 13 + 11] = 1.0f;  // K2o
        range[1 * 13 + 11] = 1.0f;  // K3o
        
        // J4o+, T6o+, 96o+, 86o+, 75o+, 65o+
        // J4o+ (r2=3=4, r1=9=J)
        for (uint8_t r = 3; r < 9; ++r) {
            range[r * 13 + 9] = 1.0f;
        }
        // T6o+ (r2=4=6, r1=8=T)
        for (uint8_t r = 4; r < 8; ++r) {
            range[r * 13 + 8] = 1.0f;
        }
        // 96o+ (r2=4=6, r1=7=9)
        range[4 * 13 + 7] = 1.0f;
        range[5 * 13 + 7] = 1.0f;
        // 86o+ (r2=4=6, r1=6=8)
        range[4 * 13 + 6] = 1.0f;
        range[5 * 13 + 6] = 1.0f;
        // 75o+ (r2=3=5, r1=5=7)
        range[3 * 13 + 5] = 1.0f;
        range[4 * 13 + 5] = 1.0f;
        // 65o (r2=3=5, r1=4=6)
        range[3 * 13 + 4] = 1.0f;
        
        // Q2o+
        for (uint8_t r = 0; r < 10; ++r) {
            range[r * 13 + 10] = 1.0f;
        }
        
        return range;
    }
    
    // BB Defending Range vs 2.5bb Open
    // Strategy: 3-bet polarized, call wide, fold bottom 25%
    [[nodiscard]] static RangeWeights get_bb_calling_range() noexcept {
        RangeWeights range = {};
        
        // Call range (excluding 3-bet range)
        // Pairs: JJ-22 (QQ+ is 3-bet)
        for (uint8_t r = 0; r < 9; ++r) {  // 22 to JJ
            range[r * 13 + r] = 1.0f;
        }
        
        // Suited broadways and connectors
        for (uint8_t r = 7; r < 13; ++r) {  // 9 to A
            for (uint8_t r2 = 0; r2 < r; ++r2) {
                if (r >= 9 || r - r2 <= 3) {  // Broadway or connected
                    range[r * 13 + r2] = 1.0f;
                }
            }
        }
        
        // Suited aces (not wheel)
        for (uint8_t r = 1; r < 12; ++r) {  // A3s to AKs
            range[12 * 13 + r] = 1.0f;
        }
        
        // Offsuit broadways
        for (uint8_t r = 9; r < 13; ++r) {  // J, Q, K, A
            for (uint8_t r2 = 9; r2 < r; ++r2) {
                range[r2 * 13 + r] = 1.0f;
            }
        }
        
        // Offsuit connectors T9o, 98o
        range[8 * 13 + 7] = 1.0f;  // T9o
        range[7 * 13 + 6] = 1.0f;  // 98o
        
        // Remove 3-bet hands from calling range
        auto three_bet = get_bb_threebet_range();
        for (size_t i = 0; i < 169; ++i) {
            if (three_bet[i] > 0.0f) {
                range[i] = 0.0f;
            }
        }
        
        return range;
    }
    
    [[nodiscard]] static RangeWeights get_bb_threebet_range() noexcept {
        RangeWeights range = {};
        
        // Value: QQ+, AKs, AKo
        // QQ+
        range[10 * 13 + 10] = 1.0f;  // QQ
        range[11 * 13 + 11] = 1.0f;  // KK
        range[12 * 13 + 12] = 1.0f;  // AA
        // AKs, AKo
        range[12 * 13 + 11] = 1.0f;  // AKs
        range[11 * 13 + 12] = 1.0f;  // AKo
        
        // Bluffs: A2s-A5s, K5s, suited connectors
        range[12 * 13 + 0] = 1.0f;  // A2s
        range[12 * 13 + 1] = 1.0f;  // A3s
        range[12 * 13 + 2] = 1.0f;  // A4s
        range[12 * 13 + 3] = 1.0f;  // A5s
        range[11 * 13 + 3] = 1.0f;  // K5s
        range[6 * 13 + 5] = 1.0f;   // 76s
        range[7 * 13 + 6] = 1.0f;   // 87s
        
        return range;
    }
    
    [[nodiscard]] static RangeWeights get_bb_fold_range() noexcept {
        RangeWeights range = {};
        
        // Fold bottom 25%: absolute trash hands
        // 72o, 62o, 52o, 42o, 32o and similar
        
        // Offsuit trash
        range[0 * 13 + 5] = 1.0f;  // 72o
        range[0 * 13 + 4] = 1.0f;  // 62o
        range[0 * 13 + 3] = 1.0f;  // 52o
        range[0 * 13 + 2] = 1.0f;  // 42o
        range[0 * 13 + 1] = 1.0f;  // 32o
        
        // More trash offsuit
        range[1 * 13 + 5] = 1.0f;  // 73o
        range[1 * 13 + 4] = 1.0f;  // 63o
        range[1 * 13 + 3] = 1.0f;  // 53o
        range[1 * 13 + 2] = 1.0f;  // 43o
        
        range[2 * 13 + 5] = 1.0f;  // 74o
        range[2 * 13 + 4] = 1.0f;  // 64o
        range[2 * 13 + 3] = 1.0f;  // 54o
        
        // Weak suited hands (very rare but included)
        range[2 * 13 + 0] = 1.0f;  // 42s
        
        return range;
    }
    
    // Get combined BB defend range (call + 3-bet)
    [[nodiscard]] static RangeWeights get_bb_defend_range() noexcept {
        RangeWeights calling = get_bb_calling_range();
        RangeWeights three_bet = get_bb_threebet_range();
        RangeWeights combined = {};
        
        for (size_t i = 0; i < 169; ++i) {
            combined[i] = calling[i] + three_bet[i];
        }
        
        return combined;
    }
};

} // namespace gto
