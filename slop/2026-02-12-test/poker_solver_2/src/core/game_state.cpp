#include "core/game_state.h"
#include <algorithm>
#include <sstream>

namespace poker {

std::string Action::toString() const {
    switch (type) {
        case ActionType::FOLD: return "Fold";
        case ActionType::CHECK: return "Check";
        case ActionType::CALL: return "Call " + std::to_string(static_cast<int>(amount)) + "bb";
        case ActionType::BET_20: return "Bet 20%";
        case ActionType::BET_33: return "Bet 33%";
        case ActionType::BET_52: return "Bet 52%";
        case ActionType::BET_100: return "Bet 100%";
        case ActionType::BET_123: return "Bet 123%";
        case ActionType::RAISE: return "Raise 2.5x";
        case ActionType::ALL_IN: return "All-in " + std::to_string(static_cast<int>(amount)) + "bb";
        default: return "Unknown";
    }
}

GameStateManager::GameStateManager()
    : state_(GameState::PREFLOP_SB_ACTS)
    , pot_size_(1.5f)  // SB 0.5 + BB 1.0
    , sb_stack_(99.5f)  // Started with 100, posted 0.5
    , bb_stack_(99.0f)  // Started with 100, posted 1.0
    , current_bet_(0.5f)  // SB's initial blind
    , amount_to_call_(0.5f)  // BB needs to call 0.5 more
    , last_aggressor_(Position::SB) {
}

Position GameStateManager::getPlayerToAct() const {
    switch (state_) {
        case GameState::PREFLOP_SB_ACTS:
        case GameState::FLOP_SB_ACTS:
        case GameState::TURN_SB_ACTS:
        case GameState::RIVER_SB_ACTS:
            return Position::SB;
        case GameState::PREFLOP_BB_RESPONDS:
        case GameState::FLOP_BB_RESPONDS:
        case GameState::TURN_BB_RESPONDS:
        case GameState::RIVER_BB_RESPONDS:
            return Position::BB;
        default:
            return Position::SB;  // Default
    }
}

void GameStateManager::setFlop(const std::string& cards) {
    if (state_ != GameState::PREFLOP_SB_ACTS && 
        state_ != GameState::PREFLOP_BB_RESPONDS) {
        return;  // Can only set flop at preflop
    }
    
    board_.clear();
    for (size_t i = 0; i < cards.length(); i += 2) {
        if (i + 1 < cards.length()) {
            board_.addCard(Card::fromString(cards.substr(i, 2)));
        }
    }
    
    // Reset for flop action
    state_ = GameState::FLOP_SB_ACTS;
    current_bet_ = 0.0f;
    amount_to_call_ = 0.0f;
    last_aggressor_ = Position::SB;
    
    saveSnapshot();
    if (state_callback_) state_callback_(state_);
}

void GameStateManager::setTurn(const std::string& card) {
    if (board_.size() != 3) return;
    
    board_.addCard(Card::fromString(card));
    state_ = GameState::TURN_SB_ACTS;
    current_bet_ = 0.0f;
    amount_to_call_ = 0.0f;
    last_aggressor_ = Position::SB;
    
    saveSnapshot();
    if (state_callback_) state_callback_(state_);
}

void GameStateManager::setRiver(const std::string& card) {
    if (board_.size() != 4) return;
    
    board_.addCard(Card::fromString(card));
    state_ = GameState::RIVER_SB_ACTS;
    current_bet_ = 0.0f;
    amount_to_call_ = 0.0f;
    last_aggressor_ = Position::SB;
    
    saveSnapshot();
    if (state_callback_) state_callback_(state_);
}

bool GameStateManager::isActionValid(ActionType action) const {
    // Can't act if not in an acting state
    if (state_ != GameState::PREFLOP_SB_ACTS &&
        state_ != GameState::PREFLOP_BB_RESPONDS &&
        state_ != GameState::FLOP_SB_ACTS &&
        state_ != GameState::FLOP_BB_RESPONDS &&
        state_ != GameState::TURN_SB_ACTS &&
        state_ != GameState::TURN_BB_RESPONDS &&
        state_ != GameState::RIVER_SB_ACTS &&
        state_ != GameState::RIVER_BB_RESPONDS) {
        return false;
    }
    
    bool is_sb_turn = (state_ == GameState::PREFLOP_SB_ACTS ||
                       state_ == GameState::FLOP_SB_ACTS ||
                       state_ == GameState::TURN_SB_ACTS ||
                       state_ == GameState::RIVER_SB_ACTS);
    
    float to_call = amount_to_call_;
    
    switch (action) {
        case ActionType::FOLD:
            // Can only fold if there's a bet to call
            return to_call > 0.0f;
            
        case ActionType::CHECK:
            // Can only check if no bet to call
            return to_call == 0.0f;
            
        case ActionType::CALL:
            // Can only call if there's a bet
            return to_call > 0.0f;
            
        case ActionType::BET_20:
        case ActionType::BET_33:
        case ActionType::BET_52:
        case ActionType::BET_100:
        case ActionType::BET_123:
            // Can only bet if no current bet AND not BB responding
            return to_call == 0.0f && 
                   (state_ == GameState::PREFLOP_SB_ACTS ||
                    state_ == GameState::FLOP_SB_ACTS ||
                    state_ == GameState::TURN_SB_ACTS ||
                    state_ == GameState::RIVER_SB_ACTS);
            
        case ActionType::RAISE:
            // Can only raise if there's a bet to call AND not SB opening
            return to_call > 0.0f &&
                   (state_ == GameState::PREFLOP_BB_RESPONDS ||
                    state_ == GameState::FLOP_BB_RESPONDS ||
                    state_ == GameState::TURN_BB_RESPONDS ||
                    state_ == GameState::RIVER_BB_RESPONDS);
            
        case ActionType::ALL_IN:
            // Always available
            return true;
            
        default:
            return false;
    }
}

std::vector<ActionType> GameStateManager::getValidActions() const {
    std::vector<ActionType> actions;
    
    for (int i = 0; i <= static_cast<int>(ActionType::ALL_IN); ++i) {
        ActionType action = static_cast<ActionType>(i);
        if (isActionValid(action)) {
            actions.push_back(action);
        }
    }
    
    return actions;
}

float GameStateManager::getBetSize(ActionType bet_type) const {
    switch (bet_type) {
        case ActionType::BET_20: return pot_size_ * 0.20f;
        case ActionType::BET_33: return pot_size_ * 0.33f;
        case ActionType::BET_52: return pot_size_ * 0.52f;
        case ActionType::BET_100: return pot_size_ * 1.00f;
        case ActionType::BET_123: return pot_size_ * 1.23f;
        case ActionType::RAISE: return current_bet_ * 2.5f;
        case ActionType::ALL_IN: 
            return (getPlayerToAct() == Position::SB) ? sb_stack_ : bb_stack_;
        default: return 0.0f;
    }
}

bool GameStateManager::applyAction(const Action& action) {
    if (!isActionValid(action.type)) {
        return false;
    }
    
    saveSnapshot();
    
    Position player = getPlayerToAct();
    float amount = 0.0f;
    
    // Calculate actual amount
    switch (action.type) {
        case ActionType::CALL:
            amount = amount_to_call_;
            break;
        case ActionType::BET_20:
        case ActionType::BET_33:
        case ActionType::BET_52:
        case ActionType::BET_100:
        case ActionType::BET_123:
        case ActionType::RAISE:
        case ActionType::ALL_IN:
            amount = getBetSize(action.type);
            break;
        default:
            amount = 0.0f;
    }
    
    // Update stacks and pot
    if (player == Position::SB) {
        sb_stack_ -= amount;
    } else {
        bb_stack_ -= amount;
    }
    pot_size_ += amount;
    
    // Update state based on action
    history_.push_back(Action(action.type, amount));
    if (action_callback_) action_callback_(history_.back());
    
    transitionToNextState(action);
    
    return true;
}

void GameStateManager::transitionToNextState(const Action& action) {
    switch (state_) {
        case GameState::PREFLOP_SB_ACTS:
            if (action.type == ActionType::CHECK) {
                // SB checked, BB to act
                state_ = GameState::PREFLOP_BB_RESPONDS;
                current_bet_ = 0.0f;
                amount_to_call_ = 0.0f;
            } else if (action.isAggressive()) {
                // SB bet/raised
                state_ = GameState::PREFLOP_BB_RESPONDS;
                current_bet_ = action.amount;
                amount_to_call_ = action.amount;
                last_aggressor_ = Position::SB;
            }
            break;
            
        case GameState::PREFLOP_BB_RESPONDS:
            if (action.type == ActionType::FOLD) {
                // BB folded, SB wins
                sb_stack_ += pot_size_;
                pot_size_ = 0.0f;
                state_ = GameState::SHOWDOWN;
            } else if (action.type == ActionType::CALL) {
                // BB called, move to flop
                moveToNextStreet();
            } else if (action.isAggressive()) {
                // BB raised, back to SB
                state_ = GameState::PREFLOP_SB_ACTS;
                current_bet_ = action.amount;
                amount_to_call_ = action.amount - amount_to_call_;  // Amount to call the raise
                last_aggressor_ = Position::BB;
            }
            break;
            
        case GameState::FLOP_SB_ACTS:
            if (action.type == ActionType::CHECK) {
                state_ = GameState::FLOP_BB_RESPONDS;
            } else if (action.isAggressive()) {
                state_ = GameState::FLOP_BB_RESPONDS;
                current_bet_ = action.amount;
                amount_to_call_ = action.amount;
                last_aggressor_ = Position::SB;
            }
            break;
            
        case GameState::FLOP_BB_RESPONDS:
            if (action.type == ActionType::FOLD) {
                sb_stack_ += pot_size_;
                pot_size_ = 0.0f;
                state_ = GameState::SHOWDOWN;
            } else if (action.type == ActionType::CALL) {
                moveToNextStreet();
            } else if (action.isAggressive()) {
                state_ = GameState::FLOP_SB_ACTS;
                current_bet_ = action.amount;
                amount_to_call_ = action.amount - amount_to_call_;
                last_aggressor_ = Position::BB;
            }
            break;
            
        case GameState::TURN_SB_ACTS:
            if (action.type == ActionType::CHECK) {
                state_ = GameState::TURN_BB_RESPONDS;
            } else if (action.isAggressive()) {
                state_ = GameState::TURN_BB_RESPONDS;
                current_bet_ = action.amount;
                amount_to_call_ = action.amount;
                last_aggressor_ = Position::SB;
            }
            break;
            
        case GameState::TURN_BB_RESPONDS:
            if (action.type == ActionType::FOLD) {
                sb_stack_ += pot_size_;
                pot_size_ = 0.0f;
                state_ = GameState::SHOWDOWN;
            } else if (action.type == ActionType::CALL) {
                moveToNextStreet();
            } else if (action.isAggressive()) {
                state_ = GameState::TURN_SB_ACTS;
                current_bet_ = action.amount;
                amount_to_call_ = action.amount - amount_to_call_;
                last_aggressor_ = Position::BB;
            }
            break;
            
        case GameState::RIVER_SB_ACTS:
            if (action.type == ActionType::CHECK) {
                state_ = GameState::RIVER_BB_RESPONDS;
            } else if (action.isAggressive()) {
                state_ = GameState::RIVER_BB_RESPONDS;
                current_bet_ = action.amount;
                amount_to_call_ = action.amount;
                last_aggressor_ = Position::SB;
            }
            break;
            
        case GameState::RIVER_BB_RESPONDS:
            if (action.type == ActionType::FOLD) {
                sb_stack_ += pot_size_;
                pot_size_ = 0.0f;
                state_ = GameState::SHOWDOWN;
            } else if (action.type == ActionType::CALL || action.type == ActionType::CHECK) {
                state_ = GameState::SHOWDOWN;
            } else if (action.isAggressive()) {
                // BB raised on river, SB must respond
                state_ = GameState::RIVER_SB_ACTS;
                current_bet_ = action.amount;
                amount_to_call_ = action.amount - amount_to_call_;
                last_aggressor_ = Position::BB;
            }
            break;
            
        default:
            break;
    }
    
    if (state_callback_) state_callback_(state_);
}

void GameStateManager::moveToNextStreet() {
    current_bet_ = 0.0f;
    amount_to_call_ = 0.0f;
    
    switch (state_) {
        case GameState::PREFLOP_BB_RESPONDS:
            // Need flop input
            state_ = GameState::FLOP_SB_ACTS;
            break;
        case GameState::FLOP_BB_RESPONDS:
            // Need turn input
            state_ = GameState::TURN_SB_ACTS;
            break;
        case GameState::TURN_BB_RESPONDS:
            // Need river input
            state_ = GameState::RIVER_SB_ACTS;
            break;
        default:
            state_ = GameState::SHOWDOWN;
            break;
    }
}

void GameStateManager::saveSnapshot() {
    GameSnapshot snap;
    snap.state = state_;
    snap.pot_size = pot_size_;
    snap.sb_stack = sb_stack_;
    snap.bb_stack = bb_stack_;
    snap.current_bet = current_bet_;
    snap.amount_to_call = amount_to_call_;
    snap.last_aggressor = last_aggressor_;
    snap.board = board_;
    undo_stack_.push_back(snap);
}

bool GameStateManager::undoLastAction() {
    if (undo_stack_.empty()) {
        return false;
    }
    
    // Remove the current state (it's after the action was applied)
    if (!undo_stack_.empty()) {
        undo_stack_.pop_back();
    }
    
    // Restore previous state
    if (!undo_stack_.empty()) {
        restoreSnapshot(undo_stack_.back());
        
        // Also remove the restored state from stack
        undo_stack_.pop_back();
        
        // Remove the action from history
        if (!history_.empty()) {
            history_.pop_back();
        }
        
        return true;
    }
    
    return false;
}

void GameStateManager::restoreSnapshot(const GameSnapshot& snap) {
    state_ = snap.state;
    pot_size_ = snap.pot_size;
    sb_stack_ = snap.sb_stack;
    bb_stack_ = snap.bb_stack;
    current_bet_ = snap.current_bet;
    amount_to_call_ = snap.amount_to_call;
    last_aggressor_ = snap.last_aggressor;
    board_ = snap.board;
    
    if (state_callback_) state_callback_(state_);
}

void GameStateManager::startSolving() {
    state_ = GameState::SOLVING;
    if (state_callback_) state_callback_(state_);
}

void GameStateManager::finishSolving() {
    state_ = GameState::SOLVED;
    if (state_callback_) state_callback_(state_);
}

} // namespace poker
