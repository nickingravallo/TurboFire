#include "app/poker_app.h"
#include <thread>
#include <sstream>
#include <iomanip>

namespace poker {
namespace ui {

PokerApp::PokerApp()
    : screen_(ftxui::ScreenInteractive::Fullscreen()) {
    buildUI();
    
    // Set up callbacks
    game_state_.onStateChange([this](GameState state) {
        onGameStateChange(state);
    });
    
    game_state_.onActionApplied([this](const Action& action) {
        onActionApplied(action);
    });
    
    action_panel_.setGameState(&game_state_);
    action_panel_.onAction([this](ActionType action) {
        onAction(action);
    });
    
    grid_.onSelect([this](const GridCell& cell) {
        onGridSelect(cell);
    });
}

PokerApp::~PokerApp() {
    stop();
}

void PokerApp::run() {
    screen_.Loop(main_component_);
}

void PokerApp::stop() {
    should_quit_ = true;
}

void PokerApp::buildUI() {
    // Create all components
    auto header = createHeader();
    auto board_input = createBoardInput();
    auto board_display = createBoardDisplay();
    auto grid = grid_.getComponent();
    auto action_panel = action_panel_.getComponent();
    auto detail_panel = createDetailPanel();
    auto status = createStatusBar();
    
    // Layout
    auto top_section = ftxui::Container::Vertical({
        header,
        board_input,
        board_display
    });
    
    auto middle_section = ftxui::Container::Horizontal({
        grid | ftxui::flex,
        ftxui::Container::Vertical({
            detail_panel | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 15),
            action_panel
        }) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 35)
    });
    
    auto layout = ftxui::Container::Vertical({
        top_section | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 6),
        middle_section | ftxui::flex,
        status | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1)
    });
    
    // Quit handler
    main_component_ = layout | ftxui::CatchEvent([this](ftxui::Event event) {
        if (event == ftxui::Event::Character('q')) {
            onQuit();
            return true;
        }
        if (event == ftxui::Event::Character('u')) {
            onUndo();
            return true;
        }
        if (event == ftxui::Event::Character('s')) {
            onSolve();
            return true;
        }
        return false;
    });
}

ftxui::Component PokerApp::createHeader() {
    return ftxui::Renderer([] {
        return ftxui::hbox({
            ftxui::text(" GTO Poker Solver ") | ftxui::bold | ftxui::center | ftxui::flex
        }) | ftxui::bgcolor(ftxui::Color::RGB(60, 60, 60));
    });
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
    
    return ftxui::Container::Horizontal({
        ftxui::Renderer([] { return ftxui::text("Board: "); }),
        input
    }) | ftxui::border;
}

ftxui::Component PokerApp::createBoardDisplay() {
    return ftxui::Renderer([this] {
        std::string board_str = game_state_.getBoard().toString();
        if (board_str.empty()) {
            board_str = "No board";
        }
        
        auto state_str = [this]() -> std::string {
            switch (game_state_.getState()) {
                case GameState::PREFLOP_SB_ACTS: return "Preflop - SB to act";
                case GameState::PREFLOP_BB_RESPONDS: return "Preflop - BB responds";
                case GameState::FLOP_SB_ACTS: return "Flop - SB to act";
                case GameState::FLOP_BB_RESPONDS: return "Flop - BB responds";
                case GameState::TURN_SB_ACTS: return "Turn - SB to act";
                case GameState::TURN_BB_RESPONDS: return "Turn - BB responds";
                case GameState::RIVER_SB_ACTS: return "River - SB to act";
                case GameState::RIVER_BB_RESPONDS: return "River - BB responds";
                case GameState::SHOWDOWN: return "Showdown";
                case GameState::SOLVING: return "Solving...";
                case GameState::SOLVED: return "Solved";
                default: return "Unknown";
            }
        }();
        
        return ftxui::hbox({
            ftxui::text(" Board: ") | ftxui::bold,
            ftxui::text(board_str) | ftxui::color(ftxui::Color::Yellow),
            ftxui::text(" | "),
            ftxui::text(state_str) | ftxui::color(ftxui::Color::Cyan)
        }) | ftxui::border;
    });
}

ftxui::Component PokerApp::createDetailPanel() {
    return ftxui::Renderer([this] {
        ftxui::Elements lines;
        lines.push_back(ftxui::text("Hand Details") | ftxui::bold);
        lines.push_back(ftxui::separator());
        
        // Split detail_text by newlines
        std::stringstream ss(detail_text_);
        std::string line;
        while (std::getline(ss, line)) {
            lines.push_back(ftxui::text(line));
        }
        
        return ftxui::vbox(lines) | ftxui::border;
    });
}

ftxui::Component PokerApp::createStatusBar() {
    return ftxui::Renderer([this] {
        if (solving_) {
            int bar_width = 30;
            int filled = static_cast<int>(solve_progress_ * bar_width);
            std::string bar = "[" + std::string(filled, '=') + std::string(bar_width - filled, ' ') + "]";
            int percent = static_cast<int>(solve_progress_ * 100);
            
            return ftxui::hbox({
                ftxui::text(" Solving: ") | ftxui::bold,
                ftxui::text(bar),
                ftxui::text(" " + std::to_string(percent) + "%")
            }) | ftxui::bgcolor(ftxui::Color::RGB(45, 45, 45));
        }
        
        return ftxui::hbox({
            ftxui::text(" Pot: " + std::to_string(static_cast<int>(game_state_.getPotSize())) + "bb "),
            ftxui::text(" SB: " + std::to_string(static_cast<int>(game_state_.getSBStack())) + "bb "),
            ftxui::text(" BB: " + std::to_string(static_cast<int>(game_state_.getBBStack())) + "bb "),
            ftxui::text(" " + status_message_ + " ") | ftxui::flex
        }) | ftxui::bgcolor(ftxui::Color::RGB(45, 45, 45));
    });
}

