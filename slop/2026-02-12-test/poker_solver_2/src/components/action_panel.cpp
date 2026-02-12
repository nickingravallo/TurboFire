#include "components/action_panel.h"
#include <sstream>

namespace poker {
namespace ui {

ActionPanel::ActionPanel() {
    createButtons();
}

void ActionPanel::setGameState(GameStateManager* game_state) {
    game_state_ = game_state;
    refresh();
}

void ActionPanel::createButtons() {
    auto make_action = [this](ActionType type) {
        return [this, type] {
            if (on_action_) on_action_(type);
        };
    };
    
    fold_btn_ = ftxui::Button("[F]old", make_action(ActionType::FOLD));
    check_btn_ = ftxui::Button("[C]heck", make_action(ActionType::CHECK));
    call_btn_ = ftxui::Button("Call", make_action(ActionType::CALL));
    bet_20_btn_ = ftxui::Button("Bet 20%", make_action(ActionType::BET_20));
    bet_33_btn_ = ftxui::Button("Bet 33%", make_action(ActionType::BET_33));
    bet_52_btn_ = ftxui::Button("Bet 52%", make_action(ActionType::BET_52));
    bet_100_btn_ = ftxui::Button("Bet 100%", make_action(ActionType::BET_100));
    bet_123_btn_ = ftxui::Button("Bet 123%", make_action(ActionType::BET_123));
    raise_btn_ = ftxui::Button("[R]aise 2.5x", make_action(ActionType::RAISE));
    allin_btn_ = ftxui::Button("[A]ll-in", make_action(ActionType::ALL_IN));
}

ftxui::Component ActionPanel::getComponent() {
    return ftxui::Renderer([this] {
        return RenderPanel();
    });
}

void ActionPanel::onAction(std::function<void(ActionType)> callback) {
    on_action_ = callback;
}

void ActionPanel::refresh() {
    if (game_state_) {
        updateButtonStates();
    }
}

void ActionPanel::updateButtonStates() {
    if (!game_state_) return;
    
    auto valid_actions = game_state_->getValidActions();
    
    // Enable/disable buttons based on valid actions
    // Note: In ftxui, we'd need to rebuild buttons with disabled state
    // For now, we just track which are valid
}

ftxui::Element ActionPanel::RenderPanel() {
    if (!game_state_) {
        return ftxui::text("No game state") | ftxui::border;
    }
    
    ftxui::Elements lines;
    
    // Game info line
    std::stringstream info;
    info << "Pot: " << static_cast<int>(game_state_->getPotSize()) << "bb";
    info << " | To Call: " << static_cast<int>(game_state_->getAmountToCall()) << "bb";
    info << " | Turn: " << (game_state_->getPlayerToAct() == Position::SB ? "SB" : "BB");
    lines.push_back(ftxui::text(info.str()) | ftxui::bold);
    lines.push_back(ftxui::separator());
    
    // Get valid actions
    auto valid_actions = game_state_->getValidActions();
    
    // Row 1: Fold, Check/Call, All-in
    ftxui::Elements row1;
    
    bool can_fold = std::find(valid_actions.begin(), valid_actions.end(), ActionType::FOLD) != valid_actions.end();
    bool can_check = std::find(valid_actions.begin(), valid_actions.end(), ActionType::CHECK) != valid_actions.end();
    bool can_call = std::find(valid_actions.begin(), valid_actions.end(), ActionType::CALL) != valid_actions.end();
    bool can_allin = std::find(valid_actions.begin(), valid_actions.end(), ActionType::ALL_IN) != valid_actions.end();
    
    if (can_fold) {
        row1.push_back(fold_btn_->Render());
    } else {
        row1.push_back(ftxui::text("[FOLD]") | ftxui::dim);
    }
    
    if (can_check) {
        row1.push_back(check_btn_->Render());
    } else if (can_call) {
        row1.push_back(call_btn_->Render());
    } else {
        row1.push_back(ftxui::text("[CHECK/CALL]") | ftxui::dim);
    }
    
    if (can_allin) {
        row1.push_back(allin_btn_->Render());
    } else {
        row1.push_back(ftxui::text("[ALL-IN]") | ftxui::dim);
    }
    
    lines.push_back(ftxui::hbox(row1));
    lines.push_back(ftxui::separator());
    
    // Row 2: Bet sizes
    ftxui::Elements row2;
    bool can_bet = false;
    
    auto add_bet_btn = [&](ActionType type, ftxui::Component& btn, const char* label) {
        bool can = std::find(valid_actions.begin(), valid_actions.end(), type) != valid_actions.end();
        if (can) {
            row2.push_back(btn->Render());
            can_bet = true;
        } else {
            row2.push_back(ftxui::text(label) | ftxui::dim);
        }
    };
    
    add_bet_btn(ActionType::BET_20, bet_20_btn_, "[20%]");
    add_bet_btn(ActionType::BET_33, bet_33_btn_, "[33%]");
    add_bet_btn(ActionType::BET_52, bet_52_btn_, "[52%]");
    add_bet_btn(ActionType::BET_100, bet_100_btn_, "[100%]");
    add_bet_btn(ActionType::BET_123, bet_123_btn_, "[123%]");
    
    if (can_bet) {
        lines.push_back(ftxui::hbox(row2));
        lines.push_back(ftxui::separator());
    }
    
    // Row 3: Raise
    bool can_raise = std::find(valid_actions.begin(), valid_actions.end(), ActionType::RAISE) != valid_actions.end();
    if (can_raise) {
        lines.push_back(raise_btn_->Render());
    } else {
        lines.push_back(ftxui::text("[RAISE]") | ftxui::dim);
    }
    
    return ftxui::vbox(lines) | ftxui::border;
}

std::string ActionPanel::getCallLabel() const {
    if (!game_state_) return "Call";
    float amount = game_state_->getAmountToCall();
    if (amount == 0.0f) return "Check";
    return "Call " + std::to_string(static_cast<int>(amount)) + "bb";
}

} // namespace ui
} // namespace poker
