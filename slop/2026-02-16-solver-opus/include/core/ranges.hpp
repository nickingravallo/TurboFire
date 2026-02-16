#pragma once

#include "hand.hpp"
#include <unordered_set>
#include <unordered_map>

// Standard GTO SB vs BB heads-up ranges
// Based on tournament conditions (50-100bb effective, 0.12bb ante)
// SB opens ~96% of hands, folding only ~4%

class GTORanges {
public:
    // Get SB opening range (standard GTO)
    static std::unordered_set<Hand, HandHash> get_sb_opening_range() {
        std::unordered_set<Hand, HandHash> range;
        
        // All pairs
        for (int r = 0; r < 13; r++) {
            range.insert(Hand(r, r, true));  // Pairs (suited flag doesn't matter)
        }
        
        // All suited hands
        for (int r1 = 12; r1 >= 0; r1--) {
            for (int r2 = r1 - 1; r2 >= 0; r2--) {
                range.insert(Hand(r1, r2, true));
            }
        }
        
        // Offsuit hands (standard GTO opening)
        // A2o+
        for (int r = 0; r < 12; r++) {
            range.insert(Hand(12, r, false));
        }
        
        // K2o+
        for (int r = 0; r < 11; r++) {
            range.insert(Hand(11, r, false));
        }
        
        // Q2o+
        for (int r = 0; r < 10; r++) {
            range.insert(Hand(10, r, false));
        }
        
        // J4o+ (J2o-J3o fold)
        for (int r = 3; r < 9; r++) {
            range.insert(Hand(9, r, false));
        }
        
        // T6o+ (T2o-T5o fold)
        for (int r = 5; r < 8; r++) {
            range.insert(Hand(8, r, false));
        }
        
        // 96o+ (92o-95o fold)
        for (int r = 5; r < 7; r++) {
            range.insert(Hand(7, r, false));
        }
        
        // 86o+ (82o-85o fold)
        for (int r = 5; r < 6; r++) {
            range.insert(Hand(6, r, false));
        }
        
        // 75o, 65o
        range.insert(Hand(6, 4, false));  // 75o
        range.insert(Hand(5, 4, false));  // 65o
        
        return range;
    }
    
    // Get BB defense range vs SB open (2.5bb)
    // Returns map of hand -> action frequency (simplified for now)
    static std::unordered_map<Hand, std::string, HandHash> get_bb_defense_range() {
        std::unordered_map<Hand, std::string, HandHash> defense;
        
        // 3-bet range (strong hands + some bluffs)
        auto sb_range = get_sb_opening_range();
        for (const auto& hand : sb_range) {
            // Simplified: strong hands 3-bet, medium call, weak fold
            // In real GTO, there's mixing - this is simplified
            if (hand.rank1 == hand.rank2 ||  // Pairs
                (hand.rank1 == 12 && hand.rank2 >= 9) ||  // AK, AQ, AJ
                (hand.rank1 == 11 && hand.rank2 >= 10)) {  // KQ
                defense[hand] = "3bet";
            } else if (hand.rank1 >= 9 || hand.suited) {
                defense[hand] = "call";
            } else {
                defense[hand] = "fold";
            }
        }
        
        return defense;
    }
    
    // Check if hand is in SB opening range
    static bool is_sb_open(const Hand& hand) {
        auto range = get_sb_opening_range();
        return range.find(hand) != range.end();
    }
    
    // Get all possible hands (13x13 grid)
    static std::vector<Hand> get_all_hands() {
        std::vector<Hand> hands;
        
        // Pairs (diagonal)
        for (int r = 12; r >= 0; r--) {
            hands.push_back(Hand(r, r, true));
        }
        
        // Suited hands (upper triangle)
        for (int r1 = 12; r1 >= 1; r1--) {
            for (int r2 = r1 - 1; r2 >= 0; r2--) {
                hands.push_back(Hand(r1, r2, true));
            }
        }
        
        // Offsuit hands (lower triangle)
        for (int r1 = 12; r1 >= 1; r1--) {
            for (int r2 = r1 - 1; r2 >= 0; r2--) {
                hands.push_back(Hand(r1, r2, false));
            }
        }
        
        return hands;
    }
};
