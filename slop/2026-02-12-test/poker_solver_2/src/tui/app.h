#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include "game/board.h"
#include "game/game_tree.h"
#include "solver/solver.h"
#include "tui/grid.h"

namespace poker {
namespace ui {

// Main application class
class PokerApp {
public:
    PokerApp();
    ~PokerApp();
    
    // Run the application
    void run();
    
    // Stop the application
    void stop();
    
private:
    // Game state
    Board board_;
    GameTree game_tree_;
    Solver solver_;
    std::vector<Action> history_;
    
    // TUI components
    ftxui::ScreenInteractive screen_;
    HandGrid hand_grid_;
    ftxui::Component main_component_;
    
    // UI state
    std::string board_input_;
    std::string status_message_;
    bool solving_ = false;
    bool should_quit_ = false;
    
    // Current game state display
    float pot_size_ = 1.5f;
    float sb_stack_ = 99.0f;
    float bb_stack_ = 99.0f;
    std::string current_street_ = "PREFLOP";
    std::string position_to_act_ = "SB (OOP)";
    
    // Build UI
    void buildUI();
    
    // Create components
    ftxui::Component createHeader();
    ftxui::Component createBoardInput();
    ftxui::Component createBoardDisplay();
    ftxui::Component createActionButtons();
    ftxui::Component createHistoryPanel();
    ftxui::Component createStatusBar();
    ftxui::Component createHandDetailsPanel();
    
    // Event handlers
    void onBoardInputConfirm();
    void onAction(ActionType type, BetSize size = BetSize::PERCENT_33);
    void onUndo();
    void onSolve();
    void onQuit();
    void onHandSelected(Hand hand);
    
    // Update display
    void updateDisplay();
    void updateGrid();
    
    // Progress callback for solver
    void onSolverProgress(float progress);
};

} // namespace ui
} // namespace poker
