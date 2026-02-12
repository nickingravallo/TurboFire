#include "game/action.h"

namespace poker {

std::string betSizeToString(BetSize size) {
    switch (size) {
        case BetSize::PERCENT_20: return "20%";
        case BetSize::PERCENT_33: return "33%";
        case BetSize::PERCENT_52: return "52%";
        case BetSize::PERCENT_100: return "100%";
        case BetSize::PERCENT_123: return "123%";
        case BetSize::RAISE_2_5X: return "2.5x";
        case BetSize::ALL_IN: return "All-in";
        default: return "Unknown";
    }
}

float calculateBetAmount(float pot_size, BetSize size) {
    switch (size) {
        case BetSize::PERCENT_20: return pot_size * 0.20f;
        case BetSize::PERCENT_33: return pot_size * 0.33f;
        case BetSize::PERCENT_52: return pot_size * 0.52f;
        case BetSize::PERCENT_100: return pot_size * 1.00f;
        case BetSize::PERCENT_123: return pot_size * 1.23f;
        default: return 0.0f;
    }
}

float calculateRaiseAmount(float current_bet, float pot_size) {
    // 2.5x the current bet
    return current_bet * 2.5f;
}

} // namespace poker
