#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include "core/game_state.h"
#include "components/grid.h"
#include "components/action_panel.h"
#include "solver/solver.h"

namespace poker {
namespace ui {

// Main poker solver application
class PokerApp {
public:
    PokerApp();
    ~PokerApp();
    
    // Run the application
    void run();
    
    // Stop the application
    void stop();
    
private:
    // Core components
    GameStateManager game_state_;
    HandGrid grid_;
    ActionPanel action_panel_;
    
    // TUI
    ftxui::ScreenInteractive screen_;
    ftxui::Component main_component_;
    
    // UI state
    std::string board_input_;
    std::string status_message_ = "Enter flop (e.g., AsKh7d)";
    bool solving_ = false;
    bool should_quit_ = false;
    float solve_progress_ = 0.0f;
    
    // Detail panel
    std::string detail_text_ = "Select a hand to see GTO strategy";
    
    // Build UI
    void buildUI();
    
    // Create components
    ftxui::Component createHeader();
    ftxui::Component createBoardInput();
    ftxui::Component createBoardDisplay();
    ftxui::Component createInfoPanel();
    ftxui::Component createDetailPanel();
    ftxui::Component createStatusBar();
    ftxui::Component createProgressBar();
    
    // Event handlers
    void onBoardInputConfirm();
    void onAction(ActionType action);
    void onGridSelect(const GridCell& cell);
    void onUndo();
    void onSolve();
    void onQuit();
    
    // Game state callbacks
    void onGameStateChange(GameState state);
    void onActionApplied(const Action& action);
    
    // Solver callbacks
    void onSolverProgress(float progress);
    void onSolverComplete();
    
    // Update display
    void updateGrid();
    void updateStatus();
};

} // namespace ui
} // namespace poker
