#pragma once

#include <ftxui/component/component.hpp>
#include <functional>
#include "../core/game_state.h"

namespace poker {
namespace ui {

// Action panel with dynamic button states
class ActionPanel {
public:
    ActionPanel();
    
    // Update game state reference
    void setGameState(GameStateManager* game_state);
    
    // Get the ftxui component
    ftxui::Component getComponent();
    
    // Set callback for action selection
    void onAction(std::function<void(ActionType)> callback);
    
    // Force refresh (call when game state changes)
    void refresh();
    
private:
    GameStateManager* game_state_ = nullptr;
    std::function<void(ActionType)> on_action_;
    
    // Button components
    ftxui::Component fold_btn_;
    ftxui::Component check_btn_;
    ftxui::Component call_btn_;
    ftxui::Component bet_20_btn_;
    ftxui::Component bet_33_btn_;
    ftxui::Component bet_52_btn_;
    ftxui::Component bet_100_btn_;
    ftxui::Component bet_123_btn_;
    ftxui::Component raise_btn_;
    ftxui::Component allin_btn_;
    
    // Create buttons
    void createButtons();
    
    // Update button states based on game state
    void updateButtonStates();
    
    // Render the panel
    ftxui::Element RenderPanel();
    
    // Get label for call button
    std::string getCallLabel() const;
};

} // namespace ui
} // namespace poker
