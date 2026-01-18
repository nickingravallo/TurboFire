/*
 * MCCFR.c - Monte Carlo Counterfactual Regret Minimization Implementation
 * 
 * Implements CFR algorithm for solving heads-up poker post-flop scenarios
 */

#include "MCCFR.h"
#include "HandRanks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

#define INITIAL_CAPACITY 500   // Reduced to save memory
#define MAX_CAPACITY 50000      // Maximum hash table size (reduced)
#define LOAD_FACTOR 0.75

// Hash function for info sets
static uint64_t hash_combine(uint64_t a, uint64_t b) {
    return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2));
}

uint64_t mccfr_hash_infoset(const InfoSet *iset) {
    uint64_t hash = 0;
    
    // Hash board cards
    for (int i = 0; i < 5; i++) {
        if (iset->board_cards[i] >= 0) {
            hash = hash_combine(hash, iset->board_cards[i]);
        }
    }
    
    // Hash action history
    hash = hash_combine(hash, iset->street);
    hash = hash_combine(hash, iset->player);
    hash = hash_combine(hash, iset->num_actions);
    
    for (int i = 0; i < iset->num_actions && i < 10; i++) {
        hash = hash_combine(hash, iset->action_history[i]);
    }
    
    return hash;
}

static void init_hash_table(MCCFRSolver *solver, int capacity) {
    solver->hash_capacity = capacity;
    solver->hash_size = 0;
    solver->hash_table = calloc(capacity, sizeof(HashEntry));
    if (!solver->hash_table) {
        fprintf(stderr, "Error: Failed to allocate hash table\n");
        exit(1);
    }
}

static void free_hash_table(MCCFRSolver *solver) {
    if (solver && solver->hash_table) {
        free(solver->hash_table);
        solver->hash_table = NULL;
        solver->hash_size = 0;
        solver->hash_capacity = 0;
    }
}

static int resize_hash_table(MCCFRSolver *solver) {
    int old_cap = solver->hash_capacity;
    HashEntry *old_table = solver->hash_table;
    
    // Check max capacity
    if (old_cap >= MAX_CAPACITY) {
        fprintf(stderr, "Warning: Hash table at max capacity (%d), cannot resize further\n", MAX_CAPACITY);
        return 0;  // Failed to resize
    }
    
    // Allocate new table
    int new_cap = old_cap * 2;
    if (new_cap > MAX_CAPACITY) {
        new_cap = MAX_CAPACITY;
    }
    HashEntry *new_table = calloc(new_cap, sizeof(HashEntry));
    if (!new_table) {
        fprintf(stderr, "Error: Failed to resize hash table\n");
        exit(1);
    }
    
    // Update solver with new table temporarily
    solver->hash_table = new_table;
    solver->hash_capacity = new_cap;
    solver->hash_size = 0;
    
    // Rehash all entries
    for (int i = 0; i < old_cap; i++) {
        if (old_table[i].key_hash != 0) {
            uint64_t hash = old_table[i].key_hash;
            int index = hash % new_cap;
            int start_index = index;
            
            // Linear probing to find empty slot
            while (new_table[index].key_hash != 0) {
                index = (index + 1) % new_cap;
                if (index == start_index) {
                    // This shouldn't happen if we doubled capacity
                    fprintf(stderr, "Error: New hash table full during resize\n");
                    exit(1);
                }
            }
            
            new_table[index] = old_table[i];
            solver->hash_size++;
        }
    }
    
    // Free old table
    free(old_table);
    return 1;  // Success
}

static InfoSetData* find_or_create_entry(MCCFRSolver *solver, const InfoSet *iset) {
    if (solver->hash_table == NULL) {
        init_hash_table(solver, INITIAL_CAPACITY);
    }
    
    // Check if we need to resize before inserting
    if (solver->hash_size >= solver->hash_capacity * LOAD_FACTOR) {
        if (!resize_hash_table(solver)) {
            // Resize failed (at max capacity), continue with current table
            // This might cause performance issues but won't crash
        }
    }
    
    uint64_t hash = mccfr_hash_infoset(iset);
    int index = hash % solver->hash_capacity;
    int start_index = index;
    int attempts = 0;
    const int max_attempts = solver->hash_capacity * 2;  // Prevent infinite loop
    
    // Linear probing to find existing entry
    while (solver->hash_table[index].key_hash != 0 && 
           solver->hash_table[index].key_hash != hash) {
        index = (index + 1) % solver->hash_capacity;
        attempts++;
        
        if (index == start_index || attempts >= max_attempts) {
            // Table full, try to resize and retry
            if (resize_hash_table(solver)) {
                // Resize succeeded, recalculate index
                index = hash % solver->hash_capacity;
                start_index = index;
                attempts = 0;
                // Continue probing in new table
                while (solver->hash_table[index].key_hash != 0 && 
                       solver->hash_table[index].key_hash != hash &&
                       attempts < max_attempts) {
                    index = (index + 1) % solver->hash_capacity;
                    attempts++;
                    if (index == start_index) {
                        fprintf(stderr, "Error: Hash table completely full after resize\n");
                        exit(1);
                    }
                }
                break;
            } else {
                // Resize failed, table is at max capacity
                fprintf(stderr, "Error: Hash table at max capacity and full\n");
                exit(1);
            }
        }
    }
    
    // Create new entry if it doesn't exist
    if (solver->hash_table[index].key_hash == 0) {
        solver->hash_table[index].key_hash = hash;
        solver->hash_table[index].iset = *iset;
        memset(&solver->hash_table[index].data, 0, sizeof(InfoSetData));
        solver->hash_size++;
    }
    
    return &solver->hash_table[index].data;
}

