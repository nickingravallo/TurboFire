#include "game/board.h"
#include <sstream>

namespace poker {

void Board::setFlop(Card c1, Card c2, Card c3) {
    cards_.clear();
    cards_.push_back(c1);
    cards_.push_back(c2);
    cards_.push_back(c3);
}

void Board::setTurn(Card c) {
    if (cards_.size() == 3) {
        cards_.push_back(c);
    }
}

void Board::setRiver(Card c) {
    if (cards_.size() == 4) {
        cards_.push_back(c);
    }
}

void Board::addCard(Card c) {
    if (cards_.size() < 5) {
        cards_.push_back(c);
    }
}

void Board::clear() {
    cards_.clear();
}

int Board::street() const {
    switch (cards_.size()) {
        case 0: return 0; // Preflop
        case 3: return 1; // Flop
        case 4: return 2; // Turn
        case 5: return 3; // River
        default: return -1;
    }
}

bool Board::blocksHand(const Hand& hand) const {
    for (const Card& c : cards_) {
        if (hand.card1() == c || hand.card2() == c) {
            return true;
        }
    }
    return false;
}

std::string Board::toString() const {
    std::string result;
    for (const Card& c : cards_) {
        result += c.toString();
    }
    return result;
}

Board Board::fromString(const std::string& str) {
    Board board;
    for (size_t i = 0; i < str.length(); i += 2) {
        if (i + 1 < str.length()) {
            board.addCard(Card::fromString(str.substr(i, 2)));
        }
    }
    return board;
}

bool Board::isPaired() const {
    if (cards_.size() < 2) return false;
    for (size_t i = 0; i < cards_.size(); ++i) {
        for (size_t j = i + 1; j < cards_.size(); ++j) {
            if (cards_[i].rank() == cards_[j].rank()) {
                return true;
            }
        }
    }
    return false;
}

bool Board::isMonotone() const {
    if (cards_.size() < 3) return false;
    uint8_t suit = cards_[0].suit();
    for (const Card& c : cards_) {
        if (c.suit() != suit) return false;
    }
    return true;
}

bool Board::isTwoTone() const {
    if (cards_.size() < 3) return false;
    uint8_t suits[4] = {0, 0, 0, 0};
    for (const Card& c : cards_) {
        suits[c.suit()]++;
    }
    int suit_count = 0;
    for (int i = 0; i < 4; ++i) {
        if (suits[i] > 0) suit_count++;
    }
    return suit_count == 2;
}

bool Board::isRainbow() const {
    if (cards_.size() < 3) return false;
    uint8_t suits[4] = {0, 0, 0, 0};
    for (const Card& c : cards_) {
        suits[c.suit()]++;
    }
    int suit_count = 0;
    for (int i = 0; i < 4; ++i) {
        if (suits[i] > 0) suit_count++;
    }
    return suit_count >= 3;
}

bool Board::hasFlushDraw() const {
    if (cards_.size() != 3) return false; // Only on flop
    uint8_t suits[4] = {0, 0, 0, 0};
    for (const Card& c : cards_) {
        suits[c.suit()]++;
    }
    for (int i = 0; i < 4; ++i) {
        if (suits[i] == 2) return true; // Two to a flush
    }
    return false;
}

bool Board::hasStraightDraw() const {
    if (cards_.size() < 3) return false;
    // Check for 4 consecutive ranks or 4-to-a-straight
    uint8_t ranks[13] = {0};
    for (const Card& c : cards_) {
        ranks[c.rank()] = 1;
    }
    
    // Check for 4 consecutive
    for (int i = 0; i <= 9; ++i) {
        int count = 0;
        for (int j = 0; j < 5; ++j) {
            int rank = (i + j) % 13;
            if (ranks[rank]) count++;
        }
        if (count >= 3) return true; // At least 3 cards towards a straight
    }
    
    // Check wheel (A-2-3-4-5)
    if (ranks[12] && ranks[0] && ranks[1] && ranks[2]) return true;
    if (ranks[12] && ranks[0] && ranks[1] && ranks[3]) return true;
    if (ranks[12] && ranks[0] && ranks[2] && ranks[3]) return true;
    if (ranks[12] && ranks[1] && ranks[2] && ranks[3]) return true;
    
    return false;
}

} // namespace poker
