#pragma once

#include <vector>
#include <string>
#include "card/card.h"
#include "card/hand.h"

namespace poker {

// Board state: 0-5 community cards
class Board {
public:
    Board() = default;
    
    // Add cards
    void setFlop(Card c1, Card c2, Card c3);
    void setTurn(Card c);
    void setRiver(Card c);
    void addCard(Card c);
    
    // Clear board
    void clear();
    
    // Getters
    bool hasFlop() const { return cards_.size() >= 3; }
    bool hasTurn() const { return cards_.size() >= 4; }
    bool hasRiver() const { return cards_.size() >= 5; }
    int street() const;
    
    const std::vector<Card>& cards() const { return cards_; }
    Card card(size_t index) const { return cards_[index]; }
    size_t size() const { return cards_.size(); }
    
    // Check if a hand is blocked by board
    bool blocksHand(const Hand& hand) const;
    
    // String representation
    std::string toString() const;
    static Board fromString(const std::string& str);
    
    // Get board texture info for abstraction
    bool isPaired() const;
    bool isMonotone() const;
    bool isTwoTone() const;
    bool isRainbow() const;
    bool hasFlushDraw() const;
    bool hasStraightDraw() const;
    
private:
    std::vector<Card> cards_;
};

} // namespace poker