MCCFRSolver* mccfr_create(int p0_c0, int p0_c1, int p1_c0, int p1_c1, HandRankTables *hr) {
    MCCFRSolver *solver = calloc(1, sizeof(MCCFRSolver));
    if (!solver) {
        fprintf(stderr, "Error: Failed to allocate solver\n");
        return NULL;
    }
    
    solver->hand_ranks = hr;
    solver->p0_hand[0] = p0_c0;
    solver->p0_hand[1] = p0_c1;
    solver->p1_hand[0] = p1_c0;
    solver->p1_hand[1] = p1_c1;
    solver->current_street = STREET_FLOP;
    solver->pot_size = 1.0;
    solver->bet_size = 1.0;
    solver->hash_table = NULL;
    solver->hash_size = 0;
    solver->hash_capacity = 0;
    
    for (int i = 0; i < 5; i++) {
        solver->board[i] = -1;
    }
    
    init_hash_table(solver, INITIAL_CAPACITY);
    
    return solver;
}

void mccfr_free(MCCFRSolver *solver) {
    if (solver) {
        free_hash_table(solver);
        free(solver);
    }
}

void mccfr_set_board(MCCFRSolver *solver, int *board_cards, Street street) {
    solver->current_street = street;
    for (int i = 0; i < 5; i++) {
        solver->board[i] = board_cards[i];
    }
}

double mccfr_evaluate_hand(MCCFRSolver *solver, int player, int *board, int board_size) {
    int hand[2];
    if (player == 0) {
        hand[0] = solver->p0_hand[0];
        hand[1] = solver->p0_hand[1];
    } else {
        hand[0] = solver->p1_hand[0];
        hand[1] = solver->p1_hand[1];
    }
    
    // Validate board cards
    int valid_board_size = 0;
    for (int i = 0; i < board_size && i < 5; i++) {
        if (board[i] >= 0 && board[i] < 52) {
            valid_board_size++;
        }
    }
    
    if (valid_board_size < 3) {
        // Not enough board cards, return neutral value
        return 0.5;
    }
    
    // Convert to 7-card evaluation
    int cards[7];
    cards[0] = hand[0];
    cards[1] = hand[1];
    int card_idx = 2;
    for (int i = 0; i < valid_board_size && card_idx < 7; i++) {
        if (board[i] >= 0 && board[i] < 52) {
            cards[card_idx++] = board[i];
        }
    }
    
    // Pad with -1 if needed (hr_eval_7 should handle this, but be safe)
    while (card_idx < 7) {
        cards[card_idx++] = -1;
    }
    
    // Evaluate best 5-card hand from available cards
    int best_rank = hr_eval_7(solver->hand_ranks, 
                              cards[0], cards[1], cards[2], cards[3], 
                              cards[4], cards[5], cards[6]);
    
    // Return normalized value (lower rank = better hand)
    return 1.0 / (1.0 + best_rank / 7462.0);
}

// Terminal node evaluation
static double evaluate_terminal(MCCFRSolver *solver, int *board, int board_size, int last_action) {
    if (last_action == ACTION_FOLD) {
        // Player who didn't fold wins the pot
        // In heads-up, if P0 folds, P1 wins 1.0 (the pot)
        // If P1 folds, P0 wins 1.0
        return 1.0;  // This will be negated for P1
    }
    
    // Showdown - compare hand strengths
    double p0_strength = mccfr_evaluate_hand(solver, 0, board, board_size);
    double p1_strength = mccfr_evaluate_hand(solver, 1, board, board_size);
    
    // Return expected value for P0
    if (p0_strength > p1_strength) {
        return 1.0;  // P0 wins pot
    } else if (p0_strength < p1_strength) {
        return -1.0;  // P1 wins pot (negative for P0)
    } else {
        return 0.0;  // Tie - split pot
    }
}

