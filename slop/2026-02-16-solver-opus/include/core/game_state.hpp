#pragma once

#include "action.hpp"
#include "card.hpp"
#include <vector>
#include <cstdint>

enum class Street {
    PREFLOP,
    FLOP,
    TURN,
    RIVER,
    SHOWDOWN
};

class GameState {
public:
    // Initialize SB vs BB heads-up spot
    GameState(int32_t sb_size = 0, int32_t bb_size = 1, int32_t stack_size = 100);
    
    // Get legal actions for current player
    std::vector<Action> get_legal_actions() const;
    
    // Apply an action and advance state
    void apply_action(const Action& action);
    
    // Check if current street is complete
    bool is_street_complete() const;
    
    // Advance to next street (deals cards if postflop)
    void advance_street();
    
    // Getters
    Street get_current_street() const { return current_street_; }
    int32_t get_pot() const { return pot_; }
    int32_t get_stack(int player) const { return stacks_[player]; }
    int32_t get_effective_stack() const { return effective_stack_; }
    int32_t get_bet_to_call() const { return bet_to_call_; }
    int32_t get_current_player() const { return current_player_; }
    const std::vector<Card>& get_board() const { return board_; }
    int32_t get_actions_this_street() const { return actions_this_street_; }
    
    // Check if player is still in hand
    bool is_player_active(int player) const { return !folded_[player]; }
    
    // Get last bet size (for minimum raise calculation)
    int32_t get_last_bet_size() const { return last_bet_size_; }
    
    // Deal community cards (for postflop)
    void deal_flop(const Card& c1, const Card& c2, const Card& c3);
    void deal_turn(const Card& card);
    void deal_river(const Card& card);
    
    // Initialize at flop (for flop solving)
    void initialize_at_flop(const Card& c1, const Card& c2, const Card& c3);
    
    // Reset for new hand
    void reset();
    
private:
    Street current_street_;
    int32_t actions_this_street_;      // Tracks raise cap
    int32_t last_bet_size_;            // For minimum raise calculation
    int32_t bet_to_call_;              // Current bet facing players
    bool player_acted_[2];             // Track if each player acted this round
    bool folded_[2];                   // Track if player folded
    int32_t pot_;
    int32_t stacks_[2];
    int32_t effective_stack_;
    int32_t current_player_;           // 0 = SB, 1 = BB
    int32_t sb_size_;
    int32_t bb_size_;
    
    // Community cards (empty until flop)
    std::vector<Card> board_;
    
    // Helper methods
    void update_effective_stack();
    bool can_raise() const;
    int32_t get_minimum_raise() const;
};
