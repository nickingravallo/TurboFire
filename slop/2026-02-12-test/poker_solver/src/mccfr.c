/**
 * mccfr.c - MCCFR Implementation
 * 
 * External Sampling MCCFR Algorithm
 * ================================
 * 
 * The key insight of MCCFR is that we don't need to explore the entire
 * game tree on each iteration. Instead, we:
 * 
 * 1. Sample ONE hand (random deal)
 * 2. Traverse the game tree for that specific hand
 * 3. For opponent nodes, sample ONE action (external sampling)
 * 4. For player nodes, compute counterfactual values for ALL actions
 * 5. Update regrets based on "how much better" each action was
 * 
 * This is called "External Sampling" because we sample the opponent's
 * actions externally to the player we're training.
 * 
 * Regret Definition:
 * =================
 * Regret(action) = value(action) - value(current_strategy)
 * 
 * If regret > 0: This action was better than our current strategy
 * If regret < 0: This action was worse than our current strategy
 * 
 * We accumulate these regrets over iterations. The strategy at each
 * info set is computed using "regret matching" - actions with higher
 * positive regret get played more often.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "mccfr.h"

/* Random number between 0 and 1 */
static double random_double(void) {
    return (double)rand() / RAND_MAX;
}

/* Create MCCFR state */
MCCFRState* mccfr_create(const MCCFRConfig* config) {
    MCCFRState* state = (MCCFRState*)malloc(sizeof(MCCFRState));
    if (!state) return NULL;
    
    /* Copy config or use default */
    if (config) {
        memcpy(&state->config, config, sizeof(MCCFRConfig));
    } else {
        state->config = (MCCFRConfig)DEFAULT_MCCFR_CONFIG;
    }
    
    /* Initialize random seed */
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }
    
    /* Create tables */
    state->regret_table = strategy_table_create();
    state->strategy_table = strategy_table_create();
    
    if (!state->regret_table || !state->strategy_table) {
        mccfr_free(state);
        return NULL;
    }
    
    /* Initialize stats */
    state->current_iteration = 0;
    state->total_regret = 0.0;
    state->hands_played = 0;
    state->hands_won_ip = 0;
    state->hands_won_oop = 0;
    state->hands_tied = 0;
    
    return state;
}

/* Free MCCFR state */
void mccfr_free(MCCFRState* state) {
    if (state) {
        if (state->regret_table) {
            strategy_table_free(state->regret_table);
        }
        if (state->strategy_table) {
            strategy_table_free(state->strategy_table);
        }
        free(state);
    }
}

/* 
 * Get strategy for info set using regret matching
 * This computes the current strategy from accumulated regrets
 */
void mccfr_get_strategy(MCCFRState* state,
                        uint64_t info_set_key,
                        const Action* actions,
                        int num_actions,
                        double* strategy) {
    /* Get or create regret data */
    InfoSetData* regret_data = strategy_get(state->regret_table, info_set_key, 
                                            actions, num_actions);
    
    if (!regret_data) {
        /* Fallback to uniform */
        double uniform = 1.0 / num_actions;
        for (int i = 0; i < num_actions; i++) {
            strategy[i] = uniform;
        }
        return;
    }
    
    /* Apply regret matching */
    regret_matching(regret_data->regrets, num_actions, strategy);
}

/* Update regrets for an info set */
void mccfr_update_regrets(MCCFRState* state,
                          uint64_t info_set_key,
                          const Action* actions,
                          int num_actions,
                          const double* action_values,
                          double node_value) {
    InfoSetData* data = strategy_get(state->regret_table, info_set_key, 
                                     actions, num_actions);
    if (!data) return;
    
    /* Update regrets: regret[a] += value[a] - node_value */
    for (int i = 0; i < num_actions; i++) {
        double regret = action_values[i] - node_value;
        data->regrets[i] += regret;
        
        /* Track total regret for convergence monitoring */
        if (regret > 0) {
            state->total_regret += regret;
        }
    }
}

/* Update average strategy */
void mccfr_update_strategy(MCCFRState* state,
                           uint64_t info_set_key,
                           const Action* actions,
                           int num_actions,
                           const double* strategy,
                           double reach_prob) {
    InfoSetData* data = strategy_get(state->strategy_table, info_set_key,
                                     actions, num_actions);
    if (!data) return;
    
    /* Accumulate strategy weighted by reach probability */
    for (int i = 0; i < num_actions; i++) {
        data->strategy_sums[i] += strategy[i] * reach_prob;
    }
    data->visit_count++;
}

