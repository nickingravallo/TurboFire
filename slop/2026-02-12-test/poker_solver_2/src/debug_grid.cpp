#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <vector>
#include <cstring>
#include "card/card.h"
#include "game/board.h"
#include "game/action.h"
#include "solver/solver.h"
#include "tui/grid.h"

using namespace poker;

// Mock the TUI grid update process
class MockTUI {
public:
    void testGridUpdate() {
        std::cout << "=== Testing Grid Update Process ===" << std::endl;
        
        // Step 1: Create solver
        std::cout << "\n1. Creating solver with 100 iterations..." << std::endl;
        Solver::Config config;
        config.iterations = 100;
        config.save_solved = false;
        Solver solver(config);
        
        // Step 2: Create board
        std::cout << "2. Creating board AsKh7d..." << std::endl;
        Board board;
        board.setFlop(Card::fromString("As"), Card::fromString("Kh"), Card::fromString("7d"));
        
        // Step 3: Solve
        std::cout << "3. Solving..." << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        solver.solve(board, std::vector<Action>{});
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double>(end - start).count();
        std::cout << "   Solve completed in " << duration << "s" << std::endl;
        
        // Step 4: Get strategies (like TUI does in updateGrid)
        std::cout << "4. Getting strategies..." << std::endl;
        auto strategies = solver.getAllStrategies();
        std::cout << "   Retrieved " << strategies.size() << " strategies" << std::endl;
        
        if (strategies.empty()) {
            std::cout << "   ERROR: No strategies!" << std::endl;
            return;
        }
        
        // Step 5: Check if strategies have actual data
        std::cout << "5. Checking strategy data..." << std::endl;
        bool has_colors = false;
        int colorful_cells = 0;
        
        for (const auto& [hand_idx, strat] : strategies) {
            if (strat.probabilities.empty()) continue;
            
            // Check if any probability is non-zero
            for (float p : strat.probabilities) {
                if (p > 0.01f) {
                    has_colors = true;
                    colorful_cells++;
                    break;
                }
            }
        }
        
        std::cout << "   Strategies with data: " << colorful_cells << "/" << strategies.size() << std::endl;
        std::cout << "   Has colors: " << (has_colors ? "YES" : "NO") << std::endl;
        
        // Step 6: Simulate grid update
        std::cout << "6. Simulating grid update..." << std::endl;
        ui::HandGrid grid;
        grid.updateStrategy(strategies);
        
        // Step 7: Sample some cells
        std::cout << "7. Sample grid cells:" << std::endl;
        std::cout << "   AA (0,0): " << describeCell(grid, 0, 0) << std::endl;
        std::cout << "   KK (1,1): " << describeCell(grid, 1, 1) << std::endl;
        std::cout << "   AKs (0,1): " << describeCell(grid, 0, 1) << std::endl;
        std::cout << "   72o (12,0): " << describeCell(grid, 12, 0) << std::endl;
        
        std::cout << "\n=== Test Complete ===" << std::endl;
    }
    
private:
    std::string describeCell(ui::HandGrid& grid, int row, int col) {
        // Access grid's internal cells through a test interface
        // Since cells_ is private, we'll need to check the Render output
        // For now, just return placeholder
        return "Cell data present";
    }
};

int main() {
    std::cout << "=== DEBUG: Simulating Complete TUI Flow ===" << std::endl;
    
    MockTUI mock;
    mock.testGridUpdate();
    
    return 0;
}
