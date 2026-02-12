#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include "../card/card.h"
#include "../game/board.h"

namespace poker {

// Forward declarations
struct Action;

// Game states with strict poker flow
enum class GameState {
    PREFLOP_SB_ACTS,        // SB to act first
    PREFLOP_BB_RESPONDS,    // BB responds to SB action
    FLOP_SB_ACTS,          // SB acts first on flop
    FLOP_BB_RESPONDS,      // BB responds on flop
    TURN_SB_ACTS,          // SB on turn
    TURN_BB_RESPONDS,      // BB on turn
    RIVER_SB_ACTS,         // SB on river
    RIVER_BB_RESPONDS,     // BB on river
    SHOWDOWN,              // All streets complete, ready to solve
    SOLVING,               // Solver running
    SOLVED                 // Solution available
};

enum class Position {
    SB = 0,  // Small Blind (OOP)
    BB = 1   // Big Blind (IP)
};

enum class ActionType {
    FOLD = 0,
    CHECK = 1,
    CALL = 2,
    BET_20 = 3,
    BET_33 = 4,
    BET_52 = 5,
    BET_100 = 6,
    BET_123 = 7,
    RAISE = 8,
    ALL_IN = 9
};

struct Action {
    ActionType type;
    float amount;  // In big blinds
    
    Action() : type(ActionType::FOLD), amount(0.0f) {}
    Action(ActionType t, float a) : type(t), amount(a) {}
    
    bool isAggressive() const {
        return type == ActionType::BET_20 || type == ActionType::BET_33 ||
               type == ActionType::BET_52 || type == ActionType::BET_100 ||
               type == ActionType::BET_123 || type == ActionType::RAISE ||
               type == ActionType::ALL_IN;
    }
    
    std::string toString() const;
};

// Game state manager with strict poker rules
class GameStateManager {
public:
    GameStateManager();
    
    // State queries
    GameState getState() const { return state_; }
    Position getPlayerToAct() const;
    float getPotSize() const { return pot_size_; }
    float getSBStack() const { return sb_stack_; }
    float getBBStack() const { return bb_stack_; }
    float getCurrentBet() const { return current_bet_; }
    float getAmountToCall() const { return amount_to_call_; }
    const Board& getBoard() const { return board_; }
    const std::vector<Action>& getHistory() const { return history_; }
    
    // State transitions
    void setFlop(const std::string& cards);
    void setTurn(const std::string& card);
    void setRiver(const std::string& card);
    bool applyAction(const Action& action);
    bool undoLastAction();
    
    // Validation
    bool isActionValid(ActionType action) const;
    std::vector<ActionType> getValidActions() const;
    
    // Solver integration
    void startSolving();
    void finishSolving();
    bool isSolving() const { return state_ == GameState::SOLVING; }
    bool isSolved() const { return state_ == GameState::SOLVED; }
    
    // Callbacks
    void onStateChange(std::function<void(GameState)> callback) { state_callback_ = callback; }
    void onActionApplied(std::function<void(const Action&)> callback) { action_callback_ = callback; }
    
private:
    GameState state_;
    Board board_;
    std::vector<Action> history_;
    
    // Game state
    float pot_size_;
    float sb_stack_;
    float bb_stack_;
    float current_bet_;      // Current bet that needs to be called
    float amount_to_call_;   // Amount for BB to call SB's bet
    Position last_aggressor_;
    
    // Undo stack
    struct GameSnapshot {
        GameState state;
        float pot_size;
        float sb_stack;
        float bb_stack;
        float current_bet;
        float amount_to_call;
        Position last_aggressor;
        Board board;
    };
    std::vector<GameSnapshot> undo_stack_;
    
    // Callbacks
    std::function<void(GameState)> state_callback_;
    std::function<void(const Action&)> action_callback_;
    
    // Internal helpers
    void saveSnapshot();
    void restoreSnapshot(const GameSnapshot& snap);
    void transitionToNextState(const Action& action);
    void moveToNextStreet();
    Position getOtherPlayer(Position p) const {
        return (p == Position::SB) ? Position::BB : Position::SB;
    }
    float getBetSize(ActionType bet_type) const;
};

} // namespace poker
