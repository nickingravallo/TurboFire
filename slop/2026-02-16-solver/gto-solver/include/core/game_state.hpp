#pragma once

#include <cstdint>
#include <optional>
#include <array>
#include <vector>
#include <stdexcept>

namespace gto {

enum class Street : uint8_t {
    Preflop = 0,
    Flop = 1,
    Turn = 2,
    River = 3,
    Showdown = 4,
    Terminal = 5
};

enum class ActionType : uint8_t {
    Fold = 0,
    Check = 1,
    Call = 2,
    Bet = 3,
    Raise = 4,
    AllIn = 5
};

struct Action {
    ActionType type;
    std::optional<float> amount;  // Only for Bet/Raise/AllIn
    
    [[nodiscard]] constexpr bool operator==(const Action& other) const noexcept {
        return type == other.type && amount == other.amount;
    }
};

struct GameState {
    // Betting state
    float pot = 1.5f;  // SB + BB to start
    float effective_stack = 100.0f;  // 100bb
    float current_bet_to_call = 1.0f;  // BB has bet 1bb
    float last_raise_size = 1.0f;  // Track for min-raise
    
    // Street tracking
    Street street = Street::Preflop;
    uint8_t street_action_count = 0;  // Actions this street
    bool is_oop_turn = true;  // BB acts first preflop, SB (BTN) acts last
    
    // Terminal state
    bool is_terminal = false;
    float terminal_value = 0.0f;  // For SB (positive = SB wins)
    
    // Board cards (0-51, -1 = not dealt)
    std::array<int8_t, 5> board = {-1, -1, -1, -1, -1};
    uint8_t board_cards_dealt = 0;
    
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return pot >= 0.0f && effective_stack >= 0.0f && current_bet_to_call >= 0.0f;
    }
};

[[nodiscard]] constexpr std::array<Action, 6> get_legal_actions(const GameState& state) noexcept {
    std::array<Action, 6> actions{};
    size_t count = 0;
    
    if (state.is_terminal) {
        return actions;  // Empty if terminal
    }
    
    // Rule 1: If current_bet_to_call == 0, Fold and Call MUST NOT be returned
    if (state.current_bet_to_call == 0.0f) {
        // Can only Check (no bet to call)
        actions[count++] = Action{ActionType::Check, std::nullopt};
    } else {
        // Have a bet to respond to
        actions[count++] = Action{ActionType::Fold, std::nullopt};
        actions[count++] = Action{ActionType::Call, std::nullopt};
    }
    
    // Rule 2: Check if we can bet/raise
    if (state.effective_stack > 0.0f) {
        // Can always go all-in
        actions[count++] = Action{ActionType::AllIn, state.effective_stack};
        
        // Rule 3: If street_action_count >= 3, Raise MUST NOT be returned
        if (state.street_action_count < 3) {
            // Calculate bet sizes based on pot
            float pot_size = state.pot + state.current_bet_to_call;
            
            if (state.current_bet_to_call == 0.0f) {
                // No bet to call - we can Bet
                // Bet sizes: 33%, 50%, 75%, 125% of pot
                float bet_33 = pot_size * 0.33f;
                float bet_50 = pot_size * 0.50f;
                float bet_75 = pot_size * 0.75f;
                float bet_125 = pot_size * 1.25f;
                
                if (bet_33 <= state.effective_stack) {
                    actions[count++] = Action{ActionType::Bet, bet_33};
                }
                if (bet_50 <= state.effective_stack) {
                    actions[count++] = Action{ActionType::Bet, bet_50};
                }
                if (bet_75 <= state.effective_stack) {
                    actions[count++] = Action{ActionType::Bet, bet_75};
                }
                if (bet_125 <= state.effective_stack) {
                    actions[count++] = Action{ActionType::Bet, bet_125};
                }
            } else {
                // Have a bet to call - we can Raise
                float min_raise = state.current_bet_to_call + state.last_raise_size;
                float pot_size_after_call = state.pot + state.current_bet_to_call * 2.0f;
                
                // Raise sizes based on pot
                float raise_33 = pot_size_after_call * 0.33f;
                float raise_50 = pot_size_after_call * 0.50f;
                float raise_75 = pot_size_after_call * 0.75f;
                float raise_125 = pot_size_after_call * 1.25f;
                
                // Must be at least min-raise
                if (raise_33 >= min_raise && raise_33 <= state.effective_stack) {
                    actions[count++] = Action{ActionType::Raise, raise_33};
                }
                if (raise_50 >= min_raise && raise_50 <= state.effective_stack) {
                    actions[count++] = Action{ActionType::Raise, raise_50};
                }
                if (raise_75 >= min_raise && raise_75 <= state.effective_stack) {
                    actions[count++] = Action{ActionType::Raise, raise_75};
                }
                if (raise_125 >= min_raise && raise_125 <= state.effective_stack) {
                    actions[count++] = Action{ActionType::Raise, raise_125};
                }
            }
        }
    }
    
    return actions;
}

