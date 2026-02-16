#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include <numeric>

namespace gto {

// Card representation: 0-51 (13*rank + suit)
// Rank: 0-12 (2-A), Suit: 0-3 (clubs, diamonds, hearts, spades)
[[nodiscard]] constexpr uint8_t make_card(uint8_t rank, uint8_t suit) noexcept {
    return rank * 4 + suit;
}

[[nodiscard]] constexpr uint8_t card_rank(uint8_t card) noexcept {
    return card / 4;
}

[[nodiscard]] constexpr uint8_t card_suit(uint8_t card) noexcept {
    return card % 4;
}

// Hand ranks (0-12): 2, 3, 4, 5, 6, 7, 8, 9, T, J, Q, K, A
// Hand types: High Card (0), Pair (1), Two Pair (2), Three Kind (3), 
//             Straight (4), Flush (5), Full House (6), Four Kind (7), Straight Flush (8)

class HandEvaluator {
public:
    // Bit representations for fast evaluation
    using CardSet = uint64_t;
    
    [[nodiscard]] static CardSet card_to_bit(uint8_t card) noexcept {
        return CardSet{1} << card;
    }
    
    [[nodiscard]] static uint32_t evaluate_7card(const std::array<uint8_t, 7>& cards) noexcept {
        // Build bit masks
        CardSet card_bits = 0;
        std::array<uint8_t, 13> rank_counts = {};
        std::array<uint8_t, 4> suit_counts = {};
        
        for (uint8_t c : cards) {
            card_bits |= card_to_bit(c);
            rank_counts[card_rank(c)]++;
            suit_counts[card_suit(c)]++;
        }
        
        // Check for flush
        int flush_suit = -1;
        for (int s = 0; s < 4; ++s) {
            if (suit_counts[s] >= 5) {
                flush_suit = s;
                break;
            }
        }
        
        // Build rank bit pattern (for straights)
        uint16_t rank_bits = 0;
        for (int r = 0; r < 13; ++r) {
            if (rank_counts[r] > 0) {
                rank_bits |= (1 << r);
            }
        }
        
        // Check for straight
        int straight_high = -1;
        // Check normal straights
        for (int r = 12; r >= 4; --r) {  // From Ace down to 5
            if ((rank_bits & (0x1F << (r - 4))) == (0x1F << (r - 4))) {
                straight_high = r;
                break;
            }
        }
        // Check A-5 straight (wheel)
        if (straight_high == -1 && (rank_bits & 0x100F) == 0x100F) {  // A, 2, 3, 4, 5
            straight_high = 3;  // 5-high straight
        }
        
        // Check for straight flush
        if (flush_suit >= 0 && straight_high >= 0) {
            // Build flush suit card bits
            CardSet flush_bits = 0;
            for (uint8_t c : cards) {
                if (card_suit(c) == flush_suit) {
                    flush_bits |= card_to_bit(c);
                }
            }
            
            // Check for straight in flush suit
            uint16_t flush_rank_bits = 0;
            for (int r = 0; r < 13; ++r) {
                if (flush_bits & (CardSet{0xF} << (r * 4))) {
                    flush_rank_bits |= (1 << r);
                }
            }
            
            int flush_straight_high = -1;
            for (int r = 12; r >= 4; --r) {
                if ((flush_rank_bits & (0x1F << (r - 4))) == (0x1F << (r - 4))) {
                    flush_straight_high = r;
                    break;
                }
            }
            if (flush_straight_high == -1 && (flush_rank_bits & 0x100F) == 0x100F) {
                flush_straight_high = 3;
            }
            
            if (flush_straight_high >= 0) {
                return make_hand_value(8, flush_straight_high);  // Straight flush
            }
        }
        
        // Check for four of a kind
        for (int r = 12; r >= 0; --r) {
            if (rank_counts[r] == 4) {
                // Find kicker
                for (int k = 12; k >= 0; --k) {
                    if (k != r && rank_counts[k] > 0) {
                        return make_hand_value(7, r, k);
                    }
                }
            }
        }
        
        // Check for full house
        int three_kind = -1;
        for (int r = 12; r >= 0; --r) {
            if (rank_counts[r] == 3) {
                three_kind = r;
                break;
            }
        }
        if (three_kind >= 0) {
            for (int r = 12; r >= 0; --r) {
                if (r != three_kind && rank_counts[r] >= 2) {
                    return make_hand_value(6, three_kind, r);
                }
            }
        }
        
        // Check for flush
        if (flush_suit >= 0) {
            uint32_t kickers = 0;
            int count = 0;
            for (int r = 12; r >= 0 && count < 5; --r) {
                for (uint8_t c : cards) {
                    if (card_rank(c) == r && card_suit(c) == flush_suit) {
                        kickers = (kickers << 4) | r;
                        count++;
                        break;
                    }
                }
            }
            return make_hand_value(5, kickers);
        }
        
        // Check for straight
        if (straight_high >= 0) {
            return make_hand_value(4, straight_high);
        }
        
        // Check for three of a kind
        for (int r = 12; r >= 0; --r) {
            if (rank_counts[r] == 3) {
                uint32_t kickers = 0;
                int count = 0;
                for (int k = 12; k >= 0 && count < 2; --k) {
                    if (k != r && rank_counts[k] > 0) {
                        kickers = (kickers << 4) | k;
                        count++;
                    }
                }
                return make_hand_value(3, r, kickers);
            }
        }
        
        // Check for two pair
        int pair1 = -1, pair2 = -1;
        for (int r = 12; r >= 0; --r) {
            if (rank_counts[r] == 2) {
                if (pair1 == -1) {
                    pair1 = r;
                } else {
                    pair2 = r;
                    break;
                }
            }
        }
        if (pair1 >= 0 && pair2 >= 0) {
            // Find kicker
            for (int k = 12; k >= 0; --k) {
                if (k != pair1 && k != pair2 && rank_counts[k] > 0) {
                    return make_hand_value(2, pair1, pair2, k);
                }
            }
        }
        
        // Check for pair
        if (pair1 >= 0) {
            uint32_t kickers = 0;
            int count = 0;
            for (int k = 12; k >= 0 && count < 3; --k) {
                if (k != pair1 && rank_counts[k] > 0) {
                    kickers = (kickers << 4) | k;
                    count++;
                }
            }
            return make_hand_value(1, pair1, kickers);
        }
        
        // High card
        uint32_t kickers = 0;
        int count = 0;
        for (int r = 12; r >= 0 && count < 5; --r) {
            if (rank_counts[r] > 0) {
                kickers = (kickers << 4) | r;
                count++;
            }
        }
        return make_hand_value(0, kickers);
    }
    
