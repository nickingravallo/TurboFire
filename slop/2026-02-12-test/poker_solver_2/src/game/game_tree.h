#pragma once

#include <memory>
#include <vector>
#include <array>
#include <cstdint>
#include <unordered_map>
#include "card/hand.h"
#include "card/evaluator.h"
#include "game/board.h"
#include "game/action.h"

namespace poker {

// Forward declarations
class GameTreeNode;
using NodePtr = std::shared_ptr<GameTreeNode>;

// Strategy for a node: probability distribution over actions
struct Strategy {
    std::vector<float> probabilities; // Indexed by action
    
    Strategy() = default;
    explicit Strategy(size_t num_actions) : probabilities(num_actions, 0.0f) {}
    
    void normalize();
    size_t bestAction() const;
};

// Information set key for CFR
struct InfoSetKey {
    uint64_t hand_hash;      // Isomorphic hand representation
    uint64_t board_hash;     // Board cards hash
    uint64_t history_hash;   // Action history hash
    
    bool operator==(const InfoSetKey& other) const;
};

// Hash function for InfoSetKey
struct InfoSetKeyHash {
    size_t operator()(const InfoSetKey& key) const;
};

// Game tree node
class GameTreeNode {
public:
    enum Type {
        DECISION,    // Player decision node
        CHANCE,      // Chance node (deal cards)
        TERMINAL     // Terminal node (showdown or fold)
    };
    
    Type type;
    Street street;
    Position player_to_act;  // For DECISION nodes
    float pot_size;
    float stack_sb;
    float stack_bb;
    float current_bet;
    Board board;
    std::vector<Action> actions;  // Available actions
    std::vector<NodePtr> children;
    
    // For CFR
    Strategy strategy;
    std::vector<float> regrets;
    std::vector<float> avg_strategy;
    
    // For isomorphic abstraction
    std::vector<uint32_t> hand_buckets; // Maps 1326 hands to buckets
    
    GameTreeNode(Type t) : type(t), street(Street::PREFLOP), player_to_act(Position::SB),
                          pot_size(1.5f), stack_sb(99.0f), stack_bb(99.0f), current_bet(0.5f) {}
    
    // Build game tree from current state
    static NodePtr buildTree(const Board& board, Street street, Position to_act,
                            float pot, float sb_stack, float bb_stack, float current_bet);
    
    // Get available actions for this node
    std::vector<Action> getAvailableActions() const;
    
    // Check if node is terminal
    bool isTerminal() const { return type == TERMINAL; }
    
    // Calculate EV for a hand at this node
    float calculateEV(const Hand& hand, const HandRange& opponent_range) const;
};

// Game tree manager
class GameTree {
public:
    GameTree();
    
    // Build tree for current state
    void build(const Board& board, Street street);
    
    // Get current node
    NodePtr getCurrentNode() const { return current_node_; }
    
    // Navigate tree
    void applyAction(const Action& action);
    void undoLastAction();
    
    // Get action history
    std::vector<Action> getHistory() const { return history_; }
    
private:
    NodePtr root_;
    NodePtr current_node_;
    std::vector<Action> history_;
    std::vector<NodePtr> node_stack_;
};

} // namespace poker
