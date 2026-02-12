/**
 * actions.h - Poker Action Definitions
 * 
 * Defines all possible actions in our simplified poker game:
 * - FOLD: Give up the hand
 * - CHECK: Pass action without betting (only if no bet to call)
 * - BET_20: Bet 20% of pot
 * - BET_33: Bet 33% of pot (1/3 pot)
 * - BET_52: Bet 52% of pot (1/2 pot)
 * - BET_100: Bet 100% of pot (pot-sized bet)
 * - RAISE_2_5X: Raise to 2.5x the current bet (only when facing a bet)
 * 
 * Betting Structure:
 * - IP (In Position, Button) posts 0.5bb
 * - OOP (Out of Position, Big Blind) posts 1bb
 * - All bets are calculated as percentage of current pot
 * - Raises are only allowed when facing a bet, and are 2.5x
 */

#ifndef ACTIONS_H
#define ACTIONS_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum number of actions in any situation */
#define MAX_ACTIONS 7

/* 
 * Action types
 * Note: BET_20 through BET_100 are only valid when no bet to call
 *       RAISE_2_5X is only valid when facing a bet
 */
typedef enum {
    ACTION_FOLD = 0,      /* Give up hand */
    ACTION_CHECK,         /* Pass without betting */
    ACTION_CALL,          /* Call current bet */
    ACTION_BET_20,        /* Bet 20% of pot */
    ACTION_BET_33,        /* Bet 33% of pot */
    ACTION_BET_52,        /* Bet 52% of pot */
    ACTION_BET_100,       /* Bet 100% of pot (pot-sized) */
    ACTION_RAISE_2_5X,    /* Raise to 2.5x current bet */
    NUM_ACTIONS
} Action;

/* Betting limits and constants */
#define STARTING_STACK 100.0    /* Starting stack in bb */
#define SB_SIZE 0.5             /* Small blind (IP posts this) */
#define BB_SIZE 1.0             /* Big blind (OOP posts this) */

/* Bet sizes as pot fractions */
#define BET_20_PCT 0.20
#define BET_33_PCT 0.333333
#define BET_52_PCT 0.52
#define BET_100_PCT 1.0
#define RAISE_MULTIPLIER 2.5

/* 
 * Action utilities
 */

/* Get action name as string */
const char* action_to_string(Action a);

/* Get action name for display */
const char* action_to_display_string(Action a);

/* Check if action is a bet (not fold, check, or raise) */
bool action_is_bet(Action a);

/* Check if action is aggressive (bet or raise) */
bool action_is_aggressive(Action a);

/*
 * Bet sizing utilities
 */

/* Calculate bet size in bb given current pot and action */
double calculate_bet_size(double current_pot, Action a);

/* Calculate raise amount given current bet to call */
double calculate_raise_size(double current_bet, double current_pot);

/*
 * Legal action determination
 */

/* 
 * Get legal actions for current game state
 * 
 * Parameters:
 *   actions - output array to store legal actions
 *   to_call - amount needed to call (0 if no bet to call)
 *   stack - player's remaining stack
 *   pot - current pot size
 * 
 * Returns: number of legal actions
 */
int get_legal_actions(Action* actions, double to_call, double stack, double pot);

/* Check if specific action is legal */
bool is_action_legal(Action a, double to_call, double stack, double pot);

#endif /* ACTIONS_H */
