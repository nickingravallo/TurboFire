#include "core/game_state.hpp"
#include "core/bet_sizing.hpp"
#include <algorithm>
#include <vector>

GameState::GameState(int32_t sb_size, int32_t bb_size, int32_t stack_size)
    : current_street_(Street::PREFLOP)
    , actions_this_street_(0)
    , last_bet_size_(0)
    , bet_to_call_(bb_size - sb_size)  // BB posts more than SB
    , pot_(sb_size + bb_size)
    , current_player_(0)  // SB acts first preflop
    , sb_size_(sb_size)
    , bb_size_(bb_size)
{
    stacks_[0] = stack_size - sb_size;  // SB
    stacks_[1] = stack_size - bb_size;  // BB
    player_acted_[0] = false;
    player_acted_[1] = false;
    folded_[0] = false;
    folded_[1] = false;
    update_effective_stack();
}

void GameState::update_effective_stack() {
    effective_stack_ = std::min(stacks_[0], stacks_[1]);
}

std::vector<Action> GameState::get_legal_actions() const {
    std::vector<Action> actions;
    
    // If player folded, no actions
    if (folded_[current_player_]) {
        return actions;
    }
    
    // When bet_to_call == 0 (no bet pending)
    if (bet_to_call_ == 0) {
        // CHECK always available
        actions.push_back(Action(ActionType::CHECK));
        
        // BET with valid sizes
        std::vector<int32_t> bet_sizes = BetSizing::get_bet_sizes(pot_, effective_stack_);
        for (int32_t size : bet_sizes) {
            if (size <= stacks_[current_player_]) {
                actions.push_back(Action(ActionType::BET, size));
            }
        }
        
        // ALL_IN always available
        if (stacks_[current_player_] > 0) {
            actions.push_back(Action(ActionType::ALL_IN, stacks_[current_player_]));
        }
    }
    // When bet_to_call > 0 (facing a bet)
    else {
        // FOLD always available
        actions.push_back(Action(ActionType::FOLD));
        
        // CALL
        if (stacks_[current_player_] >= bet_to_call_) {
            actions.push_back(Action(ActionType::CALL, bet_to_call_));
        }
        
        // RAISE (if under raise cap and meets minimum)
        if (can_raise()) {
            int32_t min_raise = get_minimum_raise();
            std::vector<int32_t> raise_sizes = BetSizing::get_raise_sizes(
                pot_, bet_to_call_, last_bet_size_, effective_stack_);
            
            for (int32_t raise_amount : raise_sizes) {
                if (raise_amount >= min_raise && raise_amount <= stacks_[current_player_]) {
                    actions.push_back(Action(ActionType::RAISE, raise_amount));
                }
            }
        }
        
        // ALL_IN always available
        if (stacks_[current_player_] > 0) {
            actions.push_back(Action(ActionType::ALL_IN, stacks_[current_player_]));
        }
    }
    
    return actions;
}

bool GameState::can_raise() const {
    // Raise cap: maximum 3 actions per street
    return actions_this_street_ < 3;
}

int32_t GameState::get_minimum_raise() const {
    if (last_bet_size_ == 0) {
        // First bet has no minimum
        return bet_to_call_ + 1;
    }
    // Minimum raise is 2x the last bet size
    return bet_to_call_ + (2 * last_bet_size_);
}

