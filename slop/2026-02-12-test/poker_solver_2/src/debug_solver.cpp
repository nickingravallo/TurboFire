#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include "card/card.h"
#include "game/board.h"
#include "game/action.h"
#include "solver/solver.h"

using namespace poker;

int main() {
    std::cout << "=== DEBUG: Simulating TUI Solver Behavior ===" << std::endl;
    
    // Step 1: Create board (like TUI does)
    std::cout << "\n1. Creating board..." << std::endl;
    Board board;
    board.setFlop(Card::fromString("As"), Card::fromString("Kh"), Card::fromString("7d"));
    std::cout << "   Board: " << board.toString() << std::endl;
    std::cout << "   Street: " << board.street() << std::endl;
    
    // Step 2: Create solver with same config as TUI
    std::cout << "\n2. Creating solver..." << std::endl;
    Solver::Config config;
    config.iterations = 100;  // Reduced for testing - was 100000
    config.save_solved = true;
    config.save_directory = "./solves/";
    
    Solver solver(config);
    std::cout << "   Iterations: " << config.iterations << std::endl;
    
    // Step 3: Solve (this is what happens when you click Solve)
    std::cout << "\n3. Starting solve..." << std::endl;
    std::vector<Action> history;  // Empty history like TUI start
    
    auto start = std::chrono::high_resolution_clock::now();
    solver.solve(board, history);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration<double>(end - start).count();
    std::cout << "   Solve took: " << duration << " seconds" << std::endl;
    std::cout << "   Progress: " << solver.getProgress() * 100 << "%" << std::endl;
    
    // Step 4: Get strategies (like updateGrid does)
    std::cout << "\n4. Getting strategies..." << std::endl;
    auto strategies = solver.getAllStrategies();
    std::cout << "   Retrieved " << strategies.size() << " strategies" << std::endl;
    
    // Step 5: Check if strategies have data
    if (!strategies.empty()) {
        std::cout << "\n5. Checking first strategy..." << std::endl;
        auto& [hand_idx, strat] = strategies[0];
        std::cout << "   Hand index: " << hand_idx << std::endl;
        std::cout << "   Strategy size: " << strat.probabilities.size() << std::endl;
        std::cout << "   Probabilities: ";
        for (auto p : strat.probabilities) {
            std::cout << p << " ";
        }
        std::cout << std::endl;
        
        // Step 6: Check if strategies are all the same (indicates problem)
        std::cout << "\n6. Checking if strategies vary..." << std::endl;
        bool all_same = true;
        for (size_t i = 1; i < std::min(strategies.size(), size_t(10)); ++i) {
            if (strategies[i].second.probabilities != strategies[0].second.probabilities) {
                all_same = false;
                break;
            }
        }
        std::cout << "   First 10 strategies identical: " << (all_same ? "YES (PROBLEM!)" : "NO (Good)") << std::endl;
        
        // Step 7: Sample different hands
        std::cout << "\n7. Sample strategies:" << std::endl;
        std::cout << "   AA (index 0): ";
        for (auto p : strategies[0].second.probabilities) std::cout << p << " ";
        std::cout << std::endl;
        
        if (strategies.size() > 78) {
            std::cout << "   AKs (index 78): ";
            for (auto p : strategies[78].second.probabilities) std::cout << p << " ";
            std::cout << std::endl;
        }
        
        if (strategies.size() > 390) {
            std::cout << "   72o (index 390): ";
            for (auto p : strategies[390].second.probabilities) std::cout << p << " ";
            std::cout << std::endl;
        }
    } else {
        std::cout << "   ERROR: No strategies returned!" << std::endl;
    }
    
    // Step 8: Check tree
    std::cout << "\n8. Testing second solve (cached)..." << std::endl;
    auto start2 = std::chrono::high_resolution_clock::now();
    solver.solve(board, history);
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration<double>(end2 - start2).count();
    std::cout << "   Second solve took: " << duration2 << " seconds" << std::endl;
    std::cout << "   (If fast, it loaded from cache)" << std::endl;
    
    std::cout << "\n=== Debug Complete ===" << std::endl;
    
    return 0;
}
