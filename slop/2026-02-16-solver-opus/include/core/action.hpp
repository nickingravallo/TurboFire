#pragma once

#include <cstdint>

enum class ActionType {
    FOLD,
    CHECK,      // Only when bet_to_call == 0
    CALL,       // Only when bet_to_call > 0
    BET,        // Only when bet_to_call == 0
    RAISE,      // Only when bet_to_call > 0, must be >= 2x last bet
    ALL_IN      // Available anytime, special case of BET/RAISE
};

struct Action {
    ActionType type;
    int32_t amount;  // For BET/RAISE/ALL_IN, 0 for FOLD/CHECK/CALL
    
    Action() : type(ActionType::FOLD), amount(0) {}
    Action(ActionType t, int32_t amt = 0) : type(t), amount(amt) {}
    
    bool operator==(const Action& other) const {
        return type == other.type && amount == other.amount;
    }
};
