#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace poker {

// Card representation: 0-51
// Bits 0-3: suit (0-3: ♠, ♥, ♦, ♣)
// Bits 4-7: rank (0-12: 2, 3, ..., T, J, Q, K, A)
class Card {
public:
    static constexpr uint8_t SUIT_SPADES = 0;
    static constexpr uint8_t SUIT_HEARTS = 1;
    static constexpr uint8_t SUIT_DIAMONDS = 2;
    static constexpr uint8_t SUIT_CLUBS = 3;
    
    static constexpr std::array<char, 4> SUIT_CHARS = {'s', 'h', 'd', 'c'};
    static constexpr std::array<char, 13> RANK_CHARS = {'2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A'};
    
    // Unicode suit characters for display
    static constexpr std::array<const char*, 4> SUIT_UNICODE = {"♠", "♥", "♦", "♣"};
    
    Card() : value_(0) {}
    explicit Card(uint8_t value) : value_(value) {}
    Card(uint8_t rank, uint8_t suit);
    
    uint8_t value() const { return value_; }
    uint8_t rank() const { return value_ >> 2; }
    uint8_t suit() const { return value_ & 0x3; }
    
    bool isValid() const { return value_ < 52; }
    
    std::string toString() const;
    static Card fromString(const std::string& str);
    
    bool operator==(const Card& other) const { return value_ == other.value_; }
    bool operator!=(const Card& other) const { return value_ != other.value_; }
    bool operator<(const Card& other) const { return value_ < other.value_; }
    
private:
    uint8_t value_;
};

} // namespace poker
