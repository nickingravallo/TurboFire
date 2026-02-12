#include "tui/app.h"
#include "tui/colors.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/loop.hpp>
#include <thread>
#include <atomic>
#include <chrono>

namespace poker {
namespace ui {

PokerApp::PokerApp()
    : screen_(ftxui::ScreenInteractive::Fullscreen())
    , solver_(Solver::Config{1000, true, "./solves/"}) {
    buildUI();
}

PokerApp::~PokerApp() {
    stop();
}

void PokerApp::run() {
    // Set up periodic refresh to update grid colors after solving
    std::atomic<bool> refresh_requested{false};
    
    auto component = main_component_ | ftxui::CatchEvent([&](ftxui::Event event) {
        if (event == ftxui::Event::Custom) {
            refresh_requested = true;
            return true;
        }
        return false;
    });
    
    // Start refresh thread
    std::thread refresh_thread([&]() {
        while (!should_quit_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (refresh_requested) {
                refresh_requested = false;
                screen_.Post(ftxui::Event::Custom);
            }
        }
    });
    
    screen_.Loop(component);
    
    refresh_thread.join();
}

void PokerApp::stop() {
    should_quit_ = true;
    solver_.stop();
}

void PokerApp::buildUI() {
    // Create main layout
    auto header = createHeader();
    auto board_input = createBoardInput();
    auto board_display = createBoardDisplay();
    auto action_buttons = createActionButtons();
    auto history = createHistoryPanel();
    auto status = createStatusBar();
    auto hand_details = createHandDetailsPanel();
    
    // Grid with selection callback
    hand_grid_.onSelect([this](Hand hand) {
        onHandSelected(hand);
    });
    auto grid = hand_grid_.getComponent();
    
    // Main layout
    ftxui::Components vertical_children;
    vertical_children.push_back(header);
    vertical_children.push_back(board_input);
    vertical_children.push_back(board_display);
    
    ftxui::Components horizontal_children;
    horizontal_children.push_back(grid | ftxui::flex);
    
    ftxui::Components right_panel_children;
    right_panel_children.push_back(hand_details | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 10));
    right_panel_children.push_back(history | ftxui::flex);
    
    horizontal_children.push_back(
        ftxui::Container::Vertical(right_panel_children) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 40)
    );
    
    vertical_children.push_back(ftxui::Container::Horizontal(horizontal_children) | ftxui::flex);
    vertical_children.push_back(action_buttons);
    vertical_children.push_back(status);
    
    auto layout = ftxui::Container::Vertical(vertical_children);
    
    // Add quit handler
    main_component_ = layout | ftxui::CatchEvent([this](ftxui::Event event) {
        if (event == ftxui::Event::Character('q') || 
            event == ftxui::Event::Character('Q')) {
            onQuit();
            return true;
        }
        return false;
    });
}

ftxui::Component PokerApp::createHeader() {
    return ftxui::Renderer(std::function<ftxui::Element()>([this] {
        return ftxui::hbox({
            ftxui::text(" GTO Poker Solver ") | ftxui::bold | ftxui::center | ftxui::flex,
        }) | ftxui::bgcolor(COLOR_BG_LIGHT) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1);
    }));
}

ftxui::Component PokerApp::createBoardInput() {
    auto input = ftxui::Input(&board_input_, "Enter flop (e.g., AsKh7d):");
    
    input |= ftxui::CatchEvent([this](ftxui::Event event) {
        if (event == ftxui::Event::Return) {
            onBoardInputConfirm();
            return true;
        }
        return false;
    });
    
    ftxui::Components children;
    children.push_back(ftxui::Renderer(std::function<ftxui::Element()>([] { return ftxui::text("Board: "); })));
    children.push_back(input);
    
    return ftxui::Container::Horizontal(children) | ftxui::border;
}

ftxui::Component PokerApp::createBoardDisplay() {
    return ftxui::Renderer(std::function<ftxui::Element()>([this] {
        std::string board_str = board_.toString();
        if (board_str.empty()) {
            board_str = "No board set";
        }
        
        return ftxui::hbox({
            ftxui::text(" Current Board: ") | ftxui::bold,
            ftxui::text(board_str) | ftxui::color(ftxui::Color::Yellow),
        }) | ftxui::border;
    }));
}

ftxui::Component PokerApp::createActionButtons() {
    auto fold_btn = ftxui::Button("[F]old", [this] { onAction(ActionType::FOLD); });
    auto check_btn = ftxui::Button("[C]heck", [this] { onAction(ActionType::CHECK); });
    auto call_btn = ftxui::Button("Ca[L]l", [this] { onAction(ActionType::CALL); });
    auto bet20_btn = ftxui::Button("Bet 20%", [this] { onAction(ActionType::BET, BetSize::PERCENT_20); });
    auto bet33_btn = ftxui::Button("Bet 33%", [this] { onAction(ActionType::BET, BetSize::PERCENT_33); });
    auto bet52_btn = ftxui::Button("Bet 52%", [this] { onAction(ActionType::BET, BetSize::PERCENT_52); });
    auto bet100_btn = ftxui::Button("Bet 100%", [this] { onAction(ActionType::BET, BetSize::PERCENT_100); });
    auto bet123_btn = ftxui::Button("Bet 123%", [this] { onAction(ActionType::BET, BetSize::PERCENT_123); });
    auto raise_btn = ftxui::Button("[R]aise 2.5x", [this] { onAction(ActionType::RAISE); });
    auto allin_btn = ftxui::Button("[A]ll-in", [this] { onAction(ActionType::ALL_IN); });
    auto undo_btn = ftxui::Button("[U]ndo", [this] { onUndo(); });
    auto solve_btn = ftxui::Button("[S]olve", [this] { onSolve(); });
    
    ftxui::Components children;
    children.push_back(fold_btn);
    children.push_back(check_btn);
    children.push_back(call_btn);
    children.push_back(bet20_btn);
    children.push_back(bet33_btn);
    children.push_back(bet52_btn);
    children.push_back(bet100_btn);
    children.push_back(bet123_btn);
    children.push_back(raise_btn);
    children.push_back(allin_btn);
    children.push_back(undo_btn);
    children.push_back(solve_btn);
    
    return ftxui::Container::Horizontal(children) | ftxui::border;
}