// Check if node is terminal
static int is_terminal(const InfoSet *iset, int last_action) {
    if (last_action == ACTION_FOLD) {
        return 1;
    }
    
    // Terminal if river and both players have acted (checked/called)
    if (iset->street == STREET_RIVER && iset->num_actions >= 2) {
        // Check if last two actions were both check/call (both players checked)
        if (last_action == ACTION_CHECK_CALL) {
            // Count check/call actions
            int check_count = 0;
            for (int i = 0; i < iset->num_actions; i++) {
                if (iset->action_history[i] == ACTION_CHECK_CALL) {
                    check_count++;
                }
            }
            // If we have at least 2 check/calls, it's a showdown
            if (check_count >= 2) {
                return 1;
            }
        }
    }
    
    // Terminal if we've had a bet followed by a call (betting sequence completed)
    if (iset->num_actions >= 2) {
        int last = iset->action_history[iset->num_actions - 1];
        int second_last = iset->action_history[iset->num_actions - 2];
        // Bet/Call or Bet/Fold sequences are terminal
        if ((second_last == ACTION_BET_RAISE && last == ACTION_CHECK_CALL) ||
            (second_last == ACTION_BET_RAISE && last == ACTION_FOLD)) {
            return 1;
        }
    }
    
    // Limit action sequence length to prevent infinite recursion
    if (iset->num_actions >= 10) {
        return 1;  // Force terminal after 10 actions
    }
    
    return 0;
}

// CFR algorithm
double mccfr_cfr(MCCFRSolver *solver, InfoSet *iset, double reach_p0, double reach_p1, int depth) {
    if (depth > 10) {
        return 0.0;  // Safety limit - prevent infinite recursion
    }
    
    if (reach_p0 < 1e-10 || reach_p1 < 1e-10) {
        return 0.0;  // Negligible reach probability
    }
    
    InfoSetData *data = find_or_create_entry(solver, iset);
    data->visits++;
    
    // Check if terminal
    if (iset->num_actions > 0) {
        int last_action = iset->action_history[iset->num_actions - 1];
        if (is_terminal(iset, last_action)) {
            // Determine board size based on street
            int board_size = 0;
            for (int i = 0; i < 5; i++) {
                if (iset->board_cards[i] >= 0 && iset->board_cards[i] < 52) {
                    board_size++;
                }
            }
            // Ensure we have at least 3 cards for flop
            if (board_size < 3) {
                board_size = (iset->street == STREET_FLOP) ? 3 : 
                            (iset->street == STREET_TURN) ? 4 : 5;
            }
            
            double value = evaluate_terminal(solver, iset->board_cards, board_size, last_action);
            // Return value from perspective of current player (P0)
            // If P1 is acting, negate the value
            double result = (iset->player == 0) ? value : -value;
            return result;
        }
    }
    
    // Compute strategy from regrets
    double strategy[MAX_ACTIONS];
    double normalizing_sum = 0.0;
    
    for (int a = 0; a < MAX_ACTIONS; a++) {
        strategy[a] = (data->regrets[a] > 0) ? data->regrets[a] : 0.0;
        normalizing_sum += strategy[a];
    }
    
    if (normalizing_sum > 0) {
        for (int a = 0; a < MAX_ACTIONS; a++) {
            strategy[a] /= normalizing_sum;
        }
    } else {
        // Uniform strategy
        for (int a = 0; a < MAX_ACTIONS; a++) {
            strategy[a] = 1.0 / MAX_ACTIONS;
        }
    }
    
    // Update strategy sum
    double node_util = 0.0;
    double util[MAX_ACTIONS];
    
    // Compute utility for each action
    for (int a = 0; a < MAX_ACTIONS; a++) {
        InfoSet next_iset = *iset;
        
        if (a == ACTION_FOLD) {
            // Fold loses the pot (1.0) for the folding player
            // From P0's perspective: if P0 folds, P0 loses 1.0; if P1 folds, P0 wins 1.0
            util[a] = (iset->player == 0) ? -1.0 : 1.0;
        } else {
            // ACTION_CHECK_CALL
            // Create next info set
            if (next_iset.num_actions < 10) {
                next_iset.action_history[next_iset.num_actions] = a;
                next_iset.num_actions++;
            }
            
            // Switch player
            next_iset.player = 1 - next_iset.player;
            
            // Advance street if needed (both players checked/called)
            // Only advance when action sequence shows both players acted
            if (a == ACTION_CHECK_CALL && next_iset.num_actions >= 2) {
                // Check if we've had two consecutive checks
                if (next_iset.num_actions >= 2 && 
                    next_iset.action_history[next_iset.num_actions - 2] == ACTION_CHECK_CALL &&
                    next_iset.action_history[next_iset.num_actions - 1] == ACTION_CHECK_CALL) {
                    if (next_iset.street == STREET_FLOP) {
                        next_iset.street = STREET_TURN;
                        // Reset action history for new street
                        next_iset.num_actions = 0;
                    } else if (next_iset.street == STREET_TURN) {
                        next_iset.street = STREET_RIVER;
                        // Reset action history for new street
                        next_iset.num_actions = 0;
                    }
                }
            }
            
            double next_reach_p0 = (iset->player == 0) ? reach_p0 * strategy[a] : reach_p0;
            double next_reach_p1 = (iset->player == 1) ? reach_p1 * strategy[a] : reach_p1;
            
            util[a] = mccfr_cfr(solver, &next_iset, next_reach_p0, next_reach_p1, depth + 1);
        }
        
        node_util += strategy[a] * util[a];
    }
    
    // Update regrets using counterfactual values
    double reach = (iset->player == 0) ? reach_p0 : reach_p1;
    double counterfactual_reach = (iset->player == 0) ? reach_p1 : reach_p0;
    
    for (int a = 0; a < MAX_ACTIONS; a++) {
        double regret = util[a] - node_util;
        // Counterfactual regret: multiply by opponent's reach probability
        // Don't clamp regrets - negative regrets are fine, we use max(0, regret) in strategy
        data->regrets[a] += counterfactual_reach * regret;
        
        // Update strategy sum (average strategy) - this accumulates over all iterations
        data->strategy_sum[a] += reach * strategy[a];
    }
    
    return node_util;
}

