#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include "game/game_tree.h"
#include "solver/cfr.h"

namespace poker {

// Solver configuration
struct SolverConfig {
    uint32_t iterations;
    bool save_solved;
    std::string save_directory;
    
    SolverConfig()
        : iterations(100000)
        , save_solved(true)
        , save_directory("./solves/") {}
    
    SolverConfig(uint32_t iter, bool save, std::string dir)
        : iterations(iter)
        , save_solved(save)
        , save_directory(std::move(dir)) {}
};

// High-level solver interface
class Solver {
public:
    using Config = SolverConfig;
    
    Solver(const Config& config = Config());
    
    // Solve current position
    void solve(const Board& board, const std::vector<Action>& history);
    
    // Check if position is already solved
    bool isSolved(const Board& board, const std::vector<Action>& history) const;
    
    // Load solved position
    bool load(const Board& board, const std::vector<Action>& history);
    
    // Save current solution
    void save();
    
    // Get strategy for a specific hand
    Strategy getHandStrategy(uint32_t hand_index) const;
    
    // Get all strategies for current position
    std::vector<std::pair<uint32_t, Strategy>> getAllStrategies() const;
    
    // Get solve progress (0.0 - 1.0)
    float getProgress() const;
    
    // Stop solving
    void stop();
    
private:
    Config config_;
    std::unique_ptr<class CFRSolver> cfr_solver_;
    NodePtr current_tree_;
    bool solving_ = false;
    bool stop_requested_ = false;
    
    // Generate filename from board + history
    std::string generateFilename(const Board& board, const std::vector<Action>& history) const;
    
    // Hash function for position
    uint64_t hashPosition(const Board& board, const std::vector<Action>& history) const;
};

} // namespace poker