ftxui::Component PokerApp::createHistoryPanel() {
    return ftxui::Renderer(std::function<ftxui::Element()>([this] {
        ftxui::Elements lines;
        lines.push_back(ftxui::text("Action History:") | ftxui::bold);
        lines.push_back(ftxui::separator());
        
        for (const auto& action : history_) {
            lines.push_back(ftxui::text("  " + action.toString()));
        }
        
        return ftxui::vbox(lines) | ftxui::border;
    }));
}

ftxui::Component PokerApp::createStatusBar() {
    return ftxui::Renderer(std::function<ftxui::Element()>([this] {
        return ftxui::hbox({
            ftxui::text(" Pot: " + std::to_string(static_cast<int>(pot_size_)) + "bb ") | ftxui::bgcolor(COLOR_BG_MID),
            ftxui::text(" SB: " + std::to_string(static_cast<int>(sb_stack_)) + "bb ") | ftxui::bgcolor(COLOR_BG_MID),
            ftxui::text(" BB: " + std::to_string(static_cast<int>(bb_stack_)) + "bb ") | ftxui::bgcolor(COLOR_BG_MID),
            ftxui::text(" Street: " + current_street_ + " ") | ftxui::bgcolor(COLOR_BG_MID),
            ftxui::text(" To Act: " + position_to_act_ + " ") | ftxui::bgcolor(COLOR_BG_MID),
            ftxui::text(" " + status_message_ + " ") | ftxui::flex | ftxui::bgcolor(COLOR_BG_MID)
        });
    }));
}

ftxui::Component PokerApp::createHandDetailsPanel() {
    return ftxui::Renderer(std::function<ftxui::Element()>([this] {
        Hand selected = hand_grid_.getSelectedHand();
        
        return ftxui::vbox({
            ftxui::text("Selected Hand: " + selected.toString()) | ftxui::bold,
            ftxui::text(selected.toDisplayString()),
            ftxui::separator(),
            ftxui::text("Use arrow keys to navigate"),
            ftxui::text("Enter to view details"),
            ftxui::text("Q to quit")
        }) | ftxui::border;
    }));
}

void PokerApp::onBoardInputConfirm() {
    try {
        board_ = Board::fromString(board_input_);
        updateDisplay();
        status_message_ = "Board set to: " + board_.toString();
    } catch (...) {
        status_message_ = "Invalid board format!";
    }
}

void PokerApp::onAction(ActionType type, BetSize size) {
    Action action(type, size, 0.0f);
    
    // Calculate bet amount
    if (type == ActionType::BET) {
        action.amount = calculateBetAmount(pot_size_, size);
    } else if (type == ActionType::RAISE) {
        action.amount = calculateRaiseAmount(0.5f, pot_size_);
    }
    
    history_.push_back(action);
    game_tree_.applyAction(action);
    
    // Update game state
    if (type == ActionType::BET || type == ActionType::RAISE || type == ActionType::ALL_IN) {
        pot_size_ += action.amount;
        // Alternate position
        if (position_to_act_ == "SB (OOP)") {
            sb_stack_ -= action.amount;
            position_to_act_ = "BB (IP)";
        } else {
            bb_stack_ -= action.amount;
            position_to_act_ = "SB (OOP)";
        }
    } else if (type == ActionType::CALL) {
        pot_size_ += action.amount;
    }
    
    updateDisplay();
    status_message_ = "Action: " + action.toString();
}

void PokerApp::onUndo() {
    if (!history_.empty()) {
        history_.pop_back();
        game_tree_.undoLastAction();
        status_message_ = "Undone last action";
        updateDisplay();
    }
}

void PokerApp::onSolve() {
    if (solving_) return;
    
    solving_ = true;
    status_message_ = "Solving...";
    updateDisplay();
    
    // Run solver in background
    std::thread solver_thread([this]() {
        solver_.solve(board_, history_);
        solving_ = false;
        status_message_ = "Solving complete!";
        updateGrid();
        
        // Force UI redraw by posting a custom event
        screen_.Post(ftxui::Event::Custom);
    });
    
    solver_thread.detach();
}

void PokerApp::onQuit() {
    stop();
    screen_.Exit();
}

void PokerApp::onHandSelected(Hand hand) {
    status_message_ = "Selected: " + hand.toString();
}

void PokerApp::updateDisplay() {
    // Update street display
    switch (board_.street()) {
        case 0: current_street_ = "PREFLOP"; break;
        case 1: current_street_ = "FLOP"; break;
        case 2: current_street_ = "TURN"; break;
        case 3: current_street_ = "RIVER"; break;
        default: current_street_ = "SHOWDOWN"; break;
    }
}

void PokerApp::updateGrid() {
    auto strategies = solver_.getAllStrategies();
    hand_grid_.updateStrategy(strategies);
}

void PokerApp::onSolverProgress(float progress) {
    status_message_ = "Solving: " + std::to_string(static_cast<int>(progress * 100)) + "%";
}

} // namespace ui
} // namespace poker