    [[nodiscard]] static int compare_hands(uint32_t hand1, uint32_t hand2) noexcept {
        if (hand1 > hand2) return 1;
        if (hand1 < hand2) return -1;
        return 0;
    }

private:
    [[nodiscard]] static constexpr uint32_t make_hand_value(uint8_t hand_type, uint32_t rank1, uint32_t rank2 = 0, uint32_t rank3 = 0) noexcept {
        return (static_cast<uint32_t>(hand_type) << 24) | (rank1 << 16) | (rank2 << 8) | rank3;
    }
};

// Suit equivalence mapping
// Maps any hand to a canonical representation
class SuitEquivalence {
public:
    // Returns canonical suit mapping for board+hand
    // Uses the property that suits are interchangeable based on board texture
    [[nodiscard]] static std::array<uint8_t, 4> get_canonical_mapping(
        const std::array<uint8_t, 5>& board,  // 0-51 or -1 for not dealt
        uint8_t hand1, uint8_t hand2) noexcept {
        
        std::array<uint8_t, 4> suit_map = {0, 1, 2, 3};  // Identity
        
        // Get suits on board
        std::array<bool, 4> board_suits = {};
        int board_suit_count = 0;
        for (int i = 0; i < 5 && board[i] >= 0; ++i) {
            uint8_t s = card_suit(board[i]);
            if (!board_suits[s]) {
                board_suits[s] = true;
                board_suit_count++;
            }
        }
        
        // If fewer than 4 suits on board, we can map equivalent suits
        if (board_suit_count < 4) {
            // Find the first suit not on board
            uint8_t missing_suit = 4;
            for (uint8_t s = 0; s < 4; ++s) {
                if (!board_suits[s]) {
                    missing_suit = s;
                    break;
                }
            }
            
            // Map suits to canonical form
            // Suits on board keep their order, missing suit gets mapped to lowest available
            uint8_t next_suit = 0;
            for (uint8_t s = 0; s < 4; ++s) {
                if (board_suits[s]) {
                    suit_map[s] = next_suit++;
                }
            }
            if (missing_suit < 4) {
                suit_map[missing_suit] = next_suit;
            }
        }
        
        return suit_map;
    }
    
    [[nodiscard]] static uint8_t canonical_card(uint8_t card, const std::array<uint8_t, 4>& suit_map) noexcept {
        uint8_t rank = card_rank(card);
        uint8_t suit = card_suit(card);
        return make_card(rank, suit_map[suit]);
    }
};

} // namespace gto
