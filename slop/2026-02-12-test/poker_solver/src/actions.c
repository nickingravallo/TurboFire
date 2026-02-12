/**
 * actions.c - Action Implementation
 */

#include <string.h>
#include "actions.h"

/* Action names for internal use */
static const char* ACTION_NAMES[] = {
    "FOLD",
    "CHECK",
    "CALL",
    "BET_20",
    "BET_33",
    "BET_52",
    "BET_100",
    "RAISE_2_5X"
};

/* Action names for display */
static const char* ACTION_DISPLAY_NAMES[] = {
    "Fold",
    "Check",
    "Call",
    "Bet 20%",
    "Bet 33%",
    "Bet 52%",
    "Bet 100%",
    "Raise 2.5x"
};

const char* action_to_string(Action a) {
    if (a < 0 || a >= NUM_ACTIONS) return "UNKNOWN";
    return ACTION_NAMES[a];
}

const char* action_to_display_string(Action a) {
    if (a < 0 || a >= NUM_ACTIONS) return "Unknown";
    return ACTION_DISPLAY_NAMES[a];
}

bool action_is_bet(Action a) {
    return a == ACTION_BET_20 || a == ACTION_BET_33 || 
           a == ACTION_BET_52 || a == ACTION_BET_100;
}

bool action_is_aggressive(Action a) {
    return action_is_bet(a) || a == ACTION_RAISE_2_5X;
}

double calculate_bet_size(double current_pot, Action a) {
    switch (a) {
        case ACTION_BET_20:
            return current_pot * BET_20_PCT;
        case ACTION_BET_33:
            return current_pot * BET_33_PCT;
        case ACTION_BET_52:
            return current_pot * BET_52_PCT;
        case ACTION_BET_100:
            return current_pot * BET_100_PCT;
        default:
            return 0.0;
    }
}

double calculate_raise_size(double current_bet, double current_pot) {
    /* Raise to 2.5x the current bet amount */
    /* This means adding 1.5x the current bet to call */
    return current_bet * RAISE_MULTIPLIER;
}

int get_legal_actions(Action* actions, double to_call, double stack, double pot) {
    int count = 0;
    
    /* FOLD is always legal (except when you can check) */
    /* Actually, we always allow fold as a choice */
    actions[count++] = ACTION_FOLD;
    
    if (to_call == 0) {
        /* No bet to call - can check or bet */
        actions[count++] = ACTION_CHECK;
        
        /* All bet sizes are legal (assuming we have enough stack) */
        /* Bet 20% */
        double bet20 = calculate_bet_size(pot, ACTION_BET_20);
        if (bet20 > 0 && bet20 <= stack) {
            actions[count++] = ACTION_BET_20;
        }
        
        /* Bet 33% */
        double bet33 = calculate_bet_size(pot, ACTION_BET_33);
        if (bet33 > 0 && bet33 <= stack) {
            actions[count++] = ACTION_BET_33;
        }
        
        /* Bet 52% */
        double bet52 = calculate_bet_size(pot, ACTION_BET_52);
        if (bet52 > 0 && bet52 <= stack) {
            actions[count++] = ACTION_BET_52;
        }
        
        /* Bet 100% */
        double bet100 = calculate_bet_size(pot, ACTION_BET_100);
        if (bet100 > 0 && bet100 <= stack) {
            actions[count++] = ACTION_BET_100;
        }
    } else {
        /* There's a bet to call - we can fold, call, or raise */
        
        /* CALL: always legal if we can afford it */
        if (to_call <= stack) {
            actions[count++] = ACTION_CALL;
        }
        
        /* RAISE 2.5x: legal if we can afford the full raise */
        double raise_amount = calculate_raise_size(to_call, pot);
        double total_to_raise = to_call + raise_amount;
        if (total_to_raise <= stack) {
            actions[count++] = ACTION_RAISE_2_5X;
        }
    }
    
    return count;
}

bool is_action_legal(Action a, double to_call, double stack, double pot) {
    Action legal[MAX_ACTIONS];
    int num_legal = get_legal_actions(legal, to_call, stack, pot);
    
    for (int i = 0; i < num_legal; i++) {
        if (legal[i] == a) return true;
    }
    return false;
}
