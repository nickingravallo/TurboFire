#pragma once

#include "core/game_state.hpp"
#include "core/hand.hpp"
#include "core/strategy.hpp"
#include <memory>

// Forward declarations
class TreeNode;
class TreeBuilder;

class CFREngine {
public:
    CFREngine(const GameState& initial_state);
    ~CFREngine();
    
    // Run CFR+ for specified number of iterations
    void solve(int iterations = 100000);
    
    // Get strategy for a specific hand at current game state
    Strategy get_strategy(const Hand& hand);
    
    // Check if solving is complete
    bool is_solved() const { return solved_; }
    
private:
    GameState initial_state_;
    std::unique_ptr<TreeBuilder> tree_builder_;
    std::unique_ptr<TreeNode> root_node_;
    bool solved_;
    
    // CFR+ algorithm implementation
    void run_cfr_iteration();
    void update_regrets();
    void update_strategies();
};

// Stub implementations - full CFR will be implemented later
class TreeNode {
public:
    // Placeholder for now
};

class TreeBuilder {
public:
    TreeBuilder(const GameState& /*state*/) {}
    std::unique_ptr<TreeNode> build_tree() {
        return std::make_unique<TreeNode>();
    }
};
