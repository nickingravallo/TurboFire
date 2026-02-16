#include "ui/interactive_range_display.hpp"
#include "solver/cfr_engine.hpp"
#include <ftxui/component/event.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>

InteractiveRangeDisplay::InteractiveRangeDisplay(GameState initial_state)
    : game_state_(initial_state)
    , selected_hand_(Hand(12, 12, true))
    , hand_selected_(true)
    , selected_row_(0)
    , selected_col_(0)
    , solving_in_progress_(false)
    , status_message_("")
    , refresh_counter_(0)
    , cfr_engine_(nullptr)
    , rng_(std::chrono::steady_clock::now().time_since_epoch().count())
{
    initialize_hand_matrix();
    
    for (const auto& card : game_state_.get_board()) {
        used_cards_.insert(card.to_bit());
    }
    
    refresh_available_actions();
    solve_all_hands();
    
    status_message_ = get_player_name(game_state_.get_current_player()) + 
                      " to act  |  Press 1-9 to choose action";
    refresh_counter_++;
}

InteractiveRangeDisplay::~InteractiveRangeDisplay() = default;

// --- Initialization ---

void InteractiveRangeDisplay::initialize_hand_matrix() {
    hand_matrix_.resize(13);
    for (int row = 0; row < 13; row++) {
        hand_matrix_[row].resize(13);
        for (int col = 0; col < 13; col++) {
            if (row == col) {
                hand_matrix_[row][col] = Hand(12 - row, 12 - row, true);
            } else if (row < col) {
                hand_matrix_[row][col] = Hand(12 - row, 12 - col, true);
            } else {
                hand_matrix_[row][col] = Hand(12 - col, 12 - row, false);
            }
        }
    }
}

Hand InteractiveRangeDisplay::get_hand_at(int row, int col) const {
    if (row >= 0 && row < 13 && col >= 0 && col < 13) {
        return hand_matrix_[row][col];
    }
    return Hand(12, 12, true);
}

std::string InteractiveRangeDisplay::get_player_name(int player) const {
    return player == 0 ? "SB" : "BB";
}

// --- Actions ---

void InteractiveRangeDisplay::refresh_available_actions() {
    available_actions_ = game_state_.get_legal_actions();
    action_labels_.clear();
    
    for (const auto& action : available_actions_) {
        action_labels_.push_back(format_action(action));
    }
}

std::string InteractiveRangeDisplay::format_action(const Action& action) const {
    switch (action.type) {
        case ActionType::FOLD:  return "Fold";
        case ActionType::CHECK: return "Check";
        case ActionType::CALL:  return "Call " + std::to_string(action.amount);
        case ActionType::BET: {
            int pct = (game_state_.get_pot() > 0) 
                      ? (action.amount * 100) / game_state_.get_pot() 
                      : 0;
            return "Bet " + std::to_string(action.amount) + " (" + std::to_string(pct) + "%)";
        }
        case ActionType::RAISE: {
            return "Raise to " + std::to_string(action.amount);
        }
        case ActionType::ALL_IN:
            return "All-in " + std::to_string(action.amount);
    }
    return "???";
}

