#pragma once

#include <vector>
#include <cstdint>
#include <span>
#include "../core/game_state.hpp"

namespace gto {

// Node types in the game tree
enum class NodeType : uint8_t {
    Decision = 0,   // Player needs to make a decision
    Chance = 1,     // Random event (card dealt)
    Terminal = 2    // Game over, payoff determined
};

// Struct of Arrays layout for cache locality
// All data is stored in contiguous vectors
struct TreeStorage {
    // Node metadata
    std::vector<NodeType> node_types;
    std::vector<GameState> states;
    std::vector<int32_t> parent_indices;
    
    // Action information
    std::vector<std::vector<Action>> legal_actions;
    std::vector<std::vector<int32_t>> child_indices;
    
    // CFR data (only for decision nodes)
    std::vector<std::vector<float>> regret_sums;
    std::vector<std::vector<float>> strategy_sums;
    std::vector<std::vector<float>> current_strategies;
    
    // Range probabilities (for each hand in range)
    std::vector<std::vector<float>> reach_probabilities_oop;  // BB
    std::vector<std::vector<float>> reach_probabilities_ip;   // SB
    
    // Utility values for terminal nodes
    std::vector<float> terminal_values;
    
    // Pre-allocate storage for known number of nodes
    void reserve(size_t num_nodes, size_t hands_per_node = 169) {
        node_types.reserve(num_nodes);
        states.reserve(num_nodes);
        parent_indices.reserve(num_nodes);
        legal_actions.reserve(num_nodes);
        child_indices.reserve(num_nodes);
        regret_sums.reserve(num_nodes);
        strategy_sums.reserve(num_nodes);
        current_strategies.reserve(num_nodes);
        reach_probabilities_oop.reserve(num_nodes);
        reach_probabilities_ip.reserve(num_nodes);
        terminal_values.reserve(num_nodes);
    }
    
    [[nodiscard]] size_t size() const noexcept {
        return node_types.size();
    }
    
    [[nodiscard]] bool empty() const noexcept {
        return node_types.empty();
    }
    
    // Add a new node to the tree
    [[nodiscard]] int32_t add_node(
        NodeType type,
        const GameState& state,
        int32_t parent = -1,
        size_t num_hands = 169
    ) {
        int32_t idx = static_cast<int32_t>(node_types.size());
        
        node_types.push_back(type);
        states.push_back(state);
        parent_indices.push_back(parent);
        legal_actions.emplace_back();
        child_indices.emplace_back();
        
        if (type == NodeType::Decision) {
            regret_sums.emplace_back();
            strategy_sums.emplace_back();
            current_strategies.emplace_back();
            reach_probabilities_oop.emplace_back(num_hands, 0.0f);
            reach_probabilities_ip.emplace_back(num_hands, 0.0f);
        } else {
            regret_sums.emplace_back();
            strategy_sums.emplace_back();
            current_strategies.emplace_back();
            reach_probabilities_oop.emplace_back();
            reach_probabilities_ip.emplace_back();
        }
        
        if (type == NodeType::Terminal) {
            terminal_values.push_back(state.terminal_value);
        } else {
            terminal_values.push_back(0.0f);
        }
        
        return idx;
    }
    
    // Initialize CFR data for a decision node with specific number of actions
    void initialize_cfr_data(int32_t node_idx, size_t num_actions, size_t num_hands = 169) {
        if (node_idx < 0 || static_cast<size_t>(node_idx) >= size()) return;
        if (node_types[node_idx] != NodeType::Decision) return;
        
        regret_sums[node_idx].assign(num_actions * num_hands, 0.0f);
        strategy_sums[node_idx].assign(num_actions * num_hands, 0.0f);
        current_strategies[node_idx].assign(num_actions * num_hands, 1.0f / num_actions);
    }
    
    // Get regret sum for specific (node, action, hand)
    [[nodiscard]] float& regret(int32_t node_idx, size_t action_idx, size_t hand_idx, size_t num_hands = 169) {
        return regret_sums[node_idx][action_idx * num_hands + hand_idx];
    }
    
