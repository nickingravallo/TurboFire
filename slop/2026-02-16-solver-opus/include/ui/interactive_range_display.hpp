#pragma once

#include "core/game_state.hpp"
#include "core/hand.hpp"
#include "core/card.hpp"
#include "core/action.hpp"
#include "core/strategy.hpp"
#include "core/ranges.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <random>
#include <vector>

class CFREngine;

class InteractiveRangeDisplay {
public:
    InteractiveRangeDisplay(GameState initial_state);
    ~InteractiveRangeDisplay();
    
    void run();
    
private:
    // Game state
    GameState game_state_;
    
    // Selected hand for display
    Hand selected_hand_;
    bool hand_selected_;
    
    // Strategy cache: key = "hand_street_player_actionseq"
    std::unordered_map<std::string, Strategy> strategy_cache_;
    
    // UI state
    int selected_row_;
    int selected_col_;
    bool solving_in_progress_;
    std::string status_message_;
    int refresh_counter_;
    
    // Action state
    std::vector<Action> available_actions_;
    std::vector<std::string> action_labels_;
    std::string action_history_;  // e.g. "check_bet33_call"
    
    // CFR engine
    std::unique_ptr<CFREngine> cfr_engine_;
    
    // Deck tracking
    std::unordered_set<uint8_t> used_cards_;
    std::mt19937 rng_;
    
    // Hand grid data (13x13)
    std::vector<std::vector<Hand>> hand_matrix_;
    
    void initialize_hand_matrix();
    Hand get_hand_at(int row, int col) const;
    
    // Actions
    void refresh_available_actions();
    void select_action(int index);
    std::string format_action(const Action& action) const;
    
    // Dealing
    Card deal_random_card();
    void deal_next_street_card();
    
    // Solving
    void solve_all_hands();
    Strategy solve_for_hand(const Hand& hand);
    std::string make_cache_key(const Hand& hand) const;
    
    // Navigation
    void on_hand_selected(int row, int col);
    
    // Rendering
    ftxui::Element render_hand_grid();
    ftxui::Element render_info_panel();
    ftxui::Element render_board();
    ftxui::Element render_action_bar();
    ftxui::Color get_cell_color(const Strategy& strategy);
    std::string get_cell_text(int row, int col) const;
    
    // Player name
    std::string get_player_name(int player) const;
};