void InteractiveRangeDisplay::select_action(int index) {
    if (index < 0 || index >= static_cast<int>(available_actions_.size())) {
        return;
    }
    
    const Action& chosen = available_actions_[index];
    Street street_before = game_state_.get_current_street();
    
    // Record in action history
    action_history_ += format_action(chosen) + "_";
    
    // Apply the action to the game state
    game_state_.apply_action(chosen);
    
    Street street_after = game_state_.get_current_street();
    
    // Did a fold end the hand?
    if (chosen.type == ActionType::FOLD) {
        // The player who DIDN'T fold wins
        // After fold+apply_action, current_player switches, so current_player is the winner
        status_message_ = get_player_name(1 - game_state_.get_current_player()) + 
                         " folds. " + get_player_name(game_state_.get_current_player()) + 
                         " wins pot of " + std::to_string(game_state_.get_pot());
        available_actions_.clear();
        action_labels_.clear();
        strategy_cache_.clear();
        refresh_counter_++;
        return;
    }
    
    // Did the street advance?
    if (street_after != street_before) {
        // Street changed - deal the next card
        deal_next_street_card();
        
        // Clear strategy cache and action history for new street
        strategy_cache_.clear();
        action_history_.clear();
        
        // Solve for new street
        solve_all_hands();
        refresh_available_actions();
        
        std::string street_name;
        switch (street_after) {
            case Street::TURN: street_name = "Turn"; break;
            case Street::RIVER: street_name = "River"; break;
            case Street::SHOWDOWN: street_name = "Showdown"; break;
            default: street_name = "???"; break;
        }
        
        if (street_after == Street::SHOWDOWN) {
            status_message_ = "Showdown!  Pot: " + std::to_string(game_state_.get_pot());
            available_actions_.clear();
            action_labels_.clear();
        } else {
            status_message_ = street_name + " dealt  |  " + 
                             get_player_name(game_state_.get_current_player()) + 
                             " to act  |  Press 1-9 to choose action";
        }
    } else {
        // Same street, other player's turn
        strategy_cache_.clear();
        solve_all_hands();
        refresh_available_actions();
        
        status_message_ = get_player_name(game_state_.get_current_player()) + 
                         " to act  |  Press 1-9 to choose action";
    }
    
    refresh_counter_++;
}

// --- Dealing ---

Card InteractiveRangeDisplay::deal_random_card() {
    std::uniform_int_distribution<int> dist(0, 51);
    uint8_t bit;
    do {
        bit = static_cast<uint8_t>(dist(rng_));
    } while (used_cards_.count(bit));
    
    used_cards_.insert(bit);
    return Card::from_bit(bit);
}

void InteractiveRangeDisplay::deal_next_street_card() {
    Street current = game_state_.get_current_street();
    
    if (current == Street::TURN && game_state_.get_board().size() == 3) {
        Card turn_card = deal_random_card();
        game_state_.deal_turn(turn_card);
    } else if (current == Street::RIVER && game_state_.get_board().size() == 4) {
        Card river_card = deal_random_card();
        game_state_.deal_river(river_card);
    }
}

// --- Solving ---

std::string InteractiveRangeDisplay::make_cache_key(const Hand& hand) const {
    return hand.to_string() + "_" + 
           std::to_string(static_cast<int>(game_state_.get_current_street())) + "_" +
           std::to_string(game_state_.get_current_player()) + "_" +
           action_history_;
}

void InteractiveRangeDisplay::solve_all_hands() {
    for (int row = 0; row < 13; row++) {
        for (int col = 0; col < 13; col++) {
            solve_for_hand(get_hand_at(row, col));
        }
    }
}

