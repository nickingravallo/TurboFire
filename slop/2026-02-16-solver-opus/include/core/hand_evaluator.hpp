#pragma once

#include "card.hpp"
#include <cstdint>
#include <vector>

// Hand ranks (higher is better)
enum class HandRank {
    HIGH_CARD = 0,
    PAIR,
    TWO_PAIR,
    THREE_OF_A_KIND,
    STRAIGHT,
    FLUSH,
    FULL_HOUSE,
    FOUR_OF_A_KIND,
    STRAIGHT_FLUSH
};

struct HandValue {
    HandRank rank;
    uint32_t value;  // Comparable value for tie-breaking
    
    bool operator>(const HandValue& other) const {
        if (rank != other.rank) {
            return static_cast<int>(rank) > static_cast<int>(other.rank);
        }
        return value > other.value;
    }
    
    bool operator==(const HandValue& other) const {
        return rank == other.rank && value == other.value;
    }
};

class HandEvaluator {
public:
    // Evaluate best 5-card hand from 7 cards (2 hole + 5 board)
    static HandValue evaluate_7cards(const Card& c1, const Card& c2, const Card& c3,
                                      const Card& c4, const Card& c5, const Card& c6,
                                      const Card& c7);
    
    // Evaluate best 5-card hand from 5 cards
    static HandValue evaluate_5cards(const Card& c1, const Card& c2, const Card& c3,
                                      const Card& c4, const Card& c5);
    
    // Evaluate hand with board cards
    static HandValue evaluate_with_board(const std::vector<Card>& hole_cards,
                                         const std::vector<Card>& board);
    
    // Compare two hands (returns 1 if hand1 wins, -1 if hand2 wins, 0 if tie)
    static int compare_hands(const HandValue& h1, const HandValue& h2);
    
private:
    // Helper: evaluate all combinations of 5 cards from 7
    static HandValue evaluate_best_5_from_7(const std::vector<Card>& cards);
    
    // Helper: evaluate 5 cards
    static HandValue evaluate_5card_hand(const std::vector<Card>& cards);
    
    // Helper: check for flush
    static bool is_flush(const std::vector<Card>& cards);
    
    // Helper: check for straight
    static bool is_straight(const std::vector<uint8_t>& ranks);
    
    // Helper: get rank counts
    static std::vector<int> get_rank_counts(const std::vector<Card>& cards);
};
