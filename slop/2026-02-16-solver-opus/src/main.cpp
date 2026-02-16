#include "ui/interactive_range_display.hpp"
#include "core/game_state.hpp"
#include "core/card.hpp"
#include <iostream>

int main() {
    try {
        // Initialize SB vs BB heads-up spot
        // Standard: SB = 0.5bb, BB = 1bb, stacks = 100bb
        GameState game_state(0, 1, 100);
        
        // Start at flop (deal 3 community cards)
        // Example flop: As Kh 2c (you can change these)
        Card flop1(12, 0);  // As (Ace of spades)
        Card flop2(11, 1);   // Kh (King of hearts)
        Card flop3(0, 2);    // 2c (2 of clubs)
        
        game_state.initialize_at_flop(flop1, flop2, flop3);
        
        // Create interactive UI
        InteractiveRangeDisplay ui(game_state);
        
        // Run TUI event loop
        ui.run();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
