#include "card/card.h"
#include <cstdint>
#include <stdexcept>

namespace poker {

Card::Card(uint8_t rank, uint8_t suit) {
    if (rank > 12 || suit > 3) {
        throw std::invalid_argument("Invalid card rank or suit");
    }
    value_ = (rank << 2) | suit;
}

std::string Card::toString() const {
    if (!isValid()) return "??";
    std::string result;
    result += RANK_CHARS[rank()];
    result += SUIT_CHARS[suit()];
    return result;
}

Card Card::fromString(const std::string& str) {
    if (str.length() < 2) {
        throw std::invalid_argument("Card string too short");
    }
    
    char rank_char = str[0];
    char suit_char = str[1];
    
    uint8_t rank = 0;
    switch (rank_char) {
        case '2': rank = 0; break;
        case '3': rank = 1; break;
        case '4': rank = 2; break;
        case '5': rank = 3; break;
        case '6': rank = 4; break;
        case '7': rank = 5; break;
        case '8': rank = 6; break;
        case '9': rank = 7; break;
        case 'T': case 't': rank = 8; break;
        case 'J': case 'j': rank = 9; break;
        case 'Q': case 'q': rank = 10; break;
        case 'K': case 'k': rank = 11; break;
        case 'A': case 'a': rank = 12; break;
        default: throw std::invalid_argument("Invalid rank character");
    }
    
    uint8_t suit = 0;
    switch (suit_char) {
        case 's': case 'S': suit = SUIT_SPADES; break;
        case 'h': case 'H': suit = SUIT_HEARTS; break;
        case 'd': case 'D': suit = SUIT_DIAMONDS; break;
        case 'c': case 'C': suit = SUIT_CLUBS; break;
        case '♠': suit = SUIT_SPADES; break;
        case '♥': suit = SUIT_HEARTS; break;
        case '♦': suit = SUIT_DIAMONDS; break;
        case '♣': suit = SUIT_CLUBS; break;
        default: throw std::invalid_argument("Invalid suit character");
    }
    
    return Card(rank, suit);
}

} // namespace poker
