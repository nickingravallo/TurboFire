#include "card/evaluator.h"
#include <array>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <string>

namespace poker {

// Static initialization
std::array<uint16_t, 7937> HandEvaluator::five_card_hash_table_;
std::array<uint32_t, 8192> HandEvaluator::flush_check_table_;
std::array<uint16_t, 7937> HandEvaluator::unique5_table_;

// Card rank multipliers for perfect hash
static constexpr std::array<uint32_t, 5> RANK_MULTIPLIERS = {1, 13, 169, 2197, 28561};

// Hand rank values
static constexpr std::array<uint16_t, 10> HAND_TYPE_VALUES = {
    0,      // Nothing (unused)
    1277,   // One pair
    4137,   // Two pair  
    4995,   // Three of a kind
    5853,   // Straight
    5863,   // Flush
    7140,   // Full house
    7296,   // Four of a kind
    7452    // Straight flush
};

void HandEvaluator::init() {
    // Initialize flush check table
    // Maps bit patterns of suits to flush detection
    for (uint32_t i = 0; i < 8192; ++i) {
        // Count bits in pattern
        uint32_t count = __builtin_popcount(i);
        if (count >= 5) {
            // This pattern has a flush
            flush_check_table_[i] = 1; // Mark as flush
        } else {
            flush_check_table_[i] = 0;
        }
    }
    
    // Initialize 5-card hash table
    // Maps perfect hash of 5 cards to hand rank
    // Initialize with sequential values based on hand strength
    
    // This is a simplified initialization
    // In production, use precomputed tables from Two Plus Two evaluator
    for (uint32_t i = 0; i < 7937; ++i) {
        // Placeholder - should use actual hand strength table
        five_card_hash_table_[i] = static_cast<uint16_t>(i % 7463);
        unique5_table_[i] = static_cast<uint16_t>(i);
    }
}

HandEvaluator::HandEvaluator() {
    // Ensure tables are initialized
    static bool initialized = false;
    if (!initialized) {
        init();
        initialized = true;
    }
}

uint32_t HandEvaluator::getSuitBits(const std::array<uint8_t, 7>& cards, uint8_t suit) {
    uint32_t bits = 0;
    for (uint8_t card : cards) {
        if ((card & 0x3) == suit) {
            bits |= (1 << (card >> 2));
        }
    }
    return bits;
}

uint32_t HandEvaluator::hashFiveCards(uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5) {
    // Perfect hash for 5 cards using ranks
    uint32_t hash = 0;
    hash += RANK_MULTIPLIERS[0] * (c1 >> 2);
    hash += RANK_MULTIPLIERS[1] * (c2 >> 2);
    hash += RANK_MULTIPLIERS[2] * (c3 >> 2);
    hash += RANK_MULTIPLIERS[3] * (c4 >> 2);
    hash += RANK_MULTIPLIERS[4] * (c5 >> 2);
    return hash % 7937;
}

uint16_t HandEvaluator::evaluate(const std::array<Card, 7>& cards) const {
    std::array<uint8_t, 7> values;
    for (size_t i = 0; i < 7; ++i) {
        values[i] = cards[i].value();
    }
    return evaluate(values);
}

uint16_t HandEvaluator::evaluate(const std::array<uint8_t, 7>& cards) const {
    // Check for flush
    uint32_t suit_bits[4] = {0, 0, 0, 0};
    for (uint8_t card : cards) {
        suit_bits[card & 0x3] |= (1 << (card >> 2));
    }
    
    bool has_flush = false;
    uint8_t flush_suit = 0;
    for (uint8_t s = 0; s < 4; ++s) {
        if (__builtin_popcount(suit_bits[s]) >= 5) {
            has_flush = true;
            flush_suit = s;
            break;
        }
    }
    
    // Check for straight flush
    if (has_flush) {
        uint32_t flush_cards = suit_bits[flush_suit];
        // Check for straight in flush cards
        // Simplified - check for 5+ consecutive bits
        for (int i = 12; i >= 4; --i) {
            if ((flush_cards & (1 << i)) && 
                (flush_cards & (1 << (i-1))) &&
                (flush_cards & (1 << (i-2))) &&
                (flush_cards & (1 << (i-3))) &&
                (flush_cards & (1 << (i-4)))) {
                // Straight flush
                return HAND_TYPE_VALUES[8] + i - 4;
            }
        }
        // Wheel straight flush (A-5)
        if ((flush_cards & (1 << 12)) && 
            (flush_cards & (1 << 0)) &&
            (flush_cards & (1 << 1)) &&
            (flush_cards & (1 << 2)) &&
            (flush_cards & (1 << 3))) {
            return HAND_TYPE_VALUES[8]; // Lowest straight flush
        }
    }
    
    // Count card ranks
    uint8_t rank_counts[13] = {0};
    for (uint8_t card : cards) {
        rank_counts[card >> 2]++;
    }
    
    // Check for four of a kind
    for (int r = 12; r >= 0; --r) {
        if (rank_counts[r] == 4) {
            // Find kicker
            for (int k = 12; k >= 0; --k) {
                if (k != r && rank_counts[k] > 0) {
                    return HAND_TYPE_VALUES[7] + r * 13 + k;
                }
            }
        }
    }
    
    // Check for full house
    for (int r3 = 12; r3 >= 0; --r3) {
        if (rank_counts[r3] == 3) {
            for (int r2 = 12; r2 >= 0; --r2) {
                if (r2 != r3 && rank_counts[r2] >= 2) {
                    return HAND_TYPE_VALUES[6] + r3 * 13 + r2;
                }
            }
        }
    }
    
    // Check for flush (if we already found one above)
    if (has_flush) {
        // Get ranks of flush cards, sorted descending
        uint16_t flush_ranks[7];
        int num_flush = 0;
        for (uint8_t card : cards) {
            if ((card & 0x3) == flush_suit) {
                flush_ranks[num_flush++] = 12 - (card >> 2);
            }
        }
        std::sort(flush_ranks, flush_ranks + num_flush, std::greater<uint16_t>());
        
        // Take top 5
        uint16_t result = HAND_TYPE_VALUES[5];
        for (int i = 0; i < 5; ++i) {
            result = result * 13 + flush_ranks[i];
        }
        return result;
    }
    
    // Check for straight
    for (int i = 12; i >= 4; --i) {
        if (rank_counts[i] && rank_counts[i-1] && rank_counts[i-2] && 
            rank_counts[i-3] && rank_counts[i-4]) {
            return HAND_TYPE_VALUES[4] + i - 4;
        }
    }
    // Wheel straight (A-5)
    if (rank_counts[12] && rank_counts[0] && rank_counts[1] && 
        rank_counts[2] && rank_counts[3]) {
        return HAND_TYPE_VALUES[4];
    }
    
    // Check for three of a kind
    for (int r = 12; r >= 0; --r) {
        if (rank_counts[r] == 3) {
            uint16_t result = HAND_TYPE_VALUES[3] + r;
            int kickers = 0;
            for (int k = 12; k >= 0 && kickers < 2; --k) {
                if (k != r && rank_counts[k] > 0) {
                    result = result * 13 + k;
                    kickers++;
                }
            }
            return result;
        }
    }
    
    // Check for two pair
    for (int r1 = 12; r1 >= 0; --r1) {
        if (rank_counts[r1] == 2) {
            for (int r2 = r1 - 1; r2 >= 0; --r2) {
                if (rank_counts[r2] == 2) {
                    // Find kicker
                    for (int k = 12; k >= 0; --k) {
                        if (k != r1 && k != r2 && rank_counts[k] > 0) {
                            return HAND_TYPE_VALUES[2] + r1 * 169 + r2 * 13 + k;
                        }
                    }
                }
            }
        }
    }
    
    // Check for one pair
    for (int r = 12; r >= 0; --r) {
        if (rank_counts[r] == 2) {
            uint16_t result = HAND_TYPE_VALUES[1] + r;
            int kickers = 0;
            for (int k = 12; k >= 0 && kickers < 3; --k) {
                if (k != r && rank_counts[k] > 0) {
                    result = result * 13 + k;
                    kickers++;
                }
            }
            return result;
        }
    }
    
    // High card
    uint16_t result = 0;
    int cards_used = 0;
    for (int r = 12; r >= 0 && cards_used < 5; --r) {
        if (rank_counts[r] > 0) {
            result = result * 13 + r;
            cards_used++;
        }
    }
    return result;
}

uint16_t HandEvaluator::evaluate5(Card c1, Card c2, Card c3, Card c4, Card c5) const {
    std::array<uint8_t, 7> cards = {
        c1.value(), c2.value(), c3.value(), c4.value(), c5.value(), 0, 0
    };
    return evaluate(cards);
}

uint16_t HandEvaluator::evaluate6(Card c1, Card c2, Card c3, Card c4, Card c5, Card c6) const {
    std::array<uint8_t, 7> cards = {
        c1.value(), c2.value(), c3.value(), c4.value(), c5.value(), c6.value(), 0
    };
    return evaluate(cards);
}

uint16_t HandEvaluator::evaluate7(Card c1, Card c2, Card c3, Card c4, Card c5, Card c6, Card c7) const {
    std::array<uint8_t, 7> cards = {
        c1.value(), c2.value(), c3.value(), c4.value(), c5.value(), c6.value(), c7.value()
    };
    return evaluate(cards);
}

std::string HandEvaluator::getHandType(uint16_t rank) {
    if (rank >= HAND_TYPE_VALUES[8]) return "Straight Flush";
    if (rank >= HAND_TYPE_VALUES[7]) return "Four of a Kind";
    if (rank >= HAND_TYPE_VALUES[6]) return "Full House";
    if (rank >= HAND_TYPE_VALUES[5]) return "Flush";
    if (rank >= HAND_TYPE_VALUES[4]) return "Straight";
    if (rank >= HAND_TYPE_VALUES[3]) return "Three of a Kind";
    if (rank >= HAND_TYPE_VALUES[2]) return "Two Pair";
    if (rank >= HAND_TYPE_VALUES[1]) return "One Pair";
    return "High Card";
}

} // namespace poker
