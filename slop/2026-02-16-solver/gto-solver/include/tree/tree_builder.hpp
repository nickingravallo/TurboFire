#pragma once

#include <queue>
#include <functional>
#include <unordered_map>
#include "tree_node.hpp"
#include "../core/game_state.hpp"
#include "../core/ranges.hpp"

namespace gto {

// Phase 1: Tree Builder
// Pre-allocates all nodes in the game tree by walking the FSM
class TreeBuilder {
public:
    struct BuildConfig {
        float initial_pot;        // SB + BB
        float initial_stack;      // 100bb - 0.5bb posted
        size_t max_nodes;         // Maximum tree size
        bool use_abstraction;     // Use bet size abstraction
        
        BuildConfig() 
            : initial_pot(1.5f)
            , initial_stack(99.5f)
            , max_nodes(5000000)
            , use_abstraction(true) {}
    };
    
    explicit TreeBuilder(const BuildConfig& config = BuildConfig{}) 
        : config_(config) {}
    
    [[nodiscard]] GameTree build_tree() {
        GameTree tree(config_.max_nodes);
        
        // Create initial state (BB to act first preflop)
        GameState initial_state;
        initial_state.pot = config_.initial_pot;
        initial_state.effective_stack = config_.initial_stack;
        initial_state.current_bet_to_call = 0.5f;  // BB has effectively bet 1bb, SB has posted 0.5bb
        initial_state.last_raise_size = 1.0f;
        initial_state.street = Street::Preflop;
        initial_state.street_action_count = 0;
        initial_state.is_oop_turn = true;  // BB acts first
        
        // Build tree using BFS to pre-allocate all nodes
        std::queue<std::pair<int32_t, GameState>> queue;
        
        int32_t root = tree.add_decision_node(initial_state, -1);
        tree.root_idx = root;
        queue.push({root, initial_state});
        
        size_t node_count = 0;
        
        while (!queue.empty() && node_count < config_.max_nodes) {
            auto [parent_idx, state] = queue.front();
            queue.pop();
            
            if (state.is_terminal) {
                continue;
            }
            
            // Get legal actions for this state
            auto legal_actions = get_legal_actions(state);
            
            // Filter to only valid actions
            std::vector<Action> valid_actions;
            for (const auto& action : legal_actions) {
                if (action.type == ActionType::Fold && state.current_bet_to_call == 0.0f) {
                    continue;  // Can't fold when no bet to call
                }
                if (action.type != ActionType::Fold || action.type != ActionType::Check || 
                    action.type != ActionType::Call || action.type != ActionType::Bet ||
                    action.type != ActionType::Raise || action.type != ActionType::AllIn) {
                    // Valid action type
                }
                if (action.type == ActionType::Fold || action.type == ActionType::Check ||
                    action.type == ActionType::Call || action.type == ActionType::Bet ||
                    action.type == ActionType::Raise || action.type == ActionType::AllIn) {
                    valid_actions.push_back(action);
                }
            }
            
            // If no valid actions, this shouldn't happen in a valid game
            if (valid_actions.empty()) {
                continue;
            }
            
            // Set legal actions for this node
            tree.set_legal_actions(parent_idx, valid_actions);
            
            // Initialize CFR data for decision node
            tree.storage.initialize_cfr_data(parent_idx, valid_actions.size(), 169);
            
            // Create child nodes for each action
            for (const auto& action : valid_actions) {
                GameState new_state = apply_action(state, action);
                
                int32_t child_idx;
                if (new_state.is_terminal) {
                    child_idx = tree.add_terminal_node(new_state, parent_idx);
                } else {
                    child_idx = tree.add_decision_node(new_state, parent_idx);
                    queue.push({child_idx, new_state});
                }
                
                tree.connect_child(parent_idx, child_idx);
            }
            
            node_count++;
        }
        
        return tree;
    }
    
    [[nodiscard]] GameTree build_preflop_only() {
        GameTree tree(config_.max_nodes);
        
        // Create initial state
        GameState initial_state;
        initial_state.pot = config_.initial_pot;
        initial_state.effective_stack = config_.initial_stack;
        initial_state.current_bet_to_call = 0.5f;
        initial_state.last_raise_size = 1.0f;
        initial_state.street = Street::Preflop;
        initial_state.street_action_count = 0;
        initial_state.is_oop_turn = true;
        
        std::queue<std::pair<int32_t, GameState>> queue;
        
        int32_t root = tree.add_decision_node(initial_state, -1);
        tree.root_idx = root;
        queue.push({root, initial_state});
        
        size_t node_count = 0;
        
        while (!queue.empty() && node_count < config_.max_nodes) {
            auto [parent_idx, state] = queue.front();
            queue.pop();
            
            if (state.is_terminal) {
                continue;
            }
            
            // Stop at flop (only build preflop tree)
            if (state.street != Street::Preflop) {
                // Make this a terminal node for now
                continue;
            }
            
            auto legal_actions = get_legal_actions(state);
            
            std::vector<Action> valid_actions;
            for (const auto& action : legal_actions) {
                if (action.type == ActionType::Fold && state.current_bet_to_call == 0.0f) {
                    continue;
                }
                if (action.type == ActionType::Fold || action.type == ActionType::Check ||
                    action.type == ActionType::Call || action.type == ActionType::Bet ||
                    action.type == ActionType::Raise || action.type == ActionType::AllIn) {
                    valid_actions.push_back(action);
                }
            }
            
            if (valid_actions.empty()) {
                continue;
            }
            
            tree.set_legal_actions(parent_idx, valid_actions);
            tree.storage.initialize_cfr_data(parent_idx, valid_actions.size(), 169);
            
            for (const auto& action : valid_actions) {
                GameState new_state = apply_action(state, action);
                
                int32_t child_idx;
                if (new_state.is_terminal || new_state.street != Street::Preflop) {
                    child_idx = tree.add_terminal_node(new_state, parent_idx);
                } else {
                    child_idx = tree.add_decision_node(new_state, parent_idx);
                    queue.push({child_idx, new_state});
                }
                
                tree.connect_child(parent_idx, child_idx);
            }
            
            node_count++;
        }
        
        return tree;
    }

private:
    BuildConfig config_;
};

} // namespace gto
