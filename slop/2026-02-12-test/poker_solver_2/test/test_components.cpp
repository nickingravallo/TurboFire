#include <iostream>
#include <thread>
#include <chrono>
#include "../src/core/game_state.h"
#include "../src/components/grid.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

using namespace poker;
using namespace poker::ui;

class MockSolver {
public:
    std::vector<CellStrategy> generateStrategies() {
        std::vector<CellStrategy> strategies(169);
        
        // Generate realistic-looking strategies
        for (int i = 0; i < 169; ++i) {
            CellStrategy& strat = strategies[i];
            
            if (i < 13) {
                // Pairs - aggressive
                strat.check = 0.15f;
                strat.bet_33 = 0.50f;
                strat.bet_100 = 0.25f;
                strat.call = 0.10f;
            } else if (i < 91) {
                // Suited - moderate
                int strength = 90 - i;  // Higher index = weaker hand
                strat.check = 0.40f;
                strat.fold = 0.10f + (strength / 100.0f);
                strat.bet_33 = 0.30f - (strength / 200.0f);
                strat.call = 0.20f;
            } else {
                // Offsuit - passive
                int strength = 168 - i;
                strat.check = 0.50f;
                strat.fold = 0.25f + (strength / 80.0f);
                strat.call = 0.15f;
                strat.bet_33 = 0.10f;
            }
        }
        
        return strategies;
    }
};

int main() {
    std::cout << "=== Poker TUI Component Test ===" << std::endl;
    
    // Test 1: Game State Manager
    std::cout << "\n1. Testing Game State Manager..." << std::endl;
    GameStateManager game;
    
    std::cout << "Initial state: " << static_cast<int>(game.getState()) << std::endl;
    std::cout << "Player to act: " << (game.getPlayerToAct() == Position::SB ? "SB" : "BB") << std::endl;
    std::cout << "Pot: " << game.getPotSize() << "bb" << std::endl;
    std::cout << "Valid actions: ";
    auto actions = game.getValidActions();
    for (auto a : actions) {
        std::cout << static_cast<int>(a) << " ";
    }
    std::cout << std::endl;
    
    // Test SB check
    std::cout << "\nSB checks..." << std::endl;
    game.applyAction(Action(ActionType::CHECK, 0.0f));
    std::cout << "New state: " << static_cast<int>(game.getState()) << std::endl;
    std::cout << "Player to act: " << (game.getPlayerToAct() == Position::SB ? "SB" : "BB") << std::endl;
    
    // Test BB check
    std::cout << "\nBB checks..." << std::endl;
    game.applyAction(Action(ActionType::CHECK, 0.0f));
    std::cout << "New state: " << static_cast<int>(game.getState()) << std::endl;
    
    // Test flop
    std::cout << "\nSetting flop AsKh7d..." << std::endl;
    game.setFlop("AsKh7d");
    std::cout << "Board: " << game.getBoard().toString() << std::endl;
    std::cout << "State: " << static_cast<int>(game.getState()) << std::endl;
    
    // Test 2: Grid Component
    std::cout << "\n2. Testing Grid Component..." << std::endl;
    HandGrid grid;
    
    // Generate mock strategies
    MockSolver solver;
    auto strategies = solver.generateStrategies();
    grid.updateAllStrategies(strategies);
    
    std::cout << "Grid initialized with " << strategies.size() << " strategies" << std::endl;
    
    // Create TUI
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    
    auto grid_component = grid.getComponent();
    
    // Info panel
    std::string info_text = "Select a hand to see details";
    auto info_panel = ftxui::Renderer([&] {
        return ftxui::vbox({
            ftxui::text("Hand Details") | ftxui::bold,
            ftxui::separator(),
            ftxui::text(info_text)
        }) | ftxui::border;
    });
    
    // Handle selection
    grid.onSelect([&](const GridCell& cell) {
        std::stringstream ss;
        ss << cell.notation << ":\n";
        ss << "  Fold: " << static_cast<int>(cell.strategy.fold * 100) << "%\n";
        ss << "  Check: " << static_cast<int>(cell.strategy.check * 100) << "%\n";
        ss << "  Call: " << static_cast<int>(cell.strategy.call * 100) << "%\n";
        ss << "  Bet 33%: " << static_cast<int>(cell.strategy.bet_33 * 100) << "%\n";
        auto dominant = cell.strategy.getDominantAction();
        ss << "  Dominant: " << dominant.first;
        info_text = ss.str();
    });
    
    // Layout
    auto layout = ftxui::Container::Horizontal({
        grid_component | ftxui::flex,
        info_panel | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 30)
    });
    
    // Add quit handler
    layout |= ftxui::CatchEvent([&](ftxui::Event event) {
        if (event == ftxui::Event::Character('q')) {
            screen.Exit();
            return true;
        }
        return false;
    });
    
    std::cout << "\nStarting TUI (press 'q' to quit)..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    screen.Loop(layout);
    
    std::cout << "\n=== Test Complete ===" << std::endl;
    return 0;
}