[[nodiscard]] constexpr GameState apply_action(GameState state, const Action& action) {
    if (state.is_terminal) {
        return state;
    }
    
    // Validate action is legal
    auto legal_actions = get_legal_actions(state);
    bool is_legal = false;
    for (const auto& legal : legal_actions) {
        if (legal.type == action.type) {
            if (action.type == ActionType::Bet || action.type == ActionType::Raise || 
                action.type == ActionType::AllIn) {
                // For betting actions, check amount matches one of the legal amounts
                if (legal.amount == action.amount) {
                    is_legal = true;
                    break;
                }
            } else {
                is_legal = true;
                break;
            }
        }
    }
    
    if (!is_legal) {
        return state;  // Invalid action - return unchanged
    }
    
    switch (action.type) {
        case ActionType::Fold:
            state.is_terminal = true;
            state.terminal_value = state.is_oop_turn ? -state.pot / 2.0f : state.pot / 2.0f;
            state.street = Street::Terminal;
            break;
            
        case ActionType::Check:
            // Check passes action to opponent
            state.is_oop_turn = !state.is_oop_turn;
            state.street_action_count++;
            
            // If both players checked (action back to original player), advance street
            if (state.street_action_count >= 2 && state.current_bet_to_call == 0.0f) {
                // Advance to next street
                switch (state.street) {
                    case Street::Preflop:
                        state.street = Street::Flop;
                        state.board_cards_dealt = 3;
                        state.board[0] = 0; state.board[1] = 1; state.board[2] = 2;  // Placeholder
                        break;
                    case Street::Flop:
                        state.street = Street::Turn;
                        state.board_cards_dealt = 4;
                        state.board[3] = 3;  // Placeholder
                        break;
                    case Street::Turn:
                        state.street = Street::River;
                        state.board_cards_dealt = 5;
                        state.board[4] = 4;  // Placeholder
                        break;
                    case Street::River:
                        state.street = Street::Showdown;
                        state.is_terminal = true;
                        break;
                    default:
                        break;
                }
                state.street_action_count = 0;
                state.is_oop_turn = false;  // SB acts last postflop
            }
            break;
            
        case ActionType::Call:
            // Call matches the current bet
            state.pot += state.current_bet_to_call;
            state.effective_stack -= state.current_bet_to_call;
            state.is_oop_turn = !state.is_oop_turn;
            state.street_action_count++;
            
            // Call closes the street
            if (state.street_action_count >= 2) {
                switch (state.street) {
                    case Street::Preflop:
                        state.street = Street::Flop;
                        state.board_cards_dealt = 3;
                        break;
                    case Street::Flop:
                        state.street = Street::Turn;
                        state.board_cards_dealt = 4;
                        break;
                    case Street::Turn:
                        state.street = Street::River;
                        state.board_cards_dealt = 5;
                        break;
                    case Street::River:
                        state.street = Street::Showdown;
                        state.is_terminal = true;
                        break;
                    default:
                        break;
                }
                state.current_bet_to_call = 0.0f;
                state.street_action_count = 0;
                state.is_oop_turn = false;  // SB acts last postflop
            }
            break;
            
        case ActionType::Bet:
        case ActionType::Raise:
        case ActionType::AllIn: {
            float bet_amount = action.amount.value_or(0.0f);
            float total_to_call = state.current_bet_to_call;
            
            // Update pot and stack
            state.pot += total_to_call + bet_amount;
            state.effective_stack -= total_to_call + bet_amount;
            
            // Track raise size for min-raise calculation
            if (state.current_bet_to_call > 0.0f) {
                state.last_raise_size = bet_amount;
            }
            
            state.current_bet_to_call = bet_amount;
            state.is_oop_turn = !state.is_oop_turn;
            state.street_action_count++;
            break;
        }
    }
    
    return state;
}

} // namespace gto
