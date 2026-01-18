/*
 * MCCFR.h - Monte Carlo Counterfactual Regret Minimization for Poker GTO Solver
 * 
 * Implements MCCFR algorithm for solving heads-up poker post-flop scenarios
 */

#ifndef MCCFR_H
#define MCCFR_H

#include <stdint.h>
#include "HandRanks.h"

#define MAX_ACTIONS 3  // Check/Call, Bet/Raise, Fold
#define MAX_ITERATIONS 1000000
#define DEFAULT_ITERATIONS 100000

// Action types
typedef enum {
    ACTION_CHECK_CALL = 0,
    ACTION_BET_RAISE = 1,
    ACTION_FOLD = 2
} ActionType;

// Street types
typedef enum {
    STREET_FLOP = 0,
    STREET_TURN = 1,
    STREET_RIVER = 2
} Street;

// Information set key (based on board cards and action history)
typedef struct {
    int board_cards[5];  // Board cards encoded as 0-51, -1 if not dealt
    int action_history[10];  // Action sequence (max 10 actions)
    int num_actions;
    Street street;
    int player;  // Current player (0 or 1)
} InfoSet;

// Strategy and regret storage
typedef struct {
    double regrets[MAX_ACTIONS];
    double strategy[MAX_ACTIONS];
    double strategy_sum[MAX_ACTIONS];
    uint64_t visits;
} InfoSetData;

// Hash table entry
typedef struct {
    uint64_t key_hash;
    InfoSet iset;
    InfoSetData data;
} HashEntry;

// MCCFR solver context
typedef struct {
    HashEntry *hash_table;  // Per-solver hash table
    int hash_size;
    int hash_capacity;
    HandRankTables *hand_ranks;
    int p0_hand[2];  // Player 0 hole cards (0-51)
    int p1_hand[2];  // Player 1 hole cards (0-51)
    int board[5];    // Board cards (0-51, -1 if not dealt)
    Street current_street;
    double pot_size;
    double bet_size;
} MCCFRSolver;

// Function declarations
MCCFRSolver* mccfr_create(int p0_c0, int p0_c1, int p1_c0, int p1_c1, HandRankTables *hr);
void mccfr_free(MCCFRSolver *solver);
void mccfr_set_board(MCCFRSolver *solver, int *board_cards, Street street);
uint64_t mccfr_hash_infoset(const InfoSet *iset);
InfoSetData* mccfr_get_or_create(MCCFRSolver *solver, const InfoSet *iset);
double mccfr_cfr(MCCFRSolver *solver, InfoSet *iset, double reach_p0, double reach_p1, int depth);
void mccfr_update_strategy(MCCFRSolver *solver);
void mccfr_solve(MCCFRSolver *solver, int iterations);
void mccfr_print_strategy(MCCFRSolver *solver, Street street);
double mccfr_evaluate_hand(MCCFRSolver *solver, int player, int *board, int board_size);

#endif /* MCCFR_H */
