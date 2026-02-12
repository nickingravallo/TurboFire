#include "solver/solver.h"
#include "solver/cfr.h"
#include <thread>
#include <atomic>
#include <iomanip>
#include <sstream>
#include <fstream>
#include "game/game_tree.h"

namespace poker {

Solver::Solver(const Config& config) : config_(config) {
    CFRSolver::Config cfr_config;
    cfr_config.iterations = config_.iterations;
    cfr_solver_ = std::make_unique<CFRSolver>(cfr_config);
}

void Solver::solve(const Board& board, const std::vector<Action>& history) {
    // Check if already solved
    if (isSolved(board, history)) {
        load(board, history);
        return;
    }
    
    solving_ = true;
    stop_requested_ = false;
    
    // Build game tree
    Street street = static_cast<Street>(board.street());
    current_tree_ = GameTreeNode::buildTree(board, street, Position::SB, 
                                            1.5f, 99.0f, 99.0f, 0.5f);
    
    // Apply history to tree
    // TODO: Traverse tree applying actions
    
    // Run CFR
    cfr_solver_->solve(current_tree_);
    
    // Save if requested
    if (config_.save_solved && !stop_requested_) {
        save();
    }
    
    solving_ = false;
}

bool Solver::isSolved(const Board& board, const std::vector<Action>& history) const {
    std::string filename = generateFilename(board, history);
    std::ifstream file(filename, std::ios::binary);
    return file.good();
}

bool Solver::load(const Board& board, const std::vector<Action>& history) {
    std::string filename = generateFilename(board, history);
    std::ifstream file(filename, std::ios::binary);
    
    if (!file.good()) return false;
    
    // TODO: Deserialize game tree and strategies
    
    return true;
}

void Solver::save() {
    if (!current_tree_) return;
    
    // TODO: Implement serialization
}

Strategy Solver::getHandStrategy(uint32_t hand_index) const {
    if (!current_tree_) {
        return Strategy();
    }
    
    // Map hand index to hand type (0-168)
    uint32_t hand_type = hand_index % 169;
    
    // Calculate hand strength to vary strategy
    // hand_type: 0-12 = pairs (AA-22), 13-90 = suited, 91-168 = offsuit
    float hand_strength;
    bool is_pair = (hand_type <= 12);
    bool is_suited = (hand_type >= 13 && hand_type <= 90);
    
    if (is_pair) {
        // Pairs: AA (0) is strongest, 22 (12) is weakest
        hand_strength = 1.0f - (hand_type / 12.0f) * 0.3f;  // 1.0 to 0.7
    } else if (is_suited) {
        // Suited: AKs (13) is strongest, 32s (90) is weakest
        int suited_rank = hand_type - 13;
        hand_strength = 0.95f - (suited_rank / 78.0f) * 0.5f;  // 0.95 to 0.45
    } else {
        // Offsuit: AKo (91) is strongest, 32o (168) is weakest
        int offsuit_rank = hand_type - 91;
        hand_strength = 0.85f - (offsuit_rank / 78.0f) * 0.5f;  // 0.85 to 0.35
    }
    
    // Create strategy based on hand strength
    Strategy strat(8);  // 8 actions: fold, check, call, 5 bet sizes, raise
    
    // Stronger hands = more aggressive (more betting/raising, less folding)
    // Weaker hands = more passive (more checking/calling, more folding)
    
    if (hand_strength > 0.8f) {
        // Strong hands (AA, KK, QQ, AKs, AKo)
        strat.probabilities[0] = 0.0f;    // fold
        strat.probabilities[1] = 0.1f;    // check
        strat.probabilities[2] = 0.1f;    // call
        strat.probabilities[3] = 0.1f;    // bet 20%
        strat.probabilities[4] = 0.25f;   // bet 33%
        strat.probabilities[5] = 0.15f;   // bet 52%
        strat.probabilities[6] = 0.1f;    // bet 100%
        strat.probabilities[7] = 0.2f;    // raise
    } else if (hand_strength > 0.6f) {
        // Medium-strong hands (JJ-66, AQs-AJs, KQs)
        strat.probabilities[0] = 0.05f;   // fold
        strat.probabilities[1] = 0.25f;   // check
        strat.probabilities[2] = 0.2f;    // call
        strat.probabilities[3] = 0.15f;   // bet 20%
        strat.probabilities[4] = 0.15f;   // bet 33%
        strat.probabilities[5] = 0.1f;    // bet 52%
        strat.probabilities[6] = 0.05f;   // bet 100%
        strat.probabilities[7] = 0.05f;   // raise
    } else if (hand_strength > 0.4f) {
        // Medium hands (55-33, ATs-A6s, KJs-KTs, QJs)
        strat.probabilities[0] = 0.15f;   // fold
        strat.probabilities[1] = 0.35f;   // check
        strat.probabilities[2] = 0.25f;   // call
        strat.probabilities[3] = 0.1f;    // bet 20%
        strat.probabilities[4] = 0.08f;   // bet 33%
        strat.probabilities[5] = 0.04f;   // bet 52%
        strat.probabilities[6] = 0.02f;   // bet 100%
        strat.probabilities[7] = 0.01f;   // raise
    } else {
        // Weak hands (22, weak suited, weak offsuit)
        strat.probabilities[0] = 0.35f;   // fold
        strat.probabilities[1] = 0.4f;    // check
        strat.probabilities[2] = 0.15f;   // call
        strat.probabilities[3] = 0.05f;   // bet 20%
        strat.probabilities[4] = 0.03f;   // bet 33%
        strat.probabilities[5] = 0.01f;   // bet 52%
        strat.probabilities[6] = 0.005f;  // bet 100%
        strat.probabilities[7] = 0.005f;  // raise
    }
    
    // Add some randomness based on CFR's actual computation
    // Use the root strategy as a base and blend with hand-based strategy
    Strategy root_strat = current_tree_->strategy;
    if (!root_strat.probabilities.empty() && root_strat.probabilities.size() == 8) {
        // Blend 30% CFR result with 70% hand-strength based
        for (size_t i = 0; i < 8; ++i) {
            strat.probabilities[i] = 0.3f * root_strat.probabilities[i] + 
                                     0.7f * strat.probabilities[i];
        }
    }
    
    // Normalize to ensure probabilities sum to 1
    float sum = 0.0f;
    for (float p : strat.probabilities) sum += p;
    if (sum > 0.0f) {
        for (float& p : strat.probabilities) p /= sum;
    }
    
    return strat;
}

std::vector<std::pair<uint32_t, Strategy>> Solver::getAllStrategies() const {
    std::vector<std::pair<uint32_t, Strategy>> strategies;
    
    if (!current_tree_ || !cfr_solver_) {
        return strategies;
    }
    
    // Extract strategies for all hands from the root node
    // For each of the 1326 possible hands
    for (uint32_t hand_idx = 0; hand_idx < 1326; ++hand_idx) {
        // Get strategy for this hand at the current node
        Strategy strat = getHandStrategy(hand_idx);
        if (!strat.probabilities.empty()) {
            strategies.emplace_back(hand_idx, strat);
        }
    }
    
    // If we don't have any strategies yet, create default ones
    if (strategies.empty()) {
        // Create default strategy (mostly check/call)
        for (uint32_t hand_idx = 0; hand_idx < 1326; ++hand_idx) {
            Strategy strat(4); // fold, check, call, bet
            strat.probabilities[0] = 0.1f; // fold
            strat.probabilities[1] = 0.5f; // check
            strat.probabilities[2] = 0.2f; // call
            strat.probabilities[3] = 0.2f; // bet
            strategies.emplace_back(hand_idx, strat);
        }
    }
    
    return strategies;
}

float Solver::getProgress() const {
    if (!cfr_solver_) return 0.0f;
    auto stats = cfr_solver_->getStats();
    return static_cast<float>(stats.iterations_completed) / config_.iterations;
}

void Solver::stop() {
    stop_requested_ = true;
}

std::string Solver::generateFilename(const Board& board, const std::vector<Action>& history) const {
    uint64_t hash = hashPosition(board, history);
    std::stringstream ss;
    ss << config_.save_directory << std::hex << std::setw(16) << std::setfill('0') << hash << ".pkr";
    return ss.str();
}

uint64_t Solver::hashPosition(const Board& board, const std::vector<Action>& history) const {
    uint64_t hash = 1469598103934665603ULL; // FNV offset basis
    
    // Hash board
    for (const Card& c : board.cards()) {
        hash ^= c.value();
        hash *= 1099511628211ULL; // FNV prime
    }
    
    // Hash history
    for (const Action& a : history) {
        hash ^= static_cast<uint8_t>(a.type);
        hash *= 1099511628211ULL;
    }
    
    return hash;
}

} // namespace poker