void mccfr_update_strategy(MCCFRSolver *solver) {
    // Strategy is already being updated in CFR via strategy_sum
    // This function can be used for final normalization if needed
}

void mccfr_solve(MCCFRSolver *solver, int iterations) {
    // Reduced logging - only show start and completion
    for (int i = 0; i < iterations; i++) {
        // Create initial info set
        InfoSet iset;
        memset(&iset, 0, sizeof(InfoSet));
        iset.street = solver->current_street;
        iset.player = 0;
        iset.num_actions = 0;
        
        for (int j = 0; j < 5; j++) {
            iset.board_cards[j] = solver->board[j];
        }
        
        mccfr_cfr(solver, &iset, 1.0, 1.0, 0);
    }
    
    // Normalize strategies
    for (int i = 0; i < solver->hash_capacity; i++) {
        if (solver->hash_table[i].key_hash != 0) {
            InfoSetData *data = &solver->hash_table[i].data;
            double sum = 0.0;
            for (int a = 0; a < MAX_ACTIONS; a++) {
                sum += data->strategy_sum[a];
            }
            if (sum > 0) {
                for (int a = 0; a < MAX_ACTIONS; a++) {
                    data->strategy[a] = data->strategy_sum[a] / sum;
                }
            } else {
                for (int a = 0; a < MAX_ACTIONS; a++) {
                    data->strategy[a] = 1.0 / MAX_ACTIONS;
                }
            }
        }
    }
}

void mccfr_print_strategy(MCCFRSolver *solver, Street street) {
    const char *street_names[] = {"Flop", "Turn", "River"};
    const char *action_names[] = {"Check/Call", "Bet/Raise", "Fold"};
    
    printf("\n=== GTO Strategy for %s ===\n\n", street_names[street]);
    
    int printed = 0;
    for (int i = 0; i < solver->hash_capacity && printed < 20; i++) {
        if (solver->hash_table[i].key_hash != 0 && 
            solver->hash_table[i].iset.street == street &&
            solver->hash_table[i].iset.num_actions == 0) {
            
            InfoSetData *data = &solver->hash_table[i].data;
            
            printf("Player %d Strategy:\n", solver->hash_table[i].iset.player);
            for (int a = 0; a < MAX_ACTIONS; a++) {
                printf("  %s: %.2f%%\n", action_names[a], data->strategy[a] * 100.0);
            }
            printf("  (Visits: %lu)\n\n", data->visits);
            printed++;
        }
    }
    
    if (printed == 0) {
        printf("No strategy data found for %s.\n", street_names[street]);
    }
}

InfoSetData* mccfr_get_or_create(MCCFRSolver *solver, const InfoSet *iset) {
    return find_or_create_entry(solver, iset);
}
