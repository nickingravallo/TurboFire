#include "solver/cfr.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <cstring>

namespace poker {

CFRSolver::CFRSolver(const Config& config) 
    : config_(config), rng_(std::random_device{}()) {}

void CFRSolver::solve(NodePtr root) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Initialize hand buckets for all nodes
    std::vector<uint32_t> hand_buckets(1326);
    for (uint32_t i = 0; i < 1326; ++i) {
        hand_buckets[i] = i; // Simplified - use identity mapping
    }
    
    // Count total nodes for progress reporting
    int total_nodes = countNodes(root);
    std::cout << "Building game tree with " << total_nodes << " nodes..." << std::endl;
    
    // Main CFR loop
    std::cout << "Running CFR for " << config_.iterations << " iterations..." << std::endl;
    
    // Create separate regret tables for different hand types
    // 169 hand types: 13 pairs + 78 suited + 78 offsuit
    const uint32_t NUM_HAND_TYPES = 169;
    
    for (uint32_t iter = 0; iter < config_.iterations; ++iter) {
        // Traverse tree for each player and each hand type
        for (int player = 0; player < 2; ++player) {
            // Run CFR for representative hands of each type
            // This reduces computation from 1326 to 169 per iteration
            for (uint32_t hand_type = 0; hand_type < NUM_HAND_TYPES; ++hand_type) {
                // Map hand type to a representative hand index
                uint32_t hand_idx = hand_type * 7; // Spread across all hands
                if (hand_idx >= 1326) hand_idx = hand_idx % 1326;
                
                // Update hand buckets to identify this hand type
                for (auto& bucket : hand_buckets) {
                    bucket = 0;
                }
                hand_buckets[hand_idx] = hand_type + 1; // +1 to distinguish from default
                
                cfr(root, hand_buckets, static_cast<Position>(player), 1.0f);
            }
        }
        
        // Discount regrets periodically
        if (config_.use_discounting && iter > 0 && iter % 1000 == 0) {
            discountRegrets(iter);
        }
        
        stats_.iterations_completed = iter + 1;
        
        // Progress report every 10k iterations
        if ((iter + 1) % 10000 == 0) {
            auto current_time = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(current_time - start_time).count();
            double progress = static_cast<double>(iter + 1) / config_.iterations;
            double estimated_total = elapsed / progress;
            double remaining = estimated_total - elapsed;
            
            std::cout << "Progress: " << (iter + 1) << "/" << config_.iterations 
                      << " (" << static_cast<int>(progress * 100) << "%)"
                      << " - Elapsed: " << static_cast<int>(elapsed) << "s"
                      << " - Remaining: " << static_cast<int>(remaining) << "s"
                      << std::endl;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    stats_.time_elapsed_sec = std::chrono::duration<double>(end_time - start_time).count();
    
    std::cout << "CFR complete in " << stats_.time_elapsed_sec << " seconds" << std::endl;
    std::cout << "Regret table size: " << regrets_.size() << " entries" << std::endl;
    
    // Normalize strategies and copy to tree nodes
    for (auto& [key, strategy_sum] : strategy_sums_) {
        float sum = 0.0f;
        for (float s : strategy_sum) sum += s;
        if (sum > 0.0f) {
            for (auto& s : strategy_sum) s /= sum;
        }
    }
    
    // Copy strategies to tree nodes
    copyStrategiesToTree(root);
}

void CFRSolver::copyStrategiesToTree(NodePtr node) {
    if (!node || node->isTerminal()) return;
    
    // Create info set key for this node
    InfoSetKey key;
    key.hand_hash = 0; // Would be based on hand bucket
    key.board_hash = node->board.toString().length();
    key.history_hash = 0; // Would hash action history
    
    // Get strategy for this node
    Strategy strat = getStrategy(key);
    if (!strat.probabilities.empty()) {
        node->strategy = strat;
        node->avg_strategy = strat.probabilities;
    }
    
    // Recurse to children
    for (auto& child : node->children) {
        copyStrategiesToTree(child);
    }
}

int CFRSolver::countNodes(NodePtr node) {
    if (!node) return 0;
    int count = 1;
    for (const auto& child : node->children) {
        count += countNodes(child);
    }
    return count;
}

float CFRSolver::cfr(NodePtr node, const std::vector<uint32_t>& hand_buckets,
                     Position player, float reach_probability) {
    if (!node) return 0.0f;

    // Terminal node - calculate EV based on pot size and who won
    if (node->isTerminal()) {
        // Simple EV: if we reach terminal, assume average case
        float pot = node->pot_size;

        // For now, return pot-based EV
        // Positive if SB, negative if BB (from SB's perspective)
        if (node->current_bet == 0) {
            return 0.0f;
        } else {
            return (player == Position::SB) ? pot * 0.05f : -pot * 0.05f;
        }
    }

    // Get available actions
    auto actions = node->getAvailableActions();
    if (actions.empty() || node->children.empty()) return 0.0f;
    
    // Create info set key based on current hand
    // Find the hand type we're currently solving for
    uint64_t current_hand = 0;
    for (uint32_t i = 0; i < hand_buckets.size(); ++i) {
        if (hand_buckets[i] > 0) {  // Non-zero means this is the active hand
            current_hand = hand_buckets[i] - 1;  // Subtract 1 to get actual hand type
            break;
        }
    }
    
    InfoSetKey key;
    key.hand_hash = current_hand; // Use hand type to differentiate strategies
    key.board_hash = node->board.toString().length();
    key.history_hash = 0; // Would hash action history
    
    // Get or initialize regrets
    auto& regret_vec = regrets_[key];
    if (regret_vec.empty()) {
        regret_vec.resize(actions.size(), 0.0f);
    }
    
    // Get current strategy via regret matching
    Strategy strategy = regretMatching(regret_vec);
    
    // Accumulate strategy
    auto& strategy_sum = strategy_sums_[key];
    if (strategy_sum.empty()) {
        strategy_sum.resize(actions.size(), 0.0f);
    }
    for (size_t i = 0; i < actions.size(); ++i) {
        strategy_sum[i] += reach_probability * strategy.probabilities[i];
    }
    
    // Calculate value for each action
    std::vector<float> action_values(actions.size(), 0.0f);
    float node_value = 0.0f;

    if (node->player_to_act == player) {
        // Our turn - calculate values for all actions
        for (size_t i = 0; i < actions.size() && i < node->children.size() && i < strategy.probabilities.size(); ++i) {
            if (node->children[i]) {
                action_values[i] = cfr(node->children[i], hand_buckets, player,
                                      reach_probability * strategy.probabilities[i]);
                node_value += strategy.probabilities[i] * action_values[i];
            }
        }
    } else {
        // Opponent's turn - sample one action (external sampling)
        size_t sampled_action = 0;
        if (!strategy.probabilities.empty()) {
            // Sample based on strategy
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            float r = dist(rng_);
            float cum_prob = 0.0f;
            for (size_t i = 0; i < strategy.probabilities.size() && i < node->children.size(); ++i) {
                cum_prob += strategy.probabilities[i];
                if (r <= cum_prob) {
                    sampled_action = i;
                    break;
                }
            }
        }

        if (sampled_action < node->children.size() && node->children[sampled_action]) {
            node_value = cfr(node->children[sampled_action], hand_buckets, player, reach_probability);
        }
        // For external sampling, we don't update regrets here
        return node_value;
    }
    
    // Update regrets (only for the traversing player)
    for (size_t i = 0; i < actions.size(); ++i) {
        float regret = action_values[i] - node_value;
        regret_vec[i] += regret;
        
        // Clamp regrets
        regret_vec[i] = std::max(config_.regret_floor, 
                                std::min(config_.regret_ceiling, regret_vec[i]));
    }
    
    return node_value;
}

Action CFRSolver::sampleAction(const Strategy& strategy, const std::vector<Action>& actions) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float r = dist(rng_);
    
    float cum_prob = 0.0f;
    for (size_t i = 0; i < strategy.probabilities.size() && i < actions.size(); ++i) {
        cum_prob += strategy.probabilities[i];
        if (r <= cum_prob) {
            return actions[i];
        }
    }
    
    return actions.empty() ? Action() : actions.back();
}

Strategy CFRSolver::regretMatching(const std::vector<float>& regrets) {
    Strategy strategy(regrets.size());
    
    float sum_positive_regrets = 0.0f;
    for (float r : regrets) {
        if (r > 0.0f) sum_positive_regrets += r;
    }
    
    if (sum_positive_regrets > 0.0f) {
        for (size_t i = 0; i < regrets.size(); ++i) {
            strategy.probabilities[i] = std::max(0.0f, regrets[i]) / sum_positive_regrets;
        }
    } else {
        // Uniform distribution if no positive regrets
        for (auto& p : strategy.probabilities) {
            p = 1.0f / regrets.size();
        }
    }
    
    return strategy;
}

float CFRSolver::calculateCFV(NodePtr node, const Hand& hand,
                             const HandRange& opponent_range, Position player) {
    if (!node) return 0.0f;

    if (node->isTerminal()) {
        // Calculate showdown EV or fold EV
        // For now, return pot size as placeholder
        // Real implementation would compare hand strengths
        return node->pot_size * (player == Position::SB ? 1.0f : -1.0f);
    }
    
    // For non-terminal nodes, use the current strategy to calculate expected value
    float ev = 0.0f;
    auto actions = node->getAvailableActions();
    
    for (size_t i = 0; i < actions.size() && i < node->children.size(); ++i) {
        float action_ev = calculateCFV(node->children[i], hand, opponent_range, player);
        float prob = (i < node->strategy.probabilities.size()) ? 
                     node->strategy.probabilities[i] : 1.0f / actions.size();
        ev += prob * action_ev;
    }
    
    return ev;
}

void CFRSolver::discountRegrets(uint32_t iteration) {
    float factor = std::pow(static_cast<float>(iteration) / (iteration + 1), config_.discount_factor);
    for (auto& [key, regrets] : regrets_) {
        for (auto& r : regrets) {
            r *= factor;
        }
    }
}

Strategy CFRSolver::getStrategy(const InfoSetKey& key) const {
    auto it = strategy_sums_.find(key);
    if (it != strategy_sums_.end()) {
        Strategy strategy(it->second.size());
        strategy.probabilities = it->second;
        return strategy;
    }
    return Strategy();
}

} // namespace poker
