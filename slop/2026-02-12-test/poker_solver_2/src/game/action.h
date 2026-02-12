#pragma once

#include <cstdint>
#include <string>
#include "../core/game_state.h"

namespace poker {

// Bet sizing options (supplementary to ActionType)
enum class BetSize : uint8_t {
    PERCENT_20 = 0,   // 20% of pot
    PERCENT_33 = 1,   // 33% of pot
    PERCENT_52 = 2,   // 52% of pot
    PERCENT_100 = 3,  // 100% of pot
    PERCENT_123 = 4,  // 123% of pot (overbet)
    RAISE_2_5X = 5,   // 2.5x raise
    ALL_IN = 6
};

// Action structure (legacy - use one from game_state.h)
// This is kept for backward compatibility

// Street (betting round)
enum class Street : uint8_t {
    PREFLOP = 0,
    FLOP = 1,
    TURN = 2,
    RIVER = 3,
    SHOWDOWN = 4
};

// Calculate bet amount based on pot size and bet sizing
float calculateBetAmount(float pot_size, BetSize size);

// Calculate raise amount (2.5x the current bet)
float calculateRaiseAmount(float current_bet, float pot_size);

// Additional toString for ActionType with BetSize context
std::string actionToString(ActionType type, BetSize bet_size, float amount);

} // namespace poker
