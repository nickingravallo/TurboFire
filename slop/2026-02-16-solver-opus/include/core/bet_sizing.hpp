#pragma once

#include <vector>
#include <algorithm>

class BetSizing {
public:
    // Get valid bet sizes as percentages of pot
    // Returns: 33%, 50%, 75%, 125% pot + all-in (if applicable)
    static std::vector<int32_t> get_bet_sizes(int32_t pot, int32_t effective_stack) {
        std::vector<int32_t> sizes;
        
        // Standard bet sizes as percentages of pot
        sizes.push_back((pot * 33) / 100);      // 33% pot
        sizes.push_back((pot * 50) / 100);      // 50% pot
        sizes.push_back((pot * 75) / 100);      // 75% pot
        sizes.push_back((pot * 125) / 100);     // 125% pot
        
        // Filter sizes that exceed effective stack
        sizes.erase(std::remove_if(sizes.begin(), sizes.end(),
            [effective_stack](int32_t s) { return s > effective_stack; }),
            sizes.end());
        
        // All-in always available (unless already all-in)
        if (effective_stack > 0) {
            sizes.push_back(effective_stack);
        }
        
        // Remove duplicates and sort
        std::sort(sizes.begin(), sizes.end());
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
        
        return sizes;
    }
    
    // Get valid raise sizes (must be >= 2x last bet)
    static std::vector<int32_t> get_raise_sizes(int32_t pot, int32_t bet_to_call, 
                                                 int32_t last_bet_size, int32_t effective_stack) {
        std::vector<int32_t> raise_sizes;
        
        // Minimum raise is 2x the last bet size
        int32_t min_raise = bet_to_call + (2 * last_bet_size);
        
        // Standard raise sizes: 33%, 50%, 75%, 125% pot
        std::vector<int32_t> pot_sizes = {
            (pot * 33) / 100,
            (pot * 50) / 100,
            (pot * 75) / 100,
            (pot * 125) / 100
        };
        
        for (int32_t size : pot_sizes) {
            int32_t raise_amount = bet_to_call + size;
            if (raise_amount >= min_raise && raise_amount <= effective_stack) {
                raise_sizes.push_back(raise_amount);
            }
        }
        
        // All-in raise
        if (effective_stack >= min_raise) {
            raise_sizes.push_back(effective_stack);
        }
        
        // Remove duplicates and sort
        std::sort(raise_sizes.begin(), raise_sizes.end());
        raise_sizes.erase(std::unique(raise_sizes.begin(), raise_sizes.end()), raise_sizes.end());
        
        return raise_sizes;
    }
};
