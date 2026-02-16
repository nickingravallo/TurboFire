#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>
#include "../tree/tree_node.hpp"
#include "../core/ranges.hpp"

namespace gto {

// Phase 2: CFR+ Engine
// Runs regret matching over pre-built game tree
class CFREngine {
public:
    struct CFRConfig {
        size_t num_iterations;
        float regret_floor;
        bool use_discounting;
        float alpha;
        float beta;
        float gamma;

        CFRConfig()
            : num_iterations(100000)
            , regret_floor(0.0f)
            , use_discounting(true)
            , alpha(1.5f)
            , beta(0.0f)
            , gamma(2.0f) {}
    };
    
    explicit CFREngine(const CFRConfig& config = CFRConfig{}) 
        : config_(config) {}
    
    void solve(GameTree& tree) {
        std::cout << "Starting CFR+ solver with " << config_.num_iterations << " iterations..." << std::endl;
        
        // Initialize reach probabilities at root
        auto sb_range = PreflopRanges::get_sb_opening_range();
        auto bb_range = PreflopRanges::get_bb_defend_range();
        
        initialize_reach_probs(tree, sb_range, bb_range);
        
        // Run CFR iterations
        for (size_t t = 1; t <= config_.num_iterations; ++t) {
            // Alternate between players (OOP = BB, IP = SB)
            // In our FSM, is_oop_turn = true means BB to act
            
            // Traverse tree and update regrets
            cfr_traverse(tree, tree.root_idx, true, t);   // BB perspective
            cfr_traverse(tree, tree.root_idx, false, t);  // SB perspective
            
            // Discounting for DCFR
            if (config_.use_discounting && t > 1) {
                apply_discounting(tree, t);
            }
            
            if (t % 10000 == 0) {
                std::cout << "Iteration " << t << "/" << config_.num_iterations << std::endl;
            }
        }
        
        std::cout << "CFR+ complete!" << std::endl;
    }
    
    // Get Nash equilibrium strategy for a node
    [[nodiscard]] std::vector<float> get_strategy(
        const GameTree& tree, 
        int32_t node_idx,
        size_t num_hands = 169
    ) const {
        if (node_idx < 0 || !tree.is_decision(node_idx)) {
            return {};
        }
        
        return tree.storage.get_average_strategy(node_idx, num_hands);
    }

private:
    CFRConfig config_;
    
    void initialize_reach_probs(GameTree& tree, const RangeWeights& sb_range, const RangeWeights& bb_range) {
        if (tree.root_idx < 0) return;
        
        auto& oop_probs = tree.storage.reach_probabilities_oop[tree.root_idx];
        auto& ip_probs = tree.storage.reach_probabilities_ip[tree.root_idx];
        
        // BB (OOP) defending range
        for (size_t i = 0; i < 169; ++i) {
            oop_probs[i] = bb_range[i];
        }
        
        // SB (IP) opening range
        for (size_t i = 0; i < 169; ++i) {
            ip_probs[i] = sb_range[i];
        }
        
        // Normalize probabilities
        normalize_probs(oop_probs);
        normalize_probs(ip_probs);
    }
    
    void normalize_probs(std::vector<float>& probs) {
        float sum = std::accumulate(probs.begin(), probs.end(), 0.0f);
        if (sum > 0.0f) {
            for (auto& p : probs) {
                p /= sum;
            }
        }
    }
    