Strategy InteractiveRangeDisplay::solve_for_hand(const Hand& hand) {
    std::string cache_key = make_cache_key(hand);
    
    auto it = strategy_cache_.find(cache_key);
    if (it != strategy_cache_.end()) {
        return it->second;
    }
    
    // TODO: Replace with real CFR+ solver
    // Placeholder heuristic based on hand strength, street, player, and facing bet
    Strategy strategy;
    
    Street street = game_state_.get_current_street();
    int player = game_state_.get_current_player();
    bool facing_bet = (game_state_.get_bet_to_call() > 0);
    
    bool is_pair = (hand.rank1 == hand.rank2);
    (void)(hand.rank1 >= 8 && hand.rank2 >= 8); // is_broadway, used in strength calc
    bool is_ace = (hand.rank1 == 12);
    bool is_suited = hand.suited;
    bool is_connector = (hand.rank1 - hand.rank2 <= 2 && hand.rank1 != hand.rank2);
    int strength = hand.rank1 + hand.rank2;
    if (is_pair) strength += 10;
    if (is_suited) strength += 3;
    if (is_ace) strength += 5;
    
    if (facing_bet) {
        // Facing a bet: fold/call/raise decision
        if (strength >= 25) {
            // Very strong: raise
            strategy.raise_freq = 0.60;
            strategy.call_freq = 0.30;
            strategy.fold_freq = 0.10;
        } else if (strength >= 18) {
            // Strong: mostly call
            strategy.call_freq = 0.55;
            strategy.raise_freq = 0.15;
            strategy.fold_freq = 0.30;
        } else if (strength >= 12) {
            // Medium: mix call/fold
            strategy.call_freq = 0.35;
            strategy.fold_freq = 0.55;
            strategy.raise_freq = 0.10;
        } else {
            // Weak: mostly fold, some bluff raises
            strategy.fold_freq = 0.75;
            strategy.call_freq = 0.15;
            strategy.raise_freq = (is_suited && hand.rank2 <= 3) ? 0.10 : 0.00;
            if (strategy.raise_freq == 0.0) {
                strategy.fold_freq = 0.80;
                strategy.call_freq = 0.20;
            }
        }
    } else {
        // No bet: check/bet decision
        if (street == Street::FLOP) {
            if (strength >= 22) {
                strategy.bet_freq = 0.65;
                strategy.check_freq = 0.35;
            } else if (strength >= 16) {
                strategy.bet_freq = 0.35;
                strategy.check_freq = 0.65;
            } else if (is_suited && is_connector) {
                strategy.bet_freq = 0.25;
                strategy.check_freq = 0.75;
            } else {
                strategy.check_freq = 0.70;
                strategy.bet_freq = 0.15;
                strategy.fold_freq = 0.15;
            }
        } else if (street == Street::TURN) {
            if (strength >= 22) {
                strategy.bet_freq = 0.70;
                strategy.check_freq = 0.30;
            } else if (strength >= 16) {
                strategy.bet_freq = 0.25;
                strategy.check_freq = 0.75;
            } else {
                strategy.check_freq = 0.80;
                strategy.bet_freq = 0.10;
                strategy.fold_freq = 0.10;
            }
        } else if (street == Street::RIVER) {
            if (strength >= 24) {
                strategy.bet_freq = 0.55;
                strategy.allin_freq = 0.15;
                strategy.check_freq = 0.30;
            } else if (strength >= 18) {
                strategy.check_freq = 0.60;
                strategy.bet_freq = 0.25;
                strategy.fold_freq = 0.15;
            } else if (is_suited && hand.rank2 <= 3) {
                // Bluff candidates
                strategy.bet_freq = 0.30;
                strategy.check_freq = 0.10;
                strategy.fold_freq = 0.60;
            } else {
                strategy.check_freq = 0.65;
                strategy.fold_freq = 0.25;
                strategy.bet_freq = 0.10;
            }
        }
    }
    
    // BB defends wider than SB
    if (player == 1 && facing_bet) {
        strategy.call_freq += 0.05;
        strategy.fold_freq -= 0.05;
        if (strategy.fold_freq < 0) strategy.fold_freq = 0;
    }
    
    strategy_cache_[cache_key] = strategy;
    return strategy;
}

// --- Navigation ---

void InteractiveRangeDisplay::on_hand_selected(int row, int col) {
    selected_row_ = row;
    selected_col_ = col;
    selected_hand_ = get_hand_at(row, col);
    hand_selected_ = true;
    refresh_counter_++;
}

// --- Rendering ---

ftxui::Color InteractiveRangeDisplay::get_cell_color(const Strategy& strategy) {
    std::string dominant = strategy.get_dominant_action();
    
    if (dominant == "fold")  return ftxui::Color::RGB(41, 121, 255);
    if (dominant == "call")  return ftxui::Color::RGB(0, 200, 83);
    if (dominant == "bet")   return ftxui::Color::RGB(255, 23, 68);
    if (dominant == "allin") return ftxui::Color::RGB(213, 0, 0);
    
    return ftxui::Color::White;
}

std::string InteractiveRangeDisplay::get_cell_text(int row, int col) const {
    return get_hand_at(row, col).to_string();
}

