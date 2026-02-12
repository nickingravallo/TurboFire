#include <iostream>
#include <iomanip>
#include <algorithm>
#include "card/card.h"
#include "card/hand.h"
#include "game/board.h"
#include "game/action.h"
#include "solver/solver.h"

using namespace poker;

int main() {
    std::cout << "=== Testing Grid Color Generation ===" << std::endl;
    
    // Create solver
    Solver::Config config;
    config.iterations = 100;
    config.save_solved = false;
    Solver solver(config);
    
    // Create board and solve
    Board board;
    board.setFlop(Card::fromString("As"), Card::fromString("Kh"), Card::fromString("7d"));
    
    std::cout << "\nSolving..." << std::endl;
    solver.solve(board, std::vector<Action>{});
    
    // Get strategies
    auto strategies = solver.getAllStrategies();
    std::cout << "Got " << strategies.size() << " strategies\n" << std::endl;
    
    // Create a 13x13 grid and fill it
    struct Cell {
        float fold = 0, check = 0, call = 0, bet = 0, raise = 0;
        bool has_data = false;
    };
    Cell grid[13][13];
    
    // Fill grid using hand type mapping
    // Pairs: 0-12
    for (const auto& [hand_idx, strategy] : strategies) {
        if (strategy.probabilities.empty()) continue;
        Hand hand = HandRange::indexToHand(hand_idx);
        if (!hand.isPair()) continue;
        
        int rank = hand.card1().rank();
        int row = 12 - rank;
        auto& cell = grid[row][row];
        cell.has_data = true;
        if (strategy.probabilities.size() > 0) cell.fold = strategy.probabilities[0];
        if (strategy.probabilities.size() > 1) cell.check = strategy.probabilities[1];
        if (strategy.probabilities.size() > 2) cell.call = strategy.probabilities[2];
        if (strategy.probabilities.size() > 3) cell.bet = strategy.probabilities[3];
        if (strategy.probabilities.size() > 4) cell.raise = strategy.probabilities[4];
    }
    
    // Suited: fill above diagonal (high card = row, low card = col)
    for (int rh = 12; rh >= 1; --rh) {
        for (int rl = rh - 1; rl >= 0; --rl) {
            int row = 12 - rh;
            int col = 12 - rl;
            // Find a suited hand with these ranks
            for (const auto& [hand_idx, strategy] : strategies) {
                if (strategy.probabilities.empty()) continue;
                Hand hand = HandRange::indexToHand(hand_idx);
                if (!hand.isSuited()) continue;
                
                int r1 = hand.card1().rank();
                int r2 = hand.card2().rank();
                if (std::max(r1, r2) == rh && std::min(r1, r2) == rl) {
                    auto& cell = grid[row][col];
                    cell.has_data = true;
                    if (strategy.probabilities.size() > 0) cell.fold = strategy.probabilities[0];
                    if (strategy.probabilities.size() > 1) cell.check = strategy.probabilities[1];
                    if (strategy.probabilities.size() > 2) cell.call = strategy.probabilities[2];
                    if (strategy.probabilities.size() > 3) cell.bet = strategy.probabilities[3];
                    if (strategy.probabilities.size() > 4) cell.raise = strategy.probabilities[4];
                    break;
                }
            }
        }
    }
    
    // Offsuit: fill below diagonal
    for (int rh = 12; rh >= 1; --rh) {
        for (int rl = rh - 1; rl >= 0; --rl) {
            int row = 12 - rl;
            int col = 12 - rh;
            for (const auto& [hand_idx, strategy] : strategies) {
                if (strategy.probabilities.empty()) continue;
                Hand hand = HandRange::indexToHand(hand_idx);
                if (hand.isPair() || hand.isSuited()) continue;
                
                int r1 = hand.card1().rank();
                int r2 = hand.card2().rank();
                if (std::max(r1, r2) == rh && std::min(r1, r2) == rl) {
                    auto& cell = grid[row][col];
                    cell.has_data = true;
                    if (strategy.probabilities.size() > 0) cell.fold = strategy.probabilities[0];
                    if (strategy.probabilities.size() > 1) cell.check = strategy.probabilities[1];
                    if (strategy.probabilities.size() > 2) cell.call = strategy.probabilities[2];
                    if (strategy.probabilities.size() > 3) cell.bet = strategy.probabilities[3];
                    if (strategy.probabilities.size() > 4) cell.raise = strategy.probabilities[4];
                    break;
                }
            }
        }
    }
    
    // Print grid with colors
    std::cout << "Grid color values (fold/check/call/bet):" << std::endl;
    std::cout << "    A      K      Q      J      T      9      8      7      6      5      4      3      2" << std::endl;
    
    for (int r = 0; r < 13; ++r) {
        char rank = "AKQJT98765432"[r];
        std::cout << rank << " ";
        
        for (int c = 0; c < 13; ++c) {
            auto& cell = grid[r][c];
            if (!cell.has_data) {
                std::cout << "[NODATA] ";
            } else {
                // Print dominant action
                float max_val = std::max({cell.fold, cell.check, cell.call, cell.bet});
                char action = '?';
                if (max_val == cell.fold) action = 'F';
                else if (max_val == cell.check) action = 'C';
                else if (max_val == cell.call) action = 'L';
                else if (max_val == cell.bet) action = 'B';
                
                std::cout << "[" << action << std::setw(4) << std::setprecision(2) << max_val << "] ";
            }
        }
        std::cout << std::endl;
    }
    
    // Count cells with data
    int with_data = 0;
    for (int r = 0; r < 13; ++r) {
        for (int c = 0; c < 13; ++c) {
            if (grid[r][c].has_data) with_data++;
        }
    }
    std::cout << "\nCells with data: " << with_data << "/169" << std::endl;
    
    return 0;
}