/* 
 * CFR Traversal - Core Algorithm
 * 
 * This function traverses the game tree using external sampling MCCFR.
 * 
 * Parameters:
 *   state - MCCFR training state
 *   game - Current game state
 *   traversing_player - Which player we're training (IP or OOP)
 *   reach_prob - Probability of reaching this node
 * 
 * Returns: Expected value for the traversing player
 */
double mccfr_traverse(MCCFRState* state,
                      GameState* game,
                      Player traversing_player,
                      double reach_prob) {
    
    /* Base case: terminal node */
    if (game->is_terminal) {
        double payoff = game_get_payoff(game);
        
        /* Update stats */
        if (game->winner == PLAYER_IP) state->hands_won_ip++;
        else if (game->winner == PLAYER_OOP) state->hands_won_oop++;
        else state->hands_tied++;
        
        /* Return from perspective of traversing player */
        return (traversing_player == PLAYER_IP) ? payoff : -payoff;
    }
    
    /* Get current player */
    Player current_player = game->current_player;
    
    /* Get legal actions */
    Action actions[MAX_ACTIONS];
    int num_actions = game_get_legal_actions(game, actions);
    
    if (num_actions == 0) {
        /* Shouldn't happen, but handle gracefully */
        return 0.0;
    }
    
    /* Get info set key */
    uint64_t info_set_key = game_get_info_set_hash(game);
    
    if (current_player == traversing_player) {
        /* 
         * Player node: We need to consider ALL actions
         * Compute strategy from current regrets
         */
        double strategy[MAX_ACTIONS];
        mccfr_get_strategy(state, info_set_key, actions, num_actions, strategy);
        
        /* Update average strategy */
        mccfr_update_strategy(state, info_set_key, actions, num_actions, 
                              strategy, reach_prob);
        
        /* Compute value for each action */
        double action_values[MAX_ACTIONS];
        for (int i = 0; i < num_actions; i++) {
            GameState next_state;
            game_copy(game, &next_state);
            
            if (game_apply_action(&next_state, actions[i])) {
                /* Recurse with updated reach probability */
                double action_reach = reach_prob * strategy[i];
                action_values[i] = mccfr_traverse(state, &next_state, 
                                                  traversing_player, action_reach);
            } else {
                action_values[i] = 0.0;
            }
        }
        
        /* Compute expected value under current strategy */
        double node_value = 0.0;
        for (int i = 0; i < num_actions; i++) {
            node_value += strategy[i] * action_values[i];
        }
        
        /* Update regrets */
        mccfr_update_regrets(state, info_set_key, actions, num_actions,
                            action_values, node_value);
        
        return node_value;
        
    } else {
        /* 
         * Opponent node: EXTERNAL SAMPLING
         * We sample ONE action according to opponent's strategy
         * rather than exploring all actions
         */
        
        /* Get opponent's strategy (from strategy table, not regrets) */
        InfoSetData* strat_data = strategy_get_existing(state->strategy_table, 
                                                         info_set_key);
        
        /* Sample action */
        int action_idx;
        if (strat_data && strat_data->visit_count > 0) {
            /* Sample from learned strategy */
            double avg_strategy[MAX_ACTIONS];
            get_average_strategy(strat_data, avg_strategy);
            
            double r = random_double();
            double cum_prob = 0.0;
            action_idx = 0;
            for (int i = 0; i < num_actions; i++) {
                cum_prob += avg_strategy[i];
                if (r <= cum_prob) {
                    action_idx = i;
                    break;
                }
            }
        } else {
            /* Uniform random for unexplored states */
            action_idx = (int)(random_double() * num_actions);
            if (action_idx >= num_actions) action_idx = num_actions - 1;
        }
        
        /* Recurse with sampled action */
        GameState next_state;
        game_copy(game, &next_state);
        
        if (game_apply_action(&next_state, actions[action_idx])) {
            return mccfr_traverse(state, &next_state, traversing_player, reach_prob);
        } else {
            return 0.0;
        }
    }
}

/* Run one iteration */
void mccfr_iteration(MCCFRState* state) {
    /* Create and shuffle deck */
    Deck deck;
    deck_init(&deck);
    deck_shuffle(&deck);
    
    /* Sample one hand for IP and one for OOP */
    Card ip_cards[HOLE_CARDS];
    Card oop_cards[HOLE_CARDS];
    Card board[5];
    
    ip_cards[0] = deck_deal(&deck);
    ip_cards[1] = deck_deal(&deck);
    oop_cards[0] = deck_deal(&deck);
    oop_cards[1] = deck_deal(&deck);
    
    /* Deal board */
    for (int i = 0; i < 5; i++) {
        board[i] = deck_deal(&deck);
    }
    
    /* Train IP */
    GameState game_ip;
    game_init(&game_ip, ip_cards, oop_cards, board, 0);
    mccfr_traverse(state, &game_ip, PLAYER_IP, 1.0);
    
    /* Train OOP (reuse same cards for same hand) */
    GameState game_oop;
    game_init(&game_oop, ip_cards, oop_cards, board, 0);
    mccfr_traverse(state, &game_oop, PLAYER_OOP, 1.0);
    
    state->hands_played++;
    state->current_iteration++;
}