void PokerApp::onBoardInputConfirm() {
    try {
        auto state = game_state_.getState();
        if (state == GameState::PREFLOP_SB_ACTS || state == GameState::PREFLOP_BB_RESPONDS) {
            game_state_.setFlop(board_input_);
            status_message_ = "Flop set: " + board_input_;
        } else if (state == GameState::FLOP_BB_RESPONDS) {
            // Need to parse single card for turn
            if (board_input_.length() >= 2) {
                game_state_.setTurn(board_input_.substr(0, 2));
                status_message_ = "Turn set";
            }
        } else if (state == GameState::TURN_BB_RESPONDS) {
            if (board_input_.length() >= 2) {
                game_state_.setRiver(board_input_.substr(0, 2));
                status_message_ = "River set";
            }
        }
        board_input_.clear();
        action_panel_.refresh();
    } catch (...) {
        status_message_ = "Invalid board format!";
    }
}

void PokerApp::onAction(ActionType action) {
    if (game_state_.applyAction(Action(action, 0.0f))) {
        action_panel_.refresh();
    }
}

void PokerApp::onGridSelect(const GridCell& cell) {
    std::stringstream ss;
    ss << cell.notation << "\n";
    ss << "Fold: " << static_cast<int>(cell.strategy.fold * 100) << "%\n";
    ss << "Check: " << static_cast<int>(cell.strategy.check * 100) << "%\n";
    ss << "Call: " << static_cast<int>(cell.strategy.call * 100) << "%\n";
    ss << "Bet 20%: " << static_cast<int>(cell.strategy.bet_20 * 100) << "%\n";
    ss << "Bet 33%: " << static_cast<int>(cell.strategy.bet_33 * 100) << "%\n";
    ss << "Bet 52%: " << static_cast<int>(cell.strategy.bet_52 * 100) << "%\n";
    ss << "Bet 100%: " << static_cast<int>(cell.strategy.bet_100 * 100) << "%\n";
    ss << "Bet 123%: " << static_cast<int>(cell.strategy.bet_123 * 100) << "%\n";
    ss << "Raise: " << static_cast<int>(cell.strategy.raise * 100) << "%\n";
    
    auto dominant = cell.strategy.getDominantAction();
    ss << "Dominant: " << dominant.first;
    
    detail_text_ = ss.str();
}

void PokerApp::onUndo() {
    if (game_state_.undoLastAction()) {
        status_message_ = "Undone";
        action_panel_.refresh();
    }
}

void PokerApp::onSolve() {
    if (solving_) return;
    
    solving_ = true;
    game_state_.startSolving();
    status_message_ = "Building game tree...";
    
    // Run solver in background with 10k iterations
    std::thread solver_thread([this]() {
        // Configure solver for 10,000 iterations as requested
        SolverConfig config;
        config.iterations = 10000;
        config.save_solved = false;  // Don't save to disk for now
        
        Solver solver(config);
        
        // Get current board and history from game state
        Board board = game_state_.getBoard();
        std::vector<Action> history = game_state_.getHistory();
        
        // Start solving
        solver.solve(board, history);
        
        // Monitor progress
        while (solver.getProgress() < 1.0f && !should_quit_) {
            solve_progress_ = solver.getProgress();
            status_message_ = "Solving... " + std::to_string(static_cast<int>(solve_progress_ * 100)) + "%";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Update every 100ms
        }
        
        if (!should_quit_) {
            solve_progress_ = 1.0f;
            
            // Get all strategies and update grid
            auto strategies = solver.getAllStrategies();
            std::vector<CellStrategy> cell_strategies(169);
            
            for (const auto& [hand_idx, strat] : strategies) {
                if (hand_idx < 169) {
                    CellStrategy cs;
                    // Map solver strategy (8 actions) to CellStrategy
                    // Strategy order: FOLD, CHECK, CALL, BET_20, BET_33, BET_52, BET_100, RAISE
                    if (strat.probabilities.size() >= 8) {
                        cs.fold = strat.probabilities[0];
                        cs.check = strat.probabilities[1];
                        cs.call = strat.probabilities[2];
                        cs.bet_20 = strat.probabilities[3];
                        cs.bet_33 = strat.probabilities[4];
                        cs.bet_52 = strat.probabilities[5];
                        cs.bet_100 = strat.probabilities[6];
                        cs.raise = strat.probabilities[7];
                    }
                    cell_strategies[hand_idx] = cs;
                }
            }
            
            // Update grid on main thread via screen post event
            screen_.Post([this, cell_strategies]() mutable {
                grid_.updateAllStrategies(cell_strategies);
                game_state_.finishSolving();
                onSolverComplete();
                solving_ = false;
                solve_progress_ = 0.0f;
            });
        } else {
            solver.stop();
            solving_ = false;
            solve_progress_ = 0.0f;
        }
    });
    
    solver_thread.detach();
}

void PokerApp::onQuit() {
    stop();
    screen_.Exit();
}

void PokerApp::onGameStateChange(GameState state) {
    updateStatus();
}

void PokerApp::onActionApplied(const Action& action) {
    status_message_ = action.toString();
}

void PokerApp::onSolverComplete() {
    status_message_ = "Solving complete! Grid updated with GTO strategies.";
}

void PokerApp::updateStatus() {
    // Status is updated in render
}

} // namespace ui
} // namespace poker