void GameState::apply_action(const Action& action) {
    int32_t player = current_player_;
    
    switch (action.type) {
        case ActionType::FOLD:
            folded_[player] = true;
            player_acted_[player] = true;
            break;
            
        case ActionType::CHECK:
            // No chips moved, just mark as acted
            player_acted_[player] = true;
            break;
            
        case ActionType::CALL:
            {
                int32_t call_amount = std::min(action.amount, stacks_[player]);
                stacks_[player] -= call_amount;
                pot_ += call_amount;
                bet_to_call_ -= call_amount;
                player_acted_[player] = true;
            }
            break;
            
        case ActionType::BET:
            {
                int32_t bet_amount = std::min(action.amount, stacks_[player]);
                stacks_[player] -= bet_amount;
                pot_ += bet_amount;
                bet_to_call_ = bet_amount;
                last_bet_size_ = bet_amount;
                actions_this_street_++;
                player_acted_[player] = true;
            }
            break;
            
        case ActionType::RAISE:
            {
                int32_t raise_amount = std::min(action.amount, stacks_[player]);
                int32_t total_to_call = bet_to_call_ + (raise_amount - bet_to_call_);
                stacks_[player] -= raise_amount;
                pot_ += raise_amount;
                bet_to_call_ = raise_amount - bet_to_call_;
                last_bet_size_ = raise_amount - bet_to_call_;
                actions_this_street_++;
                player_acted_[player] = true;
            }
            break;
            
        case ActionType::ALL_IN:
            {
                int32_t all_in_amount = stacks_[player];
                stacks_[player] = 0;
                pot_ += all_in_amount;
                
                if (bet_to_call_ == 0) {
                    // All-in bet
                    bet_to_call_ = all_in_amount;
                    last_bet_size_ = all_in_amount;
                    actions_this_street_++;
                } else {
                    // All-in raise or call
                    if (all_in_amount > bet_to_call_) {
                        // All-in raise
                        last_bet_size_ = all_in_amount - bet_to_call_;
                        bet_to_call_ = all_in_amount - bet_to_call_;
                        actions_this_street_++;
                    } else {
                        // All-in call
                        bet_to_call_ -= all_in_amount;
                    }
                }
                player_acted_[player] = true;
            }
            break;
    }
    
    update_effective_stack();
    
    // Check if street should close
    if (is_street_complete()) {
        advance_street();
    } else {
        // Switch to other player
        current_player_ = 1 - current_player_;
    }
}

bool GameState::is_street_complete() const {
    // Street is complete when:
    // 1. All active players have acted
    // 2. No pending bets (bet_to_call == 0)
    // 3. At least one player hasn't folded
    
    bool all_acted = true;
    for (int i = 0; i < 2; i++) {
        if (!folded_[i] && !player_acted_[i]) {
            all_acted = false;
            break;
        }
    }
    
    return all_acted && bet_to_call_ == 0 && (!folded_[0] || !folded_[1]);
}

void GameState::advance_street() {
    // Reset action tracking
    actions_this_street_ = 0;
    bet_to_call_ = 0;
    last_bet_size_ = 0;
    player_acted_[0] = false;
    player_acted_[1] = false;
    
    // Advance street
    switch (current_street_) {
        case Street::PREFLOP:
            current_street_ = Street::FLOP;
            // SB acts first postflop
            current_player_ = 0;
            break;
        case Street::FLOP:
            current_street_ = Street::TURN;
            current_player_ = 0;
            break;
        case Street::TURN:
            current_street_ = Street::RIVER;
            current_player_ = 0;
            break;
        case Street::RIVER:
            current_street_ = Street::SHOWDOWN;
            break;
        case Street::SHOWDOWN:
            // Already at showdown
            break;
    }
}

void GameState::deal_flop(const Card& c1, const Card& c2, const Card& c3) {
    board_.clear();
    board_.push_back(c1);
    board_.push_back(c2);
    board_.push_back(c3);
}

void GameState::deal_turn(const Card& card) {
    if (board_.size() == 3) {
        board_.push_back(card);
    }
}

void GameState::deal_river(const Card& card) {
    if (board_.size() == 4) {
        board_.push_back(card);
    }
}

void GameState::initialize_at_flop(const Card& c1, const Card& c2, const Card& c3) {
    // Set street to FLOP
    current_street_ = Street::FLOP;
    
    // Reset action tracking for flop
    actions_this_street_ = 0;
    bet_to_call_ = 0;
    last_bet_size_ = 0;
    player_acted_[0] = false;
    player_acted_[1] = false;
    
    // Deal flop
    deal_flop(c1, c2, c3);
    
    // SB acts first postflop
    current_player_ = 0;
}

void GameState::reset() {
    current_street_ = Street::PREFLOP;
    actions_this_street_ = 0;
    last_bet_size_ = 0;
    bet_to_call_ = bb_size_ - sb_size_;
    pot_ = sb_size_ + bb_size_;
    current_player_ = 0;
    stacks_[0] = stacks_[0] + pot_ - (sb_size_ + bb_size_);  // Restore stack
    stacks_[1] = stacks_[1] + pot_ - (sb_size_ + bb_size_);
    pot_ = sb_size_ + bb_size_;
    stacks_[0] -= sb_size_;
    stacks_[1] -= bb_size_;
    player_acted_[0] = false;
    player_acted_[1] = false;
    folded_[0] = false;
    folded_[1] = false;
    board_.clear();
    update_effective_stack();
}
