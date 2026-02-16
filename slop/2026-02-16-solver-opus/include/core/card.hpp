#pragma once

#include <cstdint>
#include <string>

// 52-bit card representation
// Bits 0-51 represent each card in deck
// Suit: 0=spades, 1=hearts, 2=diamonds, 3=clubs
// Rank: 0=2, 1=3, ..., 12=A

struct Card {
    uint8_t rank;  // 0-12 (2-A)
    uint8_t suit;  // 0-3 (spades, hearts, diamonds, clubs)
    
    Card() : rank(0), suit(0) {}
    Card(uint8_t r, uint8_t s) : rank(r), suit(s) {}
    
    // Convert to 52-bit representation (bit index)
    uint8_t to_bit() const {
        return suit * 13 + rank;
    }
    
    // Create from bit index
    static Card from_bit(uint8_t bit) {
        return Card(bit % 13, bit / 13);
    }
    
    std::string to_string() const {
        const char ranks[] = "23456789TJQKA";
        const char suits[] = "shdc";
        return std::string(1, ranks[rank]) + std::string(1, suits[suit]);
    }
    
    bool operator==(const Card& other) const {
        return rank == other.rank && suit == other.suit;
    }
};
