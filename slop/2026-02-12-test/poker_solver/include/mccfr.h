/**
 * mccfr.h - Monte Carlo Counterfactual Regret Minimization
 * 
 * This module implements External Sampling MCCFR, which is:
 * - Memory efficient (doesn't store full game tree)
 * - Computationally efficient (samples opponent actions)
 * - Converges to Nash equilibrium
 * 
 * MCCFR Algorithm Overview:
 * ========================
 * 
 * 1. For N iterations:
 *    a. Sample a random poker hand (deal cards)
 *    b. Traverse game tree using CFR:
 *       - At opponent nodes: sample ONE action
 *       - At player nodes: compute strategy from regrets
 *       - Update regrets based on counterfactual values
 * 
 * 2. Regret Matching:
 *    - Strategy(action) = max(0, regret[action]) / sum(max(0, regret[all]))
 *    - Actions with negative regret get 0 probability
 * 
 * 3. External Sampling:
 *    - For efficiency, we sample opponent actions rather than exploring all
 *    - This reduces computation per iteration significantly
 * 
 * 4. Convergence:
 *    - Average strategy converges to Nash equilibrium
 *    - Regrets reflect "how much better" each action is
 */

#ifndef MCCFR_H
#define MCCFR_H

#include <stdint.h>
#include <stdbool.h>
#include "game_state.h"
#include "strategy.h"

/* 
 * MCCFR Training configuration
 */
typedef struct {
    int num_iterations;          /* Total iterations to run (1M for learning) */
    int checkpoint_interval;     /* Save checkpoint every N iterations (100k) */
    int print_interval;          /* Print progress every N iterations (1k) */
    double epsilon;              /* Exploration parameter (0.0 = no exploration) */
    bool use_pruning;            /* Enable regret-based pruning */
} MCCFRConfig;

/* Default configuration */
#define DEFAULT_MCCFR_CONFIG { \
    .num_iterations = 1000000, \
    .checkpoint_interval = 100000, \
    .print_interval = 1000, \
    .epsilon = 0.0, \
    .use_pruning = true \
}

/*
 * MCCFR state
 */
typedef struct {
    MCCFRConfig config;
    StrategyTable* regret_table;      /* Regret sums per info set */
    StrategyTable* strategy_table;    /* Average strategy per info set */
    int current_iteration;
    double total_regret;              /* For convergence tracking */
    int hands_played;
    int hands_won_ip;
    int hands_won_oop;
    int hands_tied;
} MCCFRState;

/*
 * Initialize and cleanup
 */

/* Create MCCFR state */
MCCFRState* mccfr_create(const MCCFRConfig* config);

/* Free MCCFR state */
void mccfr_free(MCCFRState* state);

/*
 * Training
 */

/* Run one iteration of MCCFR (sample one hand and traverse) */
void mccfr_iteration(MCCFRState* state);

/* Run full training loop */
void mccfr_train(MCCFRState* state);

/*
 * CFR Traversal (core algorithm)
 */

/* 
 * CFR traversal function
 * Returns expected value for the traversing player
 * 
 * This is the heart of MCCFR. For each node:
 * - If terminal: return payoff
 * - If chance node: sample and recurse
 * - If player node: 
 *   * Get strategy from regrets
 *   * For each action: compute value
 *   * Update regrets
 *   * Return expected value
 * - If opponent node:
 *   * Sample ONE action
 *   * Return value of that action
 */
double mccfr_traverse(MCCFRState* state, 
                      GameState* game,
                      Player traversing_player,
                      double reach_prob);

/*
 * Utility functions
 */

/* Get strategy for an info set */
void mccfr_get_strategy(MCCFRState* state, 
                        uint64_t info_set_key,
                        const Action* actions,
                        int num_actions,
                        double* strategy);

/* Update regrets for an info set */
void mccfr_update_regrets(MCCFRState* state,
                          uint64_t info_set_key,
                          const Action* actions,
                          int num_actions,
                          const double* action_values,
                          double node_value);

/* Update average strategy */
void mccfr_update_strategy(MCCFRState* state,
                           uint64_t info_set_key,
                           const Action* actions,
                           int num_actions,
                           const double* strategy,
                           double reach_prob);

/*
 * Checkpointing
 */

/* Save current state to disk */
int mccfr_save_checkpoint(MCCFRState* state, const char* filename);

/* Load state from disk */
int mccfr_load_checkpoint(MCCFRState* state, const char* filename);

/*
 * Statistics and output
 */

/* Print current statistics */
void mccfr_print_stats(const MCCFRState* state);

/* Get exploitability estimate (approximate) */
double mccfr_estimate_exploitability(const MCCFRState* state, int num_samples);

/* Export strategy to file */
void mccfr_export_strategy(MCCFRState* state, const char* filename);

#endif /* MCCFR_H */
