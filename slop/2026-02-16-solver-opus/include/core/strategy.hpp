#pragma once

#include "action.hpp"
#include <unordered_map>
#include <string>

// Represents action frequencies for a hand
struct Strategy {
    double fold_freq = 0.0;
    double check_freq = 0.0;
    double call_freq = 0.0;
    double bet_freq = 0.0;
    double raise_freq = 0.0;
    double allin_freq = 0.0;
    
    // Get dominant action (for color coding)
    std::string get_dominant_action() const {
        double max_freq = fold_freq;
        std::string action = "fold";
        
        if (check_freq + call_freq > max_freq) {
            max_freq = check_freq + call_freq;
            action = "call";
        }
        if (bet_freq + raise_freq > max_freq) {
            max_freq = bet_freq + raise_freq;
            action = "bet";
        }
        if (allin_freq > max_freq) {
            max_freq = allin_freq;
            action = "allin";
        }
        
        return action;
    }
    
    // Get total frequency (should sum to ~1.0)
    double get_total() const {
        return fold_freq + check_freq + call_freq + bet_freq + raise_freq + allin_freq;
    }
};
