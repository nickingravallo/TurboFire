#include "game/game_tree.h"
#include <algorithm>

namespace poker {

// Strategy implementation
void Strategy::normalize() {
    float sum = 0.0f;
    for (float p : probabilities) sum += p;
    if (sum > 0.0f) {
        for (auto& p : probabilities) p /= sum;
    }
}

size_t Strategy::bestAction() const {
    size_t best = 0;
    float max_prob = probabilities[0];
    for (size_t i = 1; i < probabilities.size(); ++i) {
        if (probabilities[i] > max_prob) {
            max_prob = probabilities[i];
            best = i;
        }
    }
    return best;
}

// InfoSetKey implementation
bool InfoSetKey::operator==(const InfoSetKey& other) const {
    return hand_hash == other.hand_hash && 
           board_hash == other.board_hash && 
           history_hash == other.history_hash;
}

size_t InfoSetKeyHash::operator()(const InfoSetKey& key) const {
    return std::hash<uint64_t>()(key.hand_hash) ^
           (std::hash<uint64_t>()(key.board_hash) << 1) ^
           (std::hash<uint64_t>()(key.history_hash) << 2);
}

// Forward declaration for recursive build
NodePtr buildTreeRecursive(const Board& board, Street street, Position to_act,
                           float pot, float sb_stack, float bb_stack, float current_bet,
                           int depth, int max_depth);

// GameTreeNode implementation
NodePtr GameTreeNode::buildTree(const Board& board, Street street, Position to_act,
                                float pot, float sb_stack, float bb_stack, float current_bet) {
    // Build with depth 3 for reasonable coverage
    // This creates ~40-80 nodes depending on action sequences
    return buildTreeRecursive(board, street, to_act, pot, sb_stack, bb_stack, current_bet, 0, 3);
}

NodePtr buildTreeRecursive(const Board& board, Street street, Position to_act,
                           float pot, float sb_stack, float bb_stack, float current_bet,
                           int depth, int max_depth) {
    // Terminal conditions
    if (depth >= max_depth || street == Street::SHOWDOWN) {
        auto terminal = std::make_shared<GameTreeNode>(GameTreeNode::TERMINAL);
        terminal->board = board;
        terminal->street = street;
        terminal->player_to_act = to_act;
        terminal->pot_size = pot;
        terminal->stack_sb = sb_stack;
        terminal->stack_bb = bb_stack;
        terminal->current_bet = current_bet;
        return terminal;
    }
    
    auto node = std::make_shared<GameTreeNode>(GameTreeNode::DECISION);
    node->board = board;
    node->street = street;
    node->player_to_act = to_act;
    node->pot_size = pot;
    node->stack_sb = sb_stack;
    node->stack_bb = bb_stack;
    node->current_bet = current_bet;
    
    // Get available actions
    auto actions = node->getAvailableActions();
    
    // Create child nodes for each action
    for (const auto& action : actions) {
        NodePtr child;
        
        if (action.type == ActionType::FOLD) {
            // Terminal node for fold
            child = std::make_shared<GameTreeNode>(GameTreeNode::TERMINAL);
            child->board = board;
            child->street = street;
            child->pot_size = pot;
            child->stack_sb = sb_stack;
            child->stack_bb = bb_stack;
        } else {
            // Calculate new state
            float new_pot = pot;
            float new_sb_stack = sb_stack;
            float new_bb_stack = bb_stack;
            float new_bet = current_bet;
            Street new_street = street;
            
            if (action.type == ActionType::BET_20 || action.type == ActionType::BET_33 || 
                action.type == ActionType::BET_52 || action.type == ActionType::BET_100 ||
                action.type == ActionType::BET_123 || action.type == ActionType::RAISE || 
                action.type == ActionType::ALL_IN || action.type == ActionType::CALL) {
                new_pot += action.amount;
                if (to_act == Position::SB) {
                    new_sb_stack -= action.amount;
                } else {
                    new_bb_stack -= action.amount;
                }
                new_bet = action.amount;
            }
            
            // Switch player
            Position next_player = (to_act == Position::SB) ? Position::BB : Position::SB;
            
            // Check if both players acted - move to next street or showdown
            // Simplified: just continue on same street for now
            
            // Recursively build child
            child = buildTreeRecursive(board, new_street, next_player, new_pot, 
                                      new_sb_stack, new_bb_stack, new_bet, 
                                      depth + 1, max_depth);
        }
        
        node->children.push_back(child);
    }
    
    // Initialize strategy
    node->strategy = Strategy(actions.size());
    node->regrets.resize(actions.size(), 0.0f);
    node->avg_strategy.resize(actions.size(), 0.0f);
    
    return node;
}

std::vector<Action> GameTreeNode::getAvailableActions() const {
    std::vector<Action> actions;
    
    if (type != DECISION) return actions;
    
    float to_call = current_bet;
    float stack = (player_to_act == Position::SB) ? stack_sb : stack_bb;
    
    // Always can fold (except if checking is free)
    if (to_call > 0.0f) {
        actions.emplace_back(ActionType::FOLD, 0.0f);
    }
    
    // Can check if no bet to call
    if (to_call == 0.0f) {
        actions.emplace_back(ActionType::CHECK, 0.0f);
    } else {
        // Can call
        float call_amount = std::min(to_call, stack);
        actions.emplace_back(ActionType::CALL, call_amount);
    }
    
    // Can bet if no current bet
    if (to_call == 0.0f && street != Street::PREFLOP) {
        actions.emplace_back(ActionType::BET_20, calculateBetAmount(pot_size, BetSize::PERCENT_20));
        actions.emplace_back(ActionType::BET_33, calculateBetAmount(pot_size, BetSize::PERCENT_33));
        actions.emplace_back(ActionType::BET_52, calculateBetAmount(pot_size, BetSize::PERCENT_52));
        actions.emplace_back(ActionType::BET_100, calculateBetAmount(pot_size, BetSize::PERCENT_100));
        actions.emplace_back(ActionType::BET_123, calculateBetAmount(pot_size, BetSize::PERCENT_123));
    }
    
    // Can raise if there's a bet to call
    if (to_call > 0.0f) {
        float raise_amount = calculateRaiseAmount(to_call, pot_size);
        if (raise_amount <= stack) {
            actions.emplace_back(ActionType::RAISE, raise_amount);
        }
    }
    
    // Can go all-in
    actions.emplace_back(ActionType::ALL_IN, stack);
    
    return actions;
}

float GameTreeNode::calculateEV(const Hand& hand, const HandRange& opponent_range) const {
    // Simplified EV calculation
    // Full implementation would run simulations
    return 0.0f;
}

// GameTree implementation
GameTree::GameTree() : root_(nullptr), current_node_(nullptr) {}

void GameTree::build(const Board& board, Street street) {
    root_ = GameTreeNode::buildTree(board, street, Position::SB, 1.5f, 99.0f, 99.0f, 0.5f);
    current_node_ = root_;
    history_.clear();
    node_stack_.clear();
    node_stack_.push_back(root_);
}

void GameTree::applyAction(const Action& action) {
    if (!current_node_) return;
    
    history_.push_back(action);
    
    // Find or create child node
    // For now, create a simple continuation
    auto next = std::make_shared<GameTreeNode>(GameTreeNode::DECISION);
    next->board = current_node_->board;
    next->street = current_node_->street;
    next->player_to_act = (current_node_->player_to_act == Position::SB) ? Position::BB : Position::SB;
    next->pot_size = current_node_->pot_size + action.amount;
    next->current_bet = action.amount;
    
    if (current_node_->player_to_act == Position::SB) {
        next->stack_sb = current_node_->stack_sb - action.amount;
        next->stack_bb = current_node_->stack_bb;
    } else {
        next->stack_sb = current_node_->stack_sb;
        next->stack_bb = current_node_->stack_bb - action.amount;
    }
    
    current_node_->children.push_back(next);
    node_stack_.push_back(next);
    current_node_ = next;
}

void GameTree::undoLastAction() {
    if (history_.empty() || node_stack_.size() <= 1) return;
    
    history_.pop_back();
    node_stack_.pop_back();
    current_node_ = node_stack_.back();
}

} // namespace poker
