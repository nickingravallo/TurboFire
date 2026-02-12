#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <unordered_map>
#include <random>
#include "game/game_tree.h"

namespace poker {

// CFR Solver configuration
struct CFRSolverConfig {
    uint32_t iterations;
    float regret_floor;
    float regret_ceiling;
    bool use_discounting;
    float discount_factor;
    
    CFRSolverConfig()
        : iterations(100000)
        , regret_floor(-300000.0f)
        , regret_ceiling(300000.0f)
        , use_discounting(true)
        , discount_factor(1.5f) {}
};

// CFR (Counterfactual Regret Minimization) implementation
// Uses External Sampling MCCFR for memory efficiency
class CFRSolver {
public:
    using Config = CFRSolverConfig;
    
    CFRSolver(const Config& config = Config());
    
    // Solve a game tree
    void solve(NodePtr root);
    
    // Get strategy for an information set
    Strategy getStrategy(const InfoSetKey& key) const;
    
    // Training statistics
    struct Stats {
        uint32_t iterations_completed = 0;
        float avg_exploitability = 0.0f;
        double time_elapsed_sec = 0.0;
    };
    Stats getStats() const { return stats_; }
    
private:
    Config config_;
    Stats stats_;
    std::mt19937 rng_;
    
    // Regret tables: info set -> action regrets
    std::unordered_map<InfoSetKey, std::vector<float>, InfoSetKeyHash> regrets_;
    
    // Strategy tables: info set -> action strategy sums
    std::unordered_map<InfoSetKey, std::vector<float>, InfoSetKeyHash> strategy_sums_;
    
    // CFR iteration
    float cfr(NodePtr node, const std::vector<uint32_t>& hand_buckets, 
              Position player, float reach_probability);
    
    // External sampling - sample opponent actions
    Action sampleAction(const Strategy& strategy, const std::vector<Action>& actions);
    
    // Regret matching
    Strategy regretMatching(const std::vector<float>& regrets);
    
    // Calculate counterfactual value
    float calculateCFV(NodePtr node, const Hand& hand, 
                      const HandRange& opponent_range, Position player);
    
    // Discount regrets for faster convergence
    void discountRegrets(uint32_t iteration);
    
    // Count nodes in tree
    int countNodes(NodePtr node);
    
    // Copy computed strategies to tree nodes
    void copyStrategiesToTree(NodePtr node);
};

} // namespace poker
