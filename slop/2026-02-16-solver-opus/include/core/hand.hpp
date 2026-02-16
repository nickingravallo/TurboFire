#pragma once

#include "card.hpp"
#include <cstdint>
#include <string>
#include <vector>

// Represents a starting hand (e.g., AA, AKs, AKo)
struct Hand {
    uint8_t rank1;  // Higher rank (0-12, 2-A)
    uint8_t rank2;  // Lower rank (0-12, 2-A)
    bool suited;     // true for suited, false for offsuit (pairs are always "suited")
    
    Hand() : rank1(0), rank2(0), suited(false) {}
    Hand(uint8_t r1, uint8_t r2, bool s) : rank1(r1), rank2(r2), suited(s) {}
    
    // Create from string like "AA", "AKs", "AKo"
    static Hand from_string(const std::string& str);
    
    std::string to_string() const {
        const char ranks[] = "23456789TJQKA";
        std::string result;
        result += ranks[rank1];
        result += ranks[rank2];
        if (rank1 != rank2) {
            result += suited ? 's' : 'o';
        }
        return result;
    }
    
    bool operator==(const Hand& other) const {
        return rank1 == other.rank1 && rank2 == other.rank2 && suited == other.suited;
    }
    
    // Get all card combinations for this hand
    std::vector<std::pair<Card, Card>> get_combinations() const;
};

// Hash function for Hand (for use in unordered_map)
struct HandHash {
    std::size_t operator()(const Hand& h) const {
        return (h.rank1 << 8) | (h.rank2 << 1) | (h.suited ? 1 : 0);
    }
};