    [[nodiscard]] const float& regret(int32_t node_idx, size_t action_idx, size_t hand_idx, size_t num_hands = 169) const {
        return regret_sums[node_idx][action_idx * num_hands + hand_idx];
    }
    
    // Get strategy sum for specific (node, action, hand)
    [[nodiscard]] float& strategy(int32_t node_idx, size_t action_idx, size_t hand_idx, size_t num_hands = 169) {
        return strategy_sums[node_idx][action_idx * num_hands + hand_idx];
    }
    
    [[nodiscard]] const float& strategy(int32_t node_idx, size_t action_idx, size_t hand_idx, size_t num_hands = 169) const {
        return strategy_sums[node_idx][action_idx * num_hands + hand_idx];
    }
    
    // Get current strategy for specific (node, action, hand)
    [[nodiscard]] float& curr_strategy(int32_t node_idx, size_t action_idx, size_t hand_idx, size_t num_hands = 169) {
        return current_strategies[node_idx][action_idx * num_hands + hand_idx];
    }
    
    [[nodiscard]] const float& curr_strategy(int32_t node_idx, size_t action_idx, size_t hand_idx, size_t num_hands = 169) const {
        return current_strategies[node_idx][action_idx * num_hands + hand_idx];
    }
    
    // Compute average strategy from strategy sums
    [[nodiscard]] std::vector<float> get_average_strategy(int32_t node_idx, size_t num_hands = 169) const {
        if (node_idx < 0 || static_cast<size_t>(node_idx) >= size()) return {};
        if (node_types[node_idx] != NodeType::Decision) return {};
        
        const auto& strat_sums = strategy_sums[node_idx];
        size_t num_actions = legal_actions[node_idx].size();
        std::vector<float> avg_strat(strat_sums.size());
        
        for (size_t h = 0; h < num_hands; ++h) {
            float sum = 0.0f;
            for (size_t a = 0; a < num_actions; ++a) {
                sum += strat_sums[a * num_hands + h];
            }
            if (sum > 0.0f) {
                for (size_t a = 0; a < num_actions; ++a) {
                    avg_strat[a * num_hands + h] = strat_sums[a * num_hands + h] / sum;
                }
            } else {
                for (size_t a = 0; a < num_actions; ++a) {
                    avg_strat[a * num_hands + h] = 1.0f / num_actions;
                }
            }
        }
        
        return avg_strat;
    }
};

// Wrapper class for easier tree traversal
class GameTree {
public:
    TreeStorage storage;
    int32_t root_idx = -1;
    
    explicit GameTree(size_t expected_nodes = 100000) {
        storage.reserve(expected_nodes);
    }
    
    [[nodiscard]] int32_t add_decision_node(const GameState& state, int32_t parent = -1) {
        return storage.add_node(NodeType::Decision, state, parent);
    }
    
    [[nodiscard]] int32_t add_terminal_node(const GameState& state, int32_t parent = -1) {
        return storage.add_node(NodeType::Terminal, state, parent);
    }
    
    void connect_child(int32_t parent_idx, int32_t child_idx) {
        if (parent_idx >= 0 && static_cast<size_t>(parent_idx) < storage.size()) {
            storage.child_indices[parent_idx].push_back(child_idx);
        }
    }
    
    void set_legal_actions(int32_t node_idx, const std::vector<Action>& actions) {
        if (node_idx >= 0 && static_cast<size_t>(node_idx) < storage.size()) {
            storage.legal_actions[node_idx] = actions;
        }
    }
    
    [[nodiscard]] bool is_terminal(int32_t node_idx) const {
        if (node_idx < 0 || static_cast<size_t>(node_idx) >= storage.size()) return false;
        return storage.node_types[node_idx] == NodeType::Terminal;
    }
    
    [[nodiscard]] bool is_decision(int32_t node_idx) const {
        if (node_idx < 0 || static_cast<size_t>(node_idx) >= storage.size()) return false;
        return storage.node_types[node_idx] == NodeType::Decision;
    }
};

} // namespace gto
