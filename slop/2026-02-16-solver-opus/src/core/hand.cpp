#include "core/hand.hpp"
#include "core/card.hpp"
#include <sstream>
#include <algorithm>
#include <vector>

Hand Hand::from_string(const std::string& str) {
    if (str.length() < 2) {
        return Hand(0, 0, false);
    }
    
    const char ranks[] = "23456789TJQKA";
    
    char r1_char = str[0];
    char r2_char = str[1];
    
    uint8_t rank1 = 0, rank2 = 0;
    for (int i = 0; i < 13; i++) {
        if (ranks[i] == r1_char) rank1 = i;
        if (ranks[i] == r2_char) rank2 = i;
    }
    
    bool suited = false;
    if (str.length() > 2) {
        if (str[2] == 's') suited = true;
        else if (str[2] == 'o') suited = false;
    } else {
        // Pairs are always "suited" (same rank)
        suited = (rank1 == rank2);
    }
    
    // Ensure rank1 >= rank2
    if (rank1 < rank2) {
        std::swap(rank1, rank2);
    }
    
    return Hand(rank1, rank2, suited);
}

std::vector<std::pair<Card, Card>> Hand::get_combinations() const {
    std::vector<std::pair<Card, Card>> combos;
    
    if (rank1 == rank2) {
        // Pair: all combinations of the two ranks
        for (int s1 = 0; s1 < 4; s1++) {
            for (int s2 = s1 + 1; s2 < 4; s2++) {
                combos.push_back({Card(rank1, s1), Card(rank1, s2)});
            }
        }
    } else if (suited) {
        // Suited: same suit, different ranks
        for (int s = 0; s < 4; s++) {
            combos.push_back({Card(rank1, s), Card(rank2, s)});
        }
    } else {
        // Offsuit: different suits
        for (int s1 = 0; s1 < 4; s1++) {
            for (int s2 = 0; s2 < 4; s2++) {
                if (s1 != s2) {
                    combos.push_back({Card(rank1, s1), Card(rank2, s2)});
                }
            }
        }
    }
    
    return combos;
}