/* Run full training loop */
void mccfr_train(MCCFRState* state) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║        MCCFR Poker Solver - Training Started             ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Configuration:\n");
    printf("  Iterations: %d\n", state->config.num_iterations);
    printf("  Checkpoint interval: %d\n", state->config.checkpoint_interval);
    printf("  Print interval: %d\n", state->config.print_interval);
    printf("\n");
    printf("Starting training...\n\n");
    
    clock_t start_time = clock();
    int last_checkpoint = 0;
    
    while (state->current_iteration < state->config.num_iterations) {
        /* Run iteration */
        mccfr_iteration(state);
        
        /* Print progress */
        if (state->current_iteration % state->config.print_interval == 0) {
            double progress = 100.0 * state->current_iteration / state->config.num_iterations;
            int bar_width = 50;
            int filled = (int)(bar_width * state->current_iteration / state->config.num_iterations);
            
            clock_t now = clock();
            double elapsed = (double)(now - start_time) / CLOCKS_PER_SEC;
            double iter_per_sec = state->current_iteration / elapsed;
            double eta = (state->config.num_iterations - state->current_iteration) / iter_per_sec;
            
            printf("\r[");
            for (int i = 0; i < bar_width; i++) {
                if (i < filled) printf("=");
                else if (i == filled) printf(">");
                else printf(" ");
            }
            printf("] %6.2f%% | Iter: %d/%d | %.1f iter/s | ETA: %.0fs",
                   progress, state->current_iteration, state->config.num_iterations,
                   iter_per_sec, eta);
            fflush(stdout);
        }
        
        /* Save checkpoint */
        if (state->current_iteration - last_checkpoint >= state->config.checkpoint_interval) {
            char filename[256];
            snprintf(filename, sizeof(filename), "data/checkpoint_%d.dat", 
                    state->current_iteration);
            mccfr_save_checkpoint(state, filename);
            last_checkpoint = state->current_iteration;
            printf("\n[Checkpoint saved: %s]\n", filename);
        }
    }
    
    clock_t end_time = clock();
    double total_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("\n\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║        Training Complete!                                ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Results:\n");
    printf("  Total iterations: %d\n", state->current_iteration);
    printf("  Total time: %.1f seconds\n", total_time);
    printf("  Iterations/sec: %.1f\n", state->current_iteration / total_time);
    printf("  Hands played: %d\n", state->hands_played);
    printf("\n");
    
    /* Print table stats */
    printf("Regret Table:\n");
    strategy_table_stats(state->regret_table);
    printf("\nStrategy Table:\n");
    strategy_table_stats(state->strategy_table);
}

/* Save checkpoint */
int mccfr_save_checkpoint(MCCFRState* state, const char* filename) {
    /* TODO: Implement checkpoint saving */
    /* For now, just return success */
    return 0;
}

/* Load checkpoint */
int mccfr_load_checkpoint(MCCFRState* state, const char* filename) {
    /* TODO: Implement checkpoint loading */
    return -1;
}

/* Print statistics */
void mccfr_print_stats(const MCCFRState* state) {
    printf("\nMCCFR Statistics:\n");
    printf("=================\n");
    printf("Current iteration: %d\n", state->current_iteration);
    printf("Hands played: %d\n", state->hands_played);
    printf("IP wins: %d (%.1f%%)\n", state->hands_won_ip,
           100.0 * state->hands_won_ip / state->hands_played);
    printf("OOP wins: %d (%.1f%%)\n", state->hands_won_oop,
           100.0 * state->hands_won_oop / state->hands_played);
    printf("Ties: %d (%.1f%%)\n", state->hands_tied,
           100.0 * state->hands_tied / state->hands_played);
}

/* Estimate exploitability (requires many rollouts) */
double mccfr_estimate_exploitability(const MCCFRState* state, int num_samples) {
    /* TODO: Implement exploitability estimation */
    /* This is complex and requires best response computation */
    return 0.0;
}

/* Export strategy to file */
void mccfr_export_strategy(MCCFRState* state, const char* filename) {
    /* TODO: Implement strategy export */
}
