#include "card/hand.h"
#include <algorithm>
#include <vector>
#include <sstream>

namespace poker {

Hand::Hand(Card c1, Card c2) : cards_{c1, c2} {
    normalize();
}

Hand::Hand(uint8_t c1, uint8_t c2) : cards_{Card(c1), Card(c2)} {
    normalize();
}

void Hand::normalize() {
    // Ensure higher card is first
    if (cards_[0].rank() < cards_[1].rank() ||
        (cards_[0].rank() == cards_[1].rank() && cards_[0].suit() < cards_[1].suit())) {
        std::swap(cards_[0], cards_[1]);
    }
}

bool Hand::isSuited() const {
    return cards_[0].suit() == cards_[1].suit();
}

bool Hand::isPair() const {
    return cards_[0].rank() == cards_[1].rank();
}

bool Hand::isConnector() const {
    if (isPair()) return false;
    int rank_diff = cards_[0].rank() - cards_[1].rank();
    return rank_diff == 1 || (cards_[0].rank() == 12 && cards_[1].rank() == 0); // AK is connector
}

std::string Hand::toString() const {
    std::string result;
    result += Card::RANK_CHARS[cards_[0].rank()];
    result += Card::RANK_CHARS[cards_[1].rank()];
    if (isPair()) {
        // Pairs don't need suffix
    } else if (isSuited()) {
        result += 's';
    } else {
        result += 'o';
    }
    return result;
}

std::string Hand::toDisplayString() const {
    std::string result;
    result += Card::RANK_CHARS[cards_[0].rank()];
    result += Card::SUIT_UNICODE[cards_[0].suit()];
    result += Card::RANK_CHARS[cards_[1].rank()];
    result += Card::SUIT_UNICODE[cards_[1].suit()];
    return result;
}

uint16_t Hand::getCanonicalId() const {
    // For isomorphic abstraction
    // Returns: rank1 * 13 + rank2 + suited_flag * 169
    // Pairs: rank * 13 + rank (0-168)
    // Suited: rank1 * 13 + rank2 + 169 (169-337)
    // Offsuit: rank1 * 13 + rank2 + 338 (338-506)
    uint8_t r1 = cards_[0].rank();
    uint8_t r2 = cards_[1].rank();
    
    if (isPair()) {
        return r1 * 13 + r2; // 0-168
    } else if (isSuited()) {
        return r1 * 13 + r2 + 169; // 169-337
    } else {
        return r1 * 13 + r2 + 338; // 338-506
    }
}

uint16_t Hand::getPreflopRank() const {
    // Simple preflop hand strength ranking
    // Higher is better (0-168 for 169 unique hand types)
    // This is a simplified version - real GTO uses more complex rankings
    
    uint8_t r1 = cards_[0].rank();
    uint8_t r2 = cards_[1].rank();
    
    if (isPair()) {
        // Pairs: AA = 168, 22 = 156
        return 156 + r1;
    }
    
    uint16_t base = 0;
    
    // High card combinations
    // AK = 155, AKs = 154, etc.
    if (r1 == 12) { // Ace high
        base = 130;
    } else if (r1 == 11) { // King high
        base = 104;
    } else if (r1 == 10) { // Queen high
        base = 78;
    } else if (r1 == 9) { // Jack high
        base = 52;
    } else if (r1 == 8) { // Ten high
        base = 26;
    } else {
        base = 0;
    }
    
    base += r2;
    
    // Suited hands ranked slightly higher
    if (isSuited() && !isPair()) {
        base += 13;
    }
    
    return base;
}

bool Hand::operator==(const Hand& other) const {
    return cards_[0] == other.cards_[0] && cards_[1] == other.cards_[1];
}

bool Hand::operator<(const Hand& other) const {
    if (cards_[0].rank() != other.cards_[0].rank())
        return cards_[0].rank() < other.cards_[0].rank();
    if (cards_[1].rank() != other.cards_[1].rank())
        return cards_[1].rank() < other.cards_[1].rank();
    return cards_[0].suit() < other.cards_[0].suit();
}

// HandRange implementation
HandRange::HandRange() {
    weights_.fill(0.0f);
}

void HandRange::setUniform(float weight) {
    weights_.fill(weight);
}

uint32_t HandRange::handToIndex(const Hand& hand) {
    // Map 1326 hands to indices 0-1325
    // Ordered by: pairs (78), suited (312), offsuit (936)
    uint8_t r1 = hand.card1().rank();
    uint8_t r2 = hand.card2().rank();
    uint8_t s1 = hand.card1().suit();
    uint8_t s2 = hand.card2().suit();
    
    if (hand.isPair()) {
        // Pairs: 13 * 12 / 2 = 78 combinations
        // Index by rank and suits
        // (r, s1, s2) where s1 < s2
        uint8_t s_small = std::min(s1, s2);
        uint8_t s_large = std::max(s1, s2);
        uint32_t suit_combo = s_small * 4 + s_large - (s_small + 1) * (s_small + 2) / 2;
        return r1 * 6 + suit_combo;
    } else if (hand.isSuited()) {
        // Suited: 13 * 12 / 2 * 4 = 312 combinations
        uint32_t pair_index = (r1 * (r1 - 1)) / 2 + r2;
        return 78 + pair_index * 4 + s1;
    } else {
        // Offsuit: 13 * 12 / 2 * 12 = 936 combinations
        uint32_t pair_index = (r1 * (r1 - 1)) / 2 + r2;
        // 12 offsuit combinations per rank pair (4*3)
        uint32_t suit_combo = s1 * 3 + s2 - (s1 > s2 ? 1 : 0);
        return 78 + 312 + pair_index * 12 + suit_combo;
    }
}

Hand HandRange::indexToHand(uint32_t index) {
    if (index < 78) {
        // Pairs
        uint8_t rank = index / 6;
        uint8_t suit_combo = index % 6;
        // Convert suit_combo (0-5) to actual suit pairs
        uint8_t suits[2];
        uint8_t count = 0;
        for (uint8_t s1 = 0; s1 < 4; ++s1) {
            for (uint8_t s2 = s1 + 1; s2 < 4; ++s2) {
                if (count == suit_combo) {
                    suits[0] = s1;
                    suits[1] = s2;
                    break;
                }
                count++;
            }
        }
        return Hand(Card(rank, suits[0]), Card(rank, suits[1]));
    } else if (index < 390) {
        // Suited
        uint32_t suited_index = index - 78;
        uint8_t suit = suited_index % 4;
        uint32_t pair_index = suited_index / 4;
        // Reverse pair_index to ranks
        uint8_t r1 = 0, r2 = 0;
        for (uint8_t i = 1; i < 13; ++i) {
            uint32_t pairs = (i * (i - 1)) / 2;
            if (pairs > pair_index) {
                r1 = i;
                r2 = pair_index - ((i - 1) * (i - 2)) / 2;
                break;
            }
        }
        return Hand(Card(r1, suit), Card(r2, suit));
    } else {
        // Offsuit
        uint32_t offsuit_index = index - 390;
        uint32_t suit_combo = offsuit_index % 12;
        uint32_t pair_index = offsuit_index / 12;
        // Reverse pair_index to ranks
        uint8_t r1 = 0, r2 = 0;
        for (uint8_t i = 1; i < 13; ++i) {
            uint32_t pairs = (i * (i - 1)) / 2;
            if (pairs > pair_index) {
                r1 = i;
                r2 = pair_index - ((i - 1) * (i - 2)) / 2;
                break;
            }
        }
        // Convert suit_combo (0-11) to suit pairs
        uint8_t s1 = suit_combo / 3;
        uint8_t s2 = suit_combo % 3;
        if (s2 >= s1) s2++;
        return Hand(Card(r1, s1), Card(r2, s2));
    }
}

float HandRange::getWeight(uint32_t hand_index) const {
    return weights_[hand_index];
}

float HandRange::getWeight(const Hand& hand) const {
    return weights_[handToIndex(hand)];
}

void HandRange::setWeight(uint32_t hand_index, float weight) {
    weights_[hand_index] = weight;
}

void HandRange::setWeight(const Hand& hand, float weight) {
    weights_[handToIndex(hand)] = weight;
}

void HandRange::setSBOpen100bb() {
    // Standard GTO SB open ~45% of hands
    weights_.fill(0.0f);
    
    // All pairs 22+
    for (uint8_t r = 0; r < 13; ++r) {
        for (uint8_t s1 = 0; s1 < 4; ++s1) {
            for (uint8_t s2 = s1 + 1; s2 < 4; ++s2) {
                Hand h(Card(r, s1), Card(r, s2));
                setWeight(h, 1.0f);
            }
        }
    }
    
    // Suited: A2s+, K6s+, Q8s+, J8s+, T8s+, 98s, 87s, 76s, 65s, 54s
    auto addSuitedRange = [&](uint8_t high_rank, uint8_t low_rank_min) {
        for (uint8_t r = low_rank_min; r < high_rank; ++r) {
            for (uint8_t s = 0; s < 4; ++s) {
                Hand h(Card(high_rank, s), Card(r, s));
                setWeight(h, 1.0f);
            }
        }
    };
    
    addSuitedRange(12, 0); // A2s+
    addSuitedRange(11, 4); // K6s+
    addSuitedRange(10, 6); // Q8s+
    addSuitedRange(9, 6);  // J8s+
    addSuitedRange(8, 6);  // T8s+
    addSuitedRange(7, 6);  // 98s
    addSuitedRange(6, 5);  // 87s
    addSuitedRange(5, 4);  // 76s
    addSuitedRange(4, 3);  // 65s
    addSuitedRange(3, 2);  // 54s
    
    // Offsuit: ATo+, KJo+, QJo
    auto addOffsuitRange = [&](uint8_t high_rank, uint8_t low_rank_min) {
        for (uint8_t r = low_rank_min; r < high_rank; ++r) {
            for (uint8_t s1 = 0; s1 < 4; ++s1) {
                for (uint8_t s2 = 0; s2 < 4; ++s2) {
                    if (s1 != s2) {
                        Hand h(Card(high_rank, s1), Card(r, s2));
                        setWeight(h, 1.0f);
                    }
                }
            }
        }
    };
    
    addOffsuitRange(12, 8);  // ATo+
    addOffsuitRange(11, 9);  // KJo+
    addOffsuitRange(10, 9);  // QJo
    
    normalize();
}

void HandRange::setBBDefendvsSB() {
    // Standard BB defend vs SB open ~65%
    // Start with all hands, then remove some folds
    weights_.fill(1.0f);
    
    // Fold worst hands: 72o, 73o, 82o, 83o, 92o, 93o, T2o, T3o, J2o, J3o, Q2o, Q3o, K2o
    auto removeOffsuit = [&](uint8_t high_rank, uint8_t low_rank) {
        for (uint8_t s1 = 0; s1 < 4; ++s1) {
            for (uint8_t s2 = 0; s2 < 4; ++s2) {
                if (s1 != s2) {
                    Hand h(Card(high_rank, s1), Card(low_rank, s2));
                    setWeight(h, 0.0f);
                }
            }
        }
    };
    
    removeOffsuit(5, 0); // 72o
    removeOffsuit(5, 1); // 73o
    removeOffsuit(6, 0); // 82o
    removeOffsuit(6, 1); // 83o
    removeOffsuit(7, 0); // 92o
    removeOffsuit(7, 1); // 93o
    removeOffsuit(8, 0); // T2o
    removeOffsuit(8, 1); // T3o
    removeOffsuit(9, 0); // J2o
    removeOffsuit(9, 1); // J3o
    removeOffsuit(10, 0); // Q2o
    removeOffsuit(10, 1); // Q3o
    removeOffsuit(11, 0); // K2o
    
    normalize();
}

void HandRange::setBB3betvsSB() {
    // Standard BB 3bet vs SB ~10%
    weights_.fill(0.0f);
    
    // Premium hands for 3bet
    // QQ+, AKs, AKo
    auto addPairs = [&](uint8_t min_rank) {
        for (uint8_t r = min_rank; r < 13; ++r) {
            for (uint8_t s1 = 0; s1 < 4; ++s1) {
                for (uint8_t s2 = s1 + 1; s2 < 4; ++s2) {
                    Hand h(Card(r, s1), Card(r, s2));
                    setWeight(h, 1.0f);
                }
            }
        }
    };
    
    addPairs(10); // QQ+
    
    // AK suited and offsuit
    for (uint8_t s1 = 0; s1 < 4; ++s1) {
        for (uint8_t s2 = 0; s2 < 4; ++s2) {
            Hand h(Card(12, s1), Card(11, s2));
            setWeight(h, 1.0f);
        }
    }
    
    normalize();
}

void HandRange::removeBlockedCards(const std::vector<Card>& board) {
    for (uint32_t i = 0; i < NUM_HANDS; ++i) {
        Hand h = indexToHand(i);
        for (const Card& c : board) {
            if (h.card1() == c || h.card2() == c) {
                weights_[i] = 0.0f;
                break;
            }
        }
    }
    normalize();
}

void HandRange::normalize() {
    float total = 0.0f;
    for (float w : weights_) {
        total += w;
    }
    if (total > 0.0f) {
        for (auto& w : weights_) {
            w /= total;
        }
    }
}

std::vector<std::pair<Hand, float>> HandRange::getActiveHands() const {
    std::vector<std::pair<Hand, float>> result;
    for (uint32_t i = 0; i < NUM_HANDS; ++i) {
        if (weights_[i] > 0.0f) {
            result.emplace_back(indexToHand(i), weights_[i]);
        }
    }
    return result;
}

} // namespace poker
