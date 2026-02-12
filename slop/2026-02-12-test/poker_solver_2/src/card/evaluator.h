#pragma once

#include <array>
#include <cstdint>
#include "card/card.h"

namespace poker {

// Hand rank constants
enum class HandRank : uint16_t {
    HIGH_CARD = 0,
    ONE_PAIR = 1277,
    TWO_PAIR = 4137,
    THREE_KIND = 4995,
    STRAIGHT = 5853,
    FLUSH = 5863,
    FULL_HOUSE = 7140,
    FOUR_KIND = 7296,
    STRAIGHT_FLUSH = 7452
};

// Fast 7-card hand evaluator using perfect hash
// Uses lookup tables (~200KB) for O(1) evaluation
class HandEvaluator {
public:
    HandEvaluator();
    
    // Evaluate 7 cards and return a rank (higher is better)
    // Input: array of 7 card values (0-51)
    // Returns: 0-7462 (rank within hand type)
    uint16_t evaluate(const std::array<Card, 7>& cards) const;
    uint16_t evaluate(const std::array<uint8_t, 7>& card_values) const;
    
    // Evaluate 5-7 cards (for convenience)
    uint16_t evaluate5(Card c1, Card c2, Card c3, Card c4, Card c5) const;
    uint16_t evaluate6(Card c1, Card c2, Card c3, Card c4, Card c5, Card c6) const;
    uint16_t evaluate7(Card c1, Card c2, Card c3, Card c4, Card c5, Card c6, Card c7) const;
    
    // Get hand type string
    static std::string getHandType(uint16_t rank);
    
    // Initialize lookup tables (called once)
    static void init();
    
private:
    // Lookup tables
    static std::array<uint16_t, 7937> five_card_hash_table_;
    static std::array<uint32_t, 8192> flush_check_table_;
    static std::array<uint16_t, 7937> unique5_table_;
    
    // Bit manipulation for flush detection
    static uint32_t getSuitBits(const std::array<uint8_t, 7>& cards, uint8_t suit);
    
    // Perfect hash function for 5 cards
    static uint32_t hashFiveCards(uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5);
};

} // namespace poker