    // CFR traversal
    float cfr_traverse(GameTree& tree, int32_t node_idx, bool oop_perspective, size_t iteration) {
        if (node_idx < 0) return 0.0f;
        
        const auto& state = tree.storage.states[node_idx];
        
        if (tree.is_terminal(node_idx)) {
            return state.terminal_value;
        }
        
        if (!tree.is_decision(node_idx)) {
            return 0.0f;
        }
        
        const auto& actions = tree.storage.legal_actions[node_idx];
        const auto& children = tree.storage.child_indices[node_idx];
        size_t num_actions = actions.size();
        size_t num_hands = 169;
        
        if (num_actions == 0 || children.size() != num_actions) {
            return 0.0f;
        }
        
        // Get current strategy (regret matching)
        auto strategy = compute_strategy(tree, node_idx, num_actions, num_hands);
        
        // Get reach probabilities
        auto& reach_oop = tree.storage.reach_probabilities_oop[node_idx];
        auto& reach_ip = tree.storage.reach_probabilities_ip[node_idx];
        
        // Compute counterfactual values for each action
        std::vector<std::vector<float>> action_values(num_actions, std::vector<float>(num_hands, 0.0f));
        std::vector<float> node_value(num_hands, 0.0f);
        
        for (size_t a = 0; a < num_actions; ++a) {
            int32_t child_idx = children[a];
            
            // Update reach probs for child
            if (child_idx >= 0) {
                if (tree.storage.reach_probabilities_oop[child_idx].empty()) {
                    tree.storage.reach_probabilities_oop[child_idx].resize(num_hands);
                }
                if (tree.storage.reach_probabilities_ip[child_idx].empty()) {
                    tree.storage.reach_probabilities_ip[child_idx].resize(num_hands);
                }
                
                // Propagate reach probabilities
                for (size_t h = 0; h < num_hands; ++h) {
                    if (oop_perspective) {
                        tree.storage.reach_probabilities_oop[child_idx][h] = reach_oop[h] * strategy[a * num_hands + h];
                        tree.storage.reach_probabilities_ip[child_idx][h] = reach_ip[h];
                    } else {
                        tree.storage.reach_probabilities_oop[child_idx][h] = reach_oop[h];
                        tree.storage.reach_probabilities_ip[child_idx][h] = reach_ip[h] * strategy[a * num_hands + h];
                    }
                }
            }
            
            // Recursively get value
            float child_value = cfr_traverse(tree, child_idx, oop_perspective, iteration);
            
            for (size_t h = 0; h < num_hands; ++h) {
                action_values[a][h] = child_value;
                node_value[h] += strategy[a * num_hands + h] * action_values[a][h];
            }
        }
        
        // Update regrets (only for the acting player)
        bool is_oop_turn = state.is_oop_turn;
        if (is_oop_turn == oop_perspective) {
            auto& regrets = tree.storage.regret_sums[node_idx];
            auto& strat_sums = tree.storage.strategy_sums[node_idx];
            
            for (size_t a = 0; a < num_actions; ++a) {
                for (size_t h = 0; h < num_hands; ++h) {
                    float counterfactual_reach = oop_perspective ? reach_ip[h] : reach_oop[h];
                    float regret = counterfactual_reach * (action_values[a][h] - node_value[h]);
                    
                    // CFR+: floor negative regrets at 0
                    regrets[a * num_hands + h] += regret;
                    if (regrets[a * num_hands + h] < config_.regret_floor) {
                        regrets[a * num_hands + h] = config_.regret_floor;
                    }
                    
                    // Accumulate strategy
                    float my_reach = oop_perspective ? reach_oop[h] : reach_ip[h];
                    strat_sums[a * num_hands + h] += my_reach * strategy[a * num_hands + h];
                }
            }
        }
        
        return 0.0f;  // Return value from node (simplified)
    }
    
    // Regret matching to compute current strategy
    [[nodiscard]] std::vector<float> compute_strategy(
        const GameTree& tree,
        int32_t node_idx,
        size_t num_actions,
        size_t num_hands
    ) const {
        std::vector<float> strategy(num_actions * num_hands);
        const auto& regrets = tree.storage.regret_sums[node_idx];
        
        for (size_t h = 0; h < num_hands; ++h) {
            float regret_sum = 0.0f;
            for (size_t a = 0; a < num_actions; ++a) {
                float r = regrets[a * num_hands + h];
                if (r > 0.0f) {
                    regret_sum += r;
                }
            }
            
            for (size_t a = 0; a < num_actions; ++a) {
                float r = regrets[a * num_hands + h];
                if (regret_sum > 0.0f && r > 0.0f) {
                    strategy[a * num_hands + h] = r / regret_sum;
                } else {
                    strategy[a * num_hands + h] = 1.0f / num_actions;
                }
            }
        }
        
        return strategy;
    }
    
    // DCFR discounting
    void apply_discounting(GameTree& tree, size_t iteration) {
        float t = static_cast<float>(iteration);
        float regret_discount = std::pow(t / (t + 1), config_.alpha);
        float strat_discount = std::pow(t / (t + 1), config_.beta);
        
        for (size_t node_idx = 0; node_idx < tree.storage.size(); ++node_idx) {
            if (!tree.is_decision(node_idx)) continue;
            
            auto& regrets = tree.storage.regret_sums[node_idx];
            auto& strat_sums = tree.storage.strategy_sums[node_idx];
            
            for (auto& r : regrets) {
                r *= regret_discount;
            }
            for (auto& s : strat_sums) {
                s *= strat_discount;
            }
        }
    }
};

} // namespace gto