ftxui::Element InteractiveRangeDisplay::render_hand_grid() {
    using namespace ftxui;
    
    (void)refresh_counter_;
    
    const char ranks[] = "AKQJT98765432";
    
    Elements header_row = {text("   ")};
    for (int col = 0; col < 13; col++) {
        header_row.push_back(text(" " + std::string(1, ranks[col]) + " ") | center);
    }
    
    Elements rows;
    rows.push_back(hbox(header_row));
    
    for (int row = 0; row < 13; row++) {
        Elements row_elements;
        row_elements.push_back(text(" " + std::string(1, ranks[row]) + " ") | center);
        
        for (int col = 0; col < 13; col++) {
            Hand hand = get_hand_at(row, col);
            std::string cache_key = make_cache_key(hand);
            
            auto it = strategy_cache_.find(cache_key);
            Element cell;
            
            if (it != strategy_cache_.end()) {
                ftxui::Color bg_color = get_cell_color(it->second);
                cell = text(get_cell_text(row, col)) | center | bgcolor(bg_color) | color(ftxui::Color::White);
            } else {
                cell = text(get_cell_text(row, col)) | center | bgcolor(ftxui::Color::GrayDark);
            }
            
            if (row == selected_row_ && col == selected_col_) {
                cell = cell | bold | inverted;
            }
            
            row_elements.push_back(cell);
        }
        
        rows.push_back(hbox(row_elements));
    }
    
    return vbox(rows);
}

ftxui::Element InteractiveRangeDisplay::render_info_panel() {
    using namespace ftxui;
    
    Elements elements;
    
    if (hand_selected_) {
        elements.push_back(text("Selected: " + selected_hand_.to_string()) | bold);
        
        std::string cache_key = make_cache_key(selected_hand_);
        auto it = strategy_cache_.find(cache_key);
        if (it != strategy_cache_.end()) {
            const Strategy& s = it->second;
            
            auto make_bar = [](const std::string& label, double freq, ftxui::Color c) -> Element {
                int bar_width = static_cast<int>(freq * 30);
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << (freq * 100) << "%";
                if (freq < 0.01) return text("");
                return hbox({
                    text(label) | size(WIDTH, EQUAL, 8),
                    text(std::string(bar_width, ' ')) | bgcolor(c),
                    text(" " + oss.str())
                });
            };
            
            if (s.fold_freq > 0.01)
                elements.push_back(make_bar("Fold", s.fold_freq, Color::RGB(41, 121, 255)));
            if (s.check_freq > 0.01)
                elements.push_back(make_bar("Check", s.check_freq, Color::RGB(0, 200, 83)));
            if (s.call_freq > 0.01)
                elements.push_back(make_bar("Call", s.call_freq, Color::RGB(0, 200, 83)));
            if (s.bet_freq > 0.01)
                elements.push_back(make_bar("Bet", s.bet_freq, Color::RGB(255, 23, 68)));
            if (s.raise_freq > 0.01)
                elements.push_back(make_bar("Raise", s.raise_freq, Color::RGB(255, 23, 68)));
            if (s.allin_freq > 0.01)
                elements.push_back(make_bar("All-in", s.allin_freq, Color::RGB(213, 0, 0)));
        }
    }
    
    if (elements.empty()) {
        elements.push_back(text("Navigate with arrow keys to view hand strategy") | dim);
    }
    
    return vbox(elements) | border;
}

ftxui::Element InteractiveRangeDisplay::render_board() {
    using namespace ftxui;
    
    Elements row;
    
    // Street label
    std::string street_label;
    switch (game_state_.get_current_street()) {
        case Street::PREFLOP: street_label = "PREFLOP"; break;
        case Street::FLOP:    street_label = "FLOP"; break;
        case Street::TURN:    street_label = "TURN"; break;
        case Street::RIVER:   street_label = "RIVER"; break;
        case Street::SHOWDOWN: street_label = "SHOWDOWN"; break;
    }
    
    row.push_back(text(" " + street_label + " ") | bold | bgcolor(Color::RGB(30, 30, 30)) | color(Color::White));
    row.push_back(text("  "));
    
    // Board cards with suit colors
    const auto& board = game_state_.get_board();
    for (size_t i = 0; i < board.size(); i++) {
        const Card& card = board[i];
        ftxui::Color suit_color;
        switch (card.suit) {
            case 0: suit_color = Color::White; break;
            case 1: suit_color = Color::Red; break;
            case 2: suit_color = Color::RGB(0, 150, 255); break;
            case 3: suit_color = Color::Green; break;
            default: suit_color = Color::White; break;
        }
        
        row.push_back(text(" " + card.to_string() + " ") | bold | color(suit_color) | bgcolor(Color::RGB(40, 40, 40)));
        
        if (i == 2 && board.size() > 3)
            row.push_back(text(" | ") | dim);
        else if (i == 3 && board.size() > 4)
            row.push_back(text(" | ") | dim);
        else
            row.push_back(text(" "));
    }
    
    for (size_t i = board.size(); i < 5; i++) {
        row.push_back(text(" ?? ") | dim | bgcolor(Color::RGB(40, 40, 40)));
        if (i == 2) row.push_back(text(" | ") | dim);
        else row.push_back(text(" "));
    }
    
    row.push_back(text("  "));
    row.push_back(text("Pot: " + std::to_string(game_state_.get_pot())) | bold);
    row.push_back(text("  "));
    row.push_back(text("Eff: " + std::to_string(game_state_.get_effective_stack())) | dim);
    
    return hbox(row) | border;
}

