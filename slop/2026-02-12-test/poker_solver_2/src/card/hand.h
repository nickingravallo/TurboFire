#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "card/card.h"

namespace poker {

// 2-card hand representation
class Hand {
public:
    Hand() : cards_{Card(0), Card(1)} {}
    Hand(Card c1, Card c2);
    Hand(uint8_t c1, uint8_t c2);
    
    Card card1() const { return cards_[0]; }
    Card card2() const { return cards_[1]; }
    
    bool isSuited() const;
    bool isPair() const;
    bool isConnector() const;
    
    // Get canonical representation for isomorphic abstraction
    // Returns hand type (e.g., "AKs", "72o", "AA")
    std::string toString() const;
    std::string toDisplayString() const; // With Unicode suits
    
    // For sorting/grouping
    uint16_t getCanonicalId() const;
    
    // Hand strength without board (preflop ranking)
    uint16_t getPreflopRank() const;
    
    bool operator==(const Hand& other) const;
    bool operator<(const Hand& other) const;
    
private:
    std::array<Card, 2> cards_;
    void normalize(); // Ensure card1 > card2
};

// Represents all 1326 possible 2-card hands
class HandRange {
public:
    static constexpr uint32_t NUM_HANDS = 1326;
    
    HandRange();
    
    // Initialize with uniform weights (0.0-1.0)
    void setUniform(float weight);
    
    // Initialize standard GTO ranges
    void setSBOpen100bb();  // ~45% open
    void setBBDefendvsSB(); // ~65% defend
    void setBB3betvsSB();   // ~10% 3bet
    
    // Set from hand notation (e.g., "55+,A2s+,K9s+,Q9s+,J9s+,T8s+,97s+,87s,76s,ATo+,KJo+")
    void setFromNotation(const std::string& notation);
    
    // Get weight for a specific hand (0-1325 index)
    float getWeight(uint32_t hand_index) const;
    float getWeight(const Hand& hand) const;
    
    // Set weight for a specific hand
    void setWeight(uint32_t hand_index, float weight);
    void setWeight(const Hand& hand, float weight);
    
    // Get all hands with weight > 0
    std::vector<std::pair<Hand, float>> getActiveHands() const;
    
    // Remove hands that conflict with board
    void removeBlockedCards(const std::vector<Card>& board);
    
    // Normalize so total weight = 1.0
    void normalize();
    
private:
    std::array<float, NUM_HANDS> weights_;
    
    // Map hand to index 0-1325
    static uint32_t handToIndex(const Hand& hand);
    
public:
    static Hand indexToHand(uint32_t index);
};

} // namespace poker
