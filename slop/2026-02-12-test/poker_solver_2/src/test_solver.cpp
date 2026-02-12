#include <iostream>
#include <chrono>
#include "card/card.h"
#include "game/board.h"
#include "game/action.h"
#include "solver/solver.h"
#include <vector>

using namespace poker;

int main() {
    std::cout << "Testing GTO Solver..." << std::endl;
    
    // Create a board
    Board board;
    board.setFlop(Card::fromString("As"), Card::fromString("Kh"), Card::fromString("7d"));
    
    std::cout << "Board: " << board.toString() << std::endl;
    
    // Create solver with fewer iterations for testing
    Solver::Config config;
    config.iterations = 1000; // Just 1k for quick test
    config.save_solved = false;
    
    Solver solver(config);
    
    std::cout << "Starting solve..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    
    solver.solve(board, std::vector<Action>{});
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    std::cout << "Solve complete in " << duration << " seconds" << std::endl;
    std::cout << "Progress: " << solver.getProgress() * 100 << "%" << std::endl;
    
    // Get strategies
    std::cout << "\nGetting strategies..." << std::endl;
    auto strategies = solver.getAllStrategies();
    std::cout << "Retrieved " << strategies.size() << " strategies" << std::endl;
    
    if (!strategies.empty()) {
        // Print first few strategies
        std::cout << "\nFirst 5 strategies:" << std::endl;
        for (int i = 0; i < std::min(5, (int)strategies.size()); ++i) {
            auto& [hand_idx, strat] = strategies[i];
            std::cout << "Hand " << hand_idx << ": ";
            for (size_t j = 0; j < strat.probabilities.size(); ++j) {
                std::cout << strat.probabilities[j] << " ";
            }
            std::cout << std::endl;
        }
    }
    
    return 0;
}