ftxui::Element InteractiveRangeDisplay::render_action_bar() {
    using namespace ftxui;
    
    Elements elements;
    
    // Player indicator
    int player = game_state_.get_current_player();
    std::string player_name = get_player_name(player);
    
    ftxui::Color player_color = (player == 0) 
        ? Color::RGB(255, 165, 0)   // Orange for SB
        : Color::RGB(100, 200, 255); // Light blue for BB
    
    elements.push_back(
        hbox({
            text(" " + player_name + " to act ") | bold | bgcolor(player_color) | color(Color::Black),
            text("  "),
            text(status_message_) | color(Color::Yellow)
        })
    );
    elements.push_back(separator());
    
    if (!available_actions_.empty()) {
        Elements action_row;
        for (size_t i = 0; i < available_actions_.size() && i < 9; i++) {
            ftxui::Color action_color;
            switch (available_actions_[i].type) {
                case ActionType::FOLD:   action_color = Color::RGB(41, 121, 255); break;
                case ActionType::CHECK:  action_color = Color::RGB(0, 200, 83); break;
                case ActionType::CALL:   action_color = Color::RGB(0, 200, 83); break;
                case ActionType::BET:    action_color = Color::RGB(255, 23, 68); break;
                case ActionType::RAISE:  action_color = Color::RGB(255, 23, 68); break;
                case ActionType::ALL_IN: action_color = Color::RGB(213, 0, 0); break;
            }
            
            action_row.push_back(
                text(" [" + std::to_string(i + 1) + "] " + action_labels_[i] + " ") 
                    | bgcolor(action_color) | color(Color::White) | bold
            );
            action_row.push_back(text("  "));
        }
        elements.push_back(hbox(action_row));
    } else {
        elements.push_back(text("No actions available - hand complete") | dim);
    }
    
    elements.push_back(separator());
    elements.push_back(text("Arrow keys: navigate grid  |  1-9: choose action  |  Q: quit") | dim);
    
    return vbox(elements) | border;
}

void InteractiveRangeDisplay::run() {
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    
    auto renderer = ftxui::Renderer([this] {
        (void)refresh_counter_;
        return ftxui::vbox({
            render_board(),
            render_hand_grid(),
            render_info_panel(),
            render_action_bar()
        });
    });
    
    auto component = ftxui::CatchEvent(renderer, [this, &screen](ftxui::Event event) {
        if (event == ftxui::Event::Character('q') || event == ftxui::Event::Character('Q')) {
            screen.Exit();
            return true;
        }
        
        // Arrow keys: navigate grid
        if (event == ftxui::Event::ArrowUp && selected_row_ > 0) {
            on_hand_selected(selected_row_ - 1, selected_col_);
            return true;
        }
        if (event == ftxui::Event::ArrowDown && selected_row_ < 12) {
            on_hand_selected(selected_row_ + 1, selected_col_);
            return true;
        }
        if (event == ftxui::Event::ArrowLeft && selected_col_ > 0) {
            on_hand_selected(selected_row_, selected_col_ - 1);
            return true;
        }
        if (event == ftxui::Event::ArrowRight && selected_col_ < 12) {
            on_hand_selected(selected_row_, selected_col_ + 1);
            return true;
        }
        
        // Number keys 1-9: choose action
        const char keys[] = "123456789";
        for (int i = 0; i < 9; i++) {
            if (event == ftxui::Event::Character(keys[i])) {
                select_action(i);
                return true;
            }
        }
        
        return false;
    });
    
    screen.Loop(component);
}
